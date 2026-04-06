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

void video_ipc_refresh(void)
{
    // Called from 60Hz timer interrupt — copy framebuffer into SHM
    if (!g_ipc_buffer || !g_framebuffer_ptr) return;

    const int width = g_frame_width;
    const int height = g_frame_height;

    uint32_t write_idx = g_ipc_buffer->write_index;
    uint8_t* dest = g_ipc_buffer->frames[write_idx];

    memcpy(dest, g_framebuffer_ptr, (size_t)width * height * 4);

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
