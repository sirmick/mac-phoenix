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

#include <csignal>
#include <cstdio>

extern "C++" uint32_t uae_current_pc(void);
#include "../../core/snapshot.h"
#include "../../common/include/m68k_registers.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "video_modes.h"

// Parameter RAM is the Basilisk side's (core/xpram.cpp); Executor's
// ReadXPRam/WriteXPRam work on it.
extern unsigned char XPRAM[];
void XPRAMInit(const char *vmdir);
void XPRAMSetDefaults(void);

namespace
{
void init_xpram(const config::EmulatorConfig& cfg)
{
    if (cfg.zappram)
        memset(XPRAM, 0, 0x100);
    else
        XPRAMInit(nullptr);
    XPRAMSetDefaults();
}
}

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

    // There is no ROM boot. The shell app has started ("Finder") once
    // CurApName names it (before that it holds Executor's own name). First
    // event poll: it is idle in its event loop ("desktop").
    if (g_first_frame && !app.empty() && app != "mac-phoenix") {
        set_phase("Finder");
        g_first_frame = false;
    }
    if (!g_first_frame && !g_desktop && executor_host::app_idle()) {
        set_phase("desktop");
        g_desktop = true;
    }

    auto now = std::chrono::steady_clock::now().time_since_epoch();
    ipc_frame_complete(g_buf,
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

std::string expand_home(std::string p)
{
    if (!p.empty() && p[0] == '~') {
        if (const char *home = getenv("HOME"))
            p = home + p.substr(1);
    }
    return p;
}

// Snapshot guest RAM as "executor-fatal": called on a fatal Executor error and
// when the host faults on a guest access (Executor maps guest memory 1:1, so a
// bad guest pointer is a host SIGSEGV).
// Where fatal snapshots go, copied at startup: at exit the config's strings
// may already be destroyed when a late fault comes in.
std::string g_snapshot_root;
std::atomic<bool> g_exiting{false};

void fatal_snapshot(const char *message)
{
    if (g_exiting || g_snapshot_root.empty())
        return;
    if (snapshot_prepare(g_snapshot_root, "executor-fatal").empty())
        return;
    snapshot_request();
    static M68kRegisters regs;
    executor_host::get_registers(regs.d, regs.a);
    SnapshotMemory mem;
    mem.ram = executor_host::guest_ram();
    mem.ram_size = executor_host::guest_ram_size();
    mem.context = message;
    mem.regs = &regs;
    snapshot_service(mem);
}

struct sigaction prev_segv, prev_bus;

void crash_snapshot(int sig, siginfo_t *info, void *uctx)
{
    static char message[96];
    snprintf(message, sizeof message, "host signal %d at guest pc $%08x, address %p",
             sig, uae_current_pc(), info->si_addr);
    fprintf(stderr, "[Executor] %s: snapshotting\n", message);
    fatal_snapshot(message);
    struct sigaction& prev = sig == SIGBUS ? prev_bus : prev_segv;
    sigaction(sig, &prev, nullptr);
    if (prev.sa_flags & SA_SIGINFO)
        prev.sa_sigaction(sig, info, uctx);
    else if (prev.sa_handler != SIG_DFL && prev.sa_handler != SIG_IGN)
        prev.sa_handler(sig);
    else
        raise(sig);
}

void install_crash_snapshot()
{
    struct sigaction sa = {};
    sa.sa_sigaction = crash_snapshot;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &prev_segv);
    sigaction(SIGBUS, &sa, &prev_bus);
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

}  // namespace

int executor_child_main(const config::EmulatorConfig& cfg, IPCBuffer *buf)
{
    g_buf = buf;
    buf->boot_start_us = monotonic_us();
    init_xpram(cfg);
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
        mem.ram = executor_host::guest_ram();
        mem.ram_size = executor_host::guest_ram_size();
        mem.context = "Executor event poll (GetNextEvent/WaitNextEvent)";
        snapshot_service(mem);
    });

    // A fatal Executor error snapshots guest RAM as "executor-fatal" (not
    // once the process is exiting: registered after main's IPC cleanup, so
    // this runs before it).
    g_snapshot_root = expand_home(cfg.storage_dir);
    atexit([] { g_exiting = true; });
    executor_host::set_fatal_hook(fatal_snapshot);
    install_crash_snapshot();

    executor_host::Config c;
    c.width = cfg.screen_width;
    c.height = cfg.screen_height;
    c.bpp = 8;
    c.ram_mb = (int)cfg.ram_mb;
    c.data_dir = cfg.executor_data_dir.empty() ? expand_home(cfg.storage_dir) + "/executor"
                                               : expand_home(cfg.executor_data_dir);
    c.disks = cfg.disk_paths;
    c.shared_folders = cfg.extfs_paths;
    c.cpu = cfg.cpu_core;
    c.app = expand_home(cfg.executor_app);
    c.logtraps = cfg.executor_logtraps;
    c.writable_images = cfg.executor_writable_images;
    c.on_frame = publish_frame;

    fprintf(stderr, "[Executor] %dx%d, %d MB, %s 68k core, data in %s, %zu disk image(s)\n",
            c.width, c.height, c.ram_mb, c.cpu.c_str(), c.data_dir.c_str(), c.disks.size());

    int rc = executor_host::run(c);
    fprintf(stderr, "[Executor] guest exited (%d)\n", rc);
    return rc;
}

int executor_direct_main(const config::EmulatorConfig& cfg)
{
    init_xpram(cfg);
    executor_host::Config c;
    c.width = cfg.screen_width;
    c.height = cfg.screen_height;
    c.ram_mb = (int)cfg.ram_mb;
    c.data_dir = cfg.executor_data_dir.empty() ? expand_home(cfg.storage_dir) + "/executor"
                                               : expand_home(cfg.executor_data_dir);
    c.disks = cfg.disk_paths;
    c.shared_folders = cfg.extfs_paths;
    c.cpu = cfg.cpu_core;
    return executor_host::run(c);
}
