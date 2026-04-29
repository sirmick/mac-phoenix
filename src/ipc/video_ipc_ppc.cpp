/*
 * video_ipc_ppc.cpp - IPC video driver for subprocess mode
 *
 * Creates shared memory for video frames. The frame copy happens in
 * video_ipc_refresh() which is called from the 60Hz timer interrupt.
 *
 * Works for both m68k and PPC: call video_ipc_set_framebuffer() to
 * tell us where the host framebuffer lives. For PPC this is screen_base;
 * for m68k it's the buffer allocated by video_init.
 *
 * Parent connects to this SHM to relay frames to VideoOutput/encoder.
 */

#include "ipc_protocol.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <chrono>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// IPC resources (child-owned)
static IPCBuffer* g_ipc_buffer = nullptr;
static int g_shm_fd = -1;
static std::string g_shm_name;

// Frame parameters
static int g_frame_width = 640;
static int g_frame_height = 480;

// Current Mac-side pixel geometry. g_frame_depth_bits is 1/2/4/8/16/32;
// g_frame_bpr is the stride of the Mac framebuffer at the current depth.
// When bits == 32 we fast-path memcpy; otherwise we unpack/palette-lookup
// into an ARGB scratch buffer before writing to SHM.
static int g_frame_depth_bits = 32;
static int g_frame_bpr = 0;      // 0 = derive from width*4

// Active CLUT — populated via video_ipc_set_palette() from Mac
// SetEntries calls. Indexed 1/2/4/8-bit modes read from here.
static uint8_t g_frame_palette[256 * 3];

// Framebuffer pointer (set by architecture-specific init)
static const uint8_t* g_framebuffer_ptr = nullptr;

/*
 * Shared memory creation (child owns this)
 */

static bool create_video_shm()
{
    pid_t pid = getpid();
    g_shm_name = std::string(IPC_VIDEO_SHM_PREFIX) + std::to_string(pid);

    // Remove any stale SHM
    shm_unlink(g_shm_name.c_str());

    g_shm_fd = shm_open(g_shm_name.c_str(), O_CREAT | O_RDWR, 0600);
    if (g_shm_fd < 0) {
        fprintf(stderr, "IPC: Failed to create SHM %s: %s\n",
                g_shm_name.c_str(), strerror(errno));
        return false;
    }

    size_t shm_size = sizeof(IPCBuffer);
    if (ftruncate(g_shm_fd, shm_size) < 0) {
        fprintf(stderr, "IPC: Failed to size SHM: %s\n", strerror(errno));
        close(g_shm_fd);
        shm_unlink(g_shm_name.c_str());
        return false;
    }

    g_ipc_buffer = (IPCBuffer*)mmap(nullptr, shm_size,
                                     PROT_READ | PROT_WRITE,
                                     MAP_SHARED, g_shm_fd, 0);
    if (g_ipc_buffer == MAP_FAILED) {
        fprintf(stderr, "IPC: Failed to mmap SHM: %s\n", strerror(errno));
        close(g_shm_fd);
        shm_unlink(g_shm_name.c_str());
        g_ipc_buffer = nullptr;
        return false;
    }

    ipc_init_buffer(g_ipc_buffer, pid, g_frame_width, g_frame_height);

    fprintf(stderr, "IPC: Created SHM: %s (%zu bytes, %dx%d)\n",
            g_shm_name.c_str(), shm_size, g_frame_width, g_frame_height);
    return true;
}

static void destroy_video_shm()
{
    if (g_ipc_buffer) {
        g_ipc_buffer->state = IPC_STATE_STOPPED;
        if (g_ipc_buffer->frame_ready_eventfd >= 0) {
            close(g_ipc_buffer->frame_ready_eventfd);
        }
        munmap(g_ipc_buffer, sizeof(IPCBuffer));
        g_ipc_buffer = nullptr;
    }
    if (g_shm_fd >= 0) {
        close(g_shm_fd);
        g_shm_fd = -1;
    }
    if (!g_shm_name.empty()) {
        shm_unlink(g_shm_name.c_str());
        g_shm_name.clear();
    }
}

/*
 * Public API (Platform hooks)
 */

