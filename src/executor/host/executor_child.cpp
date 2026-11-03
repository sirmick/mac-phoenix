/*
 * executor_child.cpp - the IPC child for `--backend executor`.
 *
 * Same contract as the ROM backends (see src/ipc/ipc_protocol.h): frames go
 * into the SHM triple buffer, input arrives on the control socket, status
 * fields drive /api/status and /api/app. The difference is what runs: the
 * Executor Toolbox on MAME's Musashi, with no ROM.
 */
#include "executor_child.h"
#include <syn68k_public.h>
#include "executor_host.h"
#include "platform.h"

#include "emulator_config.h"
#include "ipc_protocol.h"
#include "guest_bridge.h"
#include "../../drivers/audio/audio_direct.h"

#include <csignal>
#include <execinfo.h>
#include <cstdio>

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
#include <thread>

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

void finish_frame();

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
    g_buf->scale_milli = 0;
    finish_frame();
}

/* At a display scale (--executor-scale): the presentation surface, and the
   1x frame behind it in the slot (/api/screenshot?scale=1). */
void publish_scaled_frame(const uint32_t *scaled, int sw, int sh, const uint32_t *base, int w, int h)
{
    if (!g_buf)
        return;
    /* (the parent's frames are sized for the largest PPC mode then) */
    if ((size_t)sw * sh * 4 + (size_t)w * h * 4 > IPC_FRAME_SIZE || sw > mp::video::kMaxWidth_Ppc
        || sh > mp::video::kMaxHeight_Ppc) {
        publish_frame(base, w, h);
        return;
    }
    uint32_t idx = IPC_ATOMIC_LOAD(g_buf->write_index);
    uint8_t *dst = g_buf->frames[idx];
    memcpy(dst, scaled, (size_t)sw * sh * 4);
    memcpy(dst + (size_t)sw * sh * 4, base, (size_t)w * h * 4);
    g_buf->width = sw;
    g_buf->height = sh;
    g_buf->base_width = w;
    g_buf->base_height = h;
    g_buf->scale_milli = (uint32_t)((double)sw / w * 1000 + 0.5);
    finish_frame();
}

void finish_frame()
{
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
             sig, (unsigned)g_platform.cpu_get_pc(), info->si_addr);
    fprintf(stderr, "[Executor] %s: snapshotting\n", message);
    // The host frames first: a second fault while snapshotting would lose them.
    void *frames[32];
    int n = backtrace(frames, 32);
    fprintf(stderr, "[Executor] host frames (addr2line -f -C -e mac-phoenix):\n");
    backtrace_symbols_fd(frames, n, 2);
    syn68k_describe_cpu(stderr);
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
    guest_bridge::set_slot(&buf->bridge);      /* the bridge trap's requests */
    guest_bridge::reset();                     /* no agent yet in this run */
    buf->boot_start_us = monotonic_us();
    init_xpram(cfg);
    set_phase("boot globs");

    control_ipc_set_input_hooks(&g_hooks);
    control_ipc_start();
    buf->state = IPC_STATE_RUNNING;

    // /api/executor/ppc-break: answered here, off the guest's threads (the
    // breakpoints take their own lock), while the guest may be busy.
    if (!cfg.executor_ppc_break.empty())
        std::thread([buf] {
            for (;;) {
                uint32_t want = IPC_ATOMIC_LOAD(buf->debug_request);
                if (want != IPC_ATOMIC_LOAD(buf->debug_done)) {
                    char command[sizeof buf->debug_command];
                    memcpy(command, buf->debug_command, sizeof command);
                    command[sizeof command - 1] = '\0';
                    std::string j = executor_host::ppc_break_command(command);
                    size_t n = std::min(j.size(), sizeof buf->debug_json - 1);
                    memcpy(buf->debug_json, j.data(), n);
                    buf->debug_json[n] = '\0';
                    IPC_ATOMIC_STORE(buf->debug_done, want);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }).detach();

    // POST /api/snapshot: dump guest RAM (guest address 0 up) when the
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
    c.bpp = cfg.screen_depth_set ? (int)cfg.screen_depth : 0;
    c.ram_mb = (int)cfg.ram_mb;
    c.data_dir = cfg.executor_data_dir.empty() ? expand_home(cfg.storage_dir) + "/executor"
                                               : expand_home(cfg.executor_data_dir);
    c.unimplemented_log = expand_home(cfg.storage_dir) + "/executor/unimplemented.log";
    c.disks = cfg.disk_paths;
    c.cdroms = cfg.cdrom_paths;
    c.shared_folders = cfg.extfs_paths;
    c.cpu = cfg.cpu_core;
    c.app = expand_home(cfg.executor_app);
    c.logtraps = cfg.executor_logtraps;
    c.logtraps_nesting = cfg.executor_logtraps_nesting;
    c.logtraps_filter = cfg.executor_logtraps_filter;
    c.debug_options = cfg.executor_debug;
    c.watch = cfg.watch;
    c.ppc_breaks = cfg.executor_ppc_break;
    c.writable_images = cfg.executor_writable_images;
    c.open_transport = cfg.executor_ot;
    c.prefer_ppc = cfg.executor_ppc;
    c.on_frame = publish_frame;
    c.scale = cfg.executor_scale;
    c.on_frame_scaled = publish_scaled_frame;
#if EXECUTOR_HIDPI
    /* known before the first frame: the parent sends it with the cursor */
    if (c.scale > 1.0)
        buf->scale_milli = (uint32_t)(c.scale * 1000 + 0.5);
#endif
    // The cursor is the page's to draw (client/cursor.js): never in the
    // frames; its image goes to the parent through the IPC buffer.
    c.on_cursor = [](const uint8_t data[32], const uint8_t mask[32], int hot_x, int hot_y,
                     bool visible) {
        memcpy(g_buf->cursor_data, data, 32);
        memcpy(g_buf->cursor_mask, mask, 32);
        g_buf->cursor_hot_x = (int16_t)hot_x;
        g_buf->cursor_hot_y = (int16_t)hot_y;
        g_buf->cursor_visible = visible ? 1 : 0;
        IPC_ATOMIC_STORE(g_buf->cursor_seq, IPC_ATOMIC_LOAD(g_buf->cursor_seq) + 1);
    };
    if (cfg.audio_enabled) {
        /* The parent pulls 20 ms frames from the IPC audio ring (--audio). */
        audio_direct_set_ipc_buffer(buf);
        c.on_audio = [](const uint8_t *s16be, uint32_t samples) {
            return audio_direct_push_frame(s16be, samples, 48000, 2);
        };
    }

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
    c.unimplemented_log = expand_home(cfg.storage_dir) + "/executor/unimplemented.log";
    c.disks = cfg.disk_paths;
    c.cdroms = cfg.cdrom_paths;
    c.shared_folders = cfg.extfs_paths;
    c.cpu = cfg.cpu_core;
    c.app = expand_home(cfg.executor_app);
    c.logtraps = cfg.executor_logtraps;
    c.logtraps_nesting = cfg.executor_logtraps_nesting;
    c.logtraps_filter = cfg.executor_logtraps_filter;
    c.debug_options = cfg.executor_debug;
    c.watch = cfg.watch;
    c.ppc_breaks = cfg.executor_ppc_break;
    c.writable_images = cfg.executor_writable_images;
    c.open_transport = cfg.executor_ot;
    c.prefer_ppc = cfg.executor_ppc;
    return executor_host::run(c);
}
