/*
 * executor_child.cpp - the IPC child for `--backend executor`.
 *
 * Same contract as the ROM backends (see src/ipc/ipc_protocol.h): frames go
 * into the SHM triple buffer, input arrives on the control socket, status
 * fields drive /api/status and /api/app. The difference is what runs: the
 * Executor Toolbox on UAE, with no ROM.
 */
#include "executor_child.h"
#include "executor_host.h"

#include "emulator_config.h"
#include "ipc_protocol.h"
#include "../../core/snapshot.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "video_modes.h"

extern "C" void control_ipc_start(void);

namespace {

IPCBuffer *g_buf = nullptr;
bool g_first_frame = true;
bool g_desktop = false;

uint64_t monotonic_us()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
}

void set_phase(const char *phase)
{
    strncpy(g_buf->boot_phase, phase, sizeof(g_buf->boot_phase) - 1);
    g_buf->boot_phase[sizeof(g_buf->boot_phase) - 1] = 0;
}

void publish_frame(const uint32_t *pixels, int w, int h)
{
    if (!g_buf)
        return;
    w = std::min<int>(w, mp::video::kMaxWidth_M68k);
    h = std::min<int>(h, mp::video::kMaxHeight_M68k);

    uint32_t idx = IPC_ATOMIC_LOAD(g_buf->write_index);
    uint8_t *dst = g_buf->frames[idx];
    memcpy(dst, pixels, (size_t)w * h * 4);
    g_buf->width = w;
    g_buf->height = h;
    // Executor renders 0xAARRGGBB words: bytes B,G,R,A on little-endian.
    g_buf->pixel_format = IPC_PIXFMT_BGRA;

    int cx, cy;
    executor_host::get_mouse(cx, cy);
    g_buf->shm_cursor_x = cx;
    g_buf->shm_cursor_y = cy;
    g_buf->shm_raw_x = cx;
    g_buf->shm_raw_y = cy;

    std::string app = executor_host::current_app_name();
    strncpy(g_buf->cur_app_name, app.c_str(), sizeof(g_buf->cur_app_name) - 1);

    // There is no ROM boot. First frame: the shell app is starting
    // ("Finder"). First event poll: it is idle in its event loop ("desktop").
    if (g_first_frame) {
        set_phase("Finder");
        g_first_frame = false;
    }
    if (!g_desktop && executor_host::app_idle()) {
        set_phase("desktop");
        g_desktop = true;
    }

    auto now = std::chrono::steady_clock::now().time_since_epoch();
    ipc_frame_complete(g_buf,
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

void hook_key(int down, uint8_t code) { executor_host::key(down != 0, code); }
void hook_abs(int x, int y) { executor_host::mouse_moved(x, y); }
void hook_rel(int dx, int dy) { executor_host::mouse_moved_relative(dx, dy); }
void hook_button(int button, int down)
{
    if (button == 0)
        executor_host::mouse_button(down != 0);
}

const IPCInputHooks g_hooks = { hook_key, hook_abs, hook_rel, hook_button };

std::string expand_home(std::string p)
{
    if (!p.empty() && p[0] == '~') {
        if (const char *home = getenv("HOME"))
            p = home + p.substr(1);
    }
    return p;
}

}  // namespace

int executor_child_main(const config::EmulatorConfig& cfg, IPCBuffer *buf)
{
    g_buf = buf;
    buf->boot_start_us = monotonic_us();
    set_phase("boot globs");

    control_ipc_set_input_hooks(&g_hooks);
    control_ipc_start();
    buf->state = IPC_STATE_RUNNING;

    // POST /api/snapshot: dump guest RAM (identity-mapped at 0) when the
    // app next asks for events, where the heap is consistent.
    executor_host::set_event_poll_hook([] {
        if (!snapshot_pending())
            return;
        SnapshotMemory mem;
        mem.ram = (const uint8_t *)(uintptr_t)0;
        mem.ram_size = executor_host::guest_ram_size();
        mem.context = "Executor event poll (GetNextEvent/WaitNextEvent)";
        snapshot_service(mem);
    });

    executor_host::Config c;
    c.width = cfg.screen_width;
    c.height = cfg.screen_height;
    c.bpp = 8;
    c.ram_mb = (int)cfg.ram_mb;
    c.data_dir = expand_home(cfg.storage_dir) + "/executor";
    c.disks = cfg.disk_paths;
    c.shared_folders = cfg.extfs_paths;
    c.on_frame = publish_frame;

    fprintf(stderr, "[Executor] %dx%d, %d MB, data in %s, %zu disk image(s) read-only\n",
            c.width, c.height, c.ram_mb, c.data_dir.c_str(), c.disks.size());

    int rc = executor_host::run(c);
    fprintf(stderr, "[Executor] guest exited (%d)\n", rc);
    return rc;
}

int executor_direct_main(const config::EmulatorConfig& cfg)
{
    executor_host::Config c;
    c.width = cfg.screen_width;
    c.height = cfg.screen_height;
    c.ram_mb = (int)cfg.ram_mb;
    c.data_dir = expand_home(cfg.storage_dir) + "/executor";
    c.disks = cfg.disk_paths;
    c.shared_folders = cfg.extfs_paths;
    return executor_host::run(c);
}