extern "C" {

bool video_ipc_init(int width, int height)
{
    g_frame_width = width;
    g_frame_height = height;

    if (!create_video_shm()) {
        return false;
    }

    g_ipc_buffer->state = IPC_STATE_RUNNING;

    fprintf(stderr, "IPC: Video driver initialized (%dx%d)\n", width, height);
    return true;
}

void video_ipc_exit(void)
{
    destroy_video_shm();
    fprintf(stderr, "IPC: Video driver shut down\n");
}

// Signal-safe: only unlinks the SHM name (no munmap, no malloc).
// Safe to call from a signal handler.
void video_ipc_unlink(void)
{
    if (!g_shm_name.empty()) {
        shm_unlink(g_shm_name.c_str());
    }
}

void video_ipc_set_framebuffer(const uint8_t* fb)
{
    g_framebuffer_ptr = fb;
}

void video_ipc_set_resolution(int width, int height)
{
    g_frame_width = width;
    g_frame_height = height;
    if (g_ipc_buffer) {
        g_ipc_buffer->width = width;
        g_ipc_buffer->height = height;
        fprintf(stderr, "IPC: Resolution updated to %dx%d\n", width, height);
    }
}

// Called by monitor_desc::switch_to_current_mode when the Mac picks a
// new depth. bpr is the framebuffer stride at that depth.
void video_ipc_set_depth(int depth_bits, int bytes_per_row)
{
    g_frame_depth_bits = depth_bits;
    g_frame_bpr = bytes_per_row;
    fprintf(stderr, "IPC: depth=%d bpr=%d\n", depth_bits, bytes_per_row);
}

// Called by monitor_desc::set_palette whenever the Mac issues SetEntries
// (happens continuously during boot and app launches in indexed modes).
void video_ipc_set_palette(const uint8_t *pal, int num)
{
    if (num < 0) return;
    if (num > 256) num = 256;
    memcpy(g_frame_palette, pal, (size_t)num * 3);
}

// Row-level depth → ARGB converters. We keep them local because
// video_ipc_ppc.cpp is the one hot-path that runs every frame; hoisting
// them into a shared helper adds a call-per-row with no measurable win.
//
// PIXFMT_ARGB is specified as "bytes A,R,G,B in memory" (matches the Mac's
// big-endian 32-bit layout after the emulator writes it byte-wise into
// host memory). On little-endian x86, the byte at offset 0 is the LSB of
// the stored uint32 — so to produce mem[0]=A, mem[1]=R, mem[2]=G, mem[3]=B
// the uint32 value has to be A | R<<8 | G<<16 | B<<24 — NOT the more
// "natural" A<<24 | R<<16 | G<<8 | B, which would write BGRA bytes and
// cause everything to render with R and B swapped (Mac beige desktop
// turns blue, familiar classic-emulator giveaway).
static inline uint32_t pack_argb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint32_t)0xff
         | ((uint32_t)r << 8)
         | ((uint32_t)g << 16)
         | ((uint32_t)b << 24);
}

static inline uint8_t expand5(uint8_t c) { return (c << 3) | (c >> 2); }

static void convert_row_argb(const uint8_t *src, int width, int depth_bits,
                             const uint8_t *pal, uint32_t *dst)
{
    switch (depth_bits) {
        case 1: {
            for (int x = 0; x < width; x++) {
                uint8_t bit = (src[x >> 3] >> (7 - (x & 7))) & 1;
                const uint8_t *p = &pal[bit * 3];
                dst[x] = pack_argb(p[0], p[1], p[2]);
            }
            break;
        }
        case 2: {
            for (int x = 0; x < width; x++) {
                uint8_t idx = (src[x >> 2] >> ((3 - (x & 3)) * 2)) & 0x3;
                const uint8_t *p = &pal[idx * 3];
                dst[x] = pack_argb(p[0], p[1], p[2]);
            }
            break;
        }
        case 4: {
            for (int x = 0; x < width; x++) {
                uint8_t idx = (src[x >> 1] >> ((1 - (x & 1)) * 4)) & 0xf;
                const uint8_t *p = &pal[idx * 3];
                dst[x] = pack_argb(p[0], p[1], p[2]);
            }
            break;
        }
        case 8: {
            for (int x = 0; x < width; x++) {
                const uint8_t *p = &pal[src[x] * 3];
                dst[x] = pack_argb(p[0], p[1], p[2]);
            }
            break;
        }
        case 16: {
            // Mac 16-bit = big-endian 0RRRRR GGGGG BBBBB
            for (int x = 0; x < width; x++) {
                uint16_t pix = ((uint16_t)src[x * 2] << 8) | src[x * 2 + 1];
                dst[x] = pack_argb(expand5((pix >> 10) & 0x1f),
                                   expand5((pix >> 5)  & 0x1f),
                                   expand5(pix & 0x1f));
            }
            break;
        }
        case 32:
        default:
            memcpy(dst, src, (size_t)width * 4);
            break;
    }
}

void video_ipc_refresh(void)
{
    // Called from 60Hz timer interrupt — copy framebuffer into SHM, converting
    // Mac-native pixel format to ARGB when needed.
    if (!g_ipc_buffer || !g_framebuffer_ptr) return;

    const int width = g_frame_width;
    const int height = g_frame_height;

    uint32_t write_idx = g_ipc_buffer->write_index;
    uint8_t* dest = g_ipc_buffer->frames[write_idx];

    const int depth_bits = g_frame_depth_bits;
    const int bpr = (g_frame_bpr > 0) ? g_frame_bpr : (width * (depth_bits / 8));

    if (depth_bits == 32) {
        // Fast path: Mac is already ARGB, just copy.
        memcpy(dest, g_framebuffer_ptr, (size_t)width * height * 4);
    } else {
        // Convert row-by-row at current depth + CLUT. `dest` is always
        // width*4 bytes per row of ARGB.
        uint32_t *dst_rows = reinterpret_cast<uint32_t *>(dest);
        for (int y = 0; y < height; y++) {
            convert_row_argb(g_framebuffer_ptr + y * bpr, width,
                             depth_bits, g_frame_palette,
                             dst_rows + y * width);
        }
    }

    g_ipc_buffer->pixel_format = IPC_PIXFMT_ARGB;

    // Signal frame complete
    auto now = std::chrono::steady_clock::now();
    auto timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    ipc_frame_complete(g_ipc_buffer, timestamp_us);
}

IPCBuffer* video_ipc_get_buffer(void)
{
    return g_ipc_buffer;
}

} // extern "C"
