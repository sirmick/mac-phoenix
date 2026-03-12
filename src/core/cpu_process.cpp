/*
 * cpu_process.cpp - Fork-based CPU process management
 *
 * The CPU runs as a child process, forked from the parent server process.
 * On Stop, the child is killed with SIGKILL — OS reclaims all memory,
 * file handles, and global state. No manual cleanup needed.
 */

#include "cpu_process.h"
#include "cpu_context.h"
#include "emulator_init.h"
#include "boot_progress.h"
#include "shared_state.h"

#include "../common/include/sysdeps.h"
#include "../common/include/cpu_emulation.h"
#include "../common/include/video.h"
#include "../common/include/video_defs.h"
#include "../common/include/platform.h"
#include "../common/include/main.h"
#include "../common/include/adb.h"
#include "../drivers/video/video_output.h"
#include "../config/emulator_config.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <nlohmann/json.hpp>

// External globals needed by the child
extern uint8_t *RAMBaseHost;
extern uint8_t *ROMBaseHost;
extern uint32_t RAMSize;
extern uint32_t ROMSize;
extern Platform g_platform;
extern bool g_emulator_initialized;
extern CPUContext g_cpu_ctx;  // defined in main.cpp (static but we use extern for child)

// CPU backend install functions
extern "C" {
void cpu_uae_install(Platform* platform);
void cpu_unicorn_install(Platform* platform);
void cpu_dualcpu_install(Platform* platform);
}

// Forward declarations for ADB shared input polling
extern void ADBMouseMoved(int x, int y);
extern void ADBMouseDown(int button);
extern void ADBMouseUp(int button);
extern void ADBSetRelMouseMode(bool relative);
extern void ADBKeyDown(int code);
extern void ADBKeyUp(int code);

// ========================================================================
// Child-side SHM video driver
// ========================================================================

static SharedState* g_child_shm = nullptr;
static uint8_t* shm_the_buffer = nullptr;
static int shm_video_width = 1024;
static int shm_video_height = 768;

// SHM monitor descriptor — supports runtime resolution switching via Mac Monitors control panel
class shm_monitor_desc : public monitor_desc {
public:
    shm_monitor_desc(const vector<video_mode> &modes, video_depth depth, uint32 id)
        : monitor_desc(modes, depth, id) {}
    ~shm_monitor_desc() {}
    void switch_to_current_mode(void) {
        const video_mode &mode = get_current_mode();
        shm_video_width = mode.x;
        shm_video_height = mode.y;
        if (shm_the_buffer) {
            memset(shm_the_buffer, 0, mode.x * mode.y * 4);
        }
        fprintf(stderr, "[SHM Video] Mode switch to %dx%dx32\n", mode.x, mode.y);
    }
    void set_palette(uint8 *pal, int num) { (void)pal; (void)num; }
    void set_gamma(uint8 *gamma, int num) { (void)gamma; (void)num; }
};

static bool video_shm_init(bool classic)
{
    (void)classic;

    // Supported resolutions (resolution_id follows legacy BasiliskII convention)
    struct { int w, h; uint32 res_id; } supported_modes[] = {
        {  640,  480, 0x81 },
        {  800,  600, 0x82 },
        { 1024,  768, 0x83 },
        { 1280, 1024, 0x85 },
        { 1600, 1200, 0x86 },
        { 1920, 1080, 0x87 },
    };

    // Default resolution from config
    config::EmulatorConfig& cfg = config::EmulatorConfig::instance();
    const int default_width = cfg.screen_width;
    const int default_height = cfg.screen_height;

    // Store initial dimensions for refresh function
    shm_video_width = default_width;
    shm_video_height = default_height;

    // Place framebuffer after ScratchMem (8MB area, same layout as video_webrtc)
    shm_the_buffer = ROMBaseHost + ROMSize + 0x10000;
    memset(shm_the_buffer, 0, 0x800000);

    // Build video modes (32-bit only)
    vector<video_mode> modes;
    uint32 default_res_id = 0x83;

    for (const auto& sm : supported_modes) {
        if ((uint32_t)sm.w * sm.h * 4 > 0x800000) continue;

        // --screen limits maximum resolution (prevents Mac from mode-switching up)
        if (sm.w > default_width || sm.h > default_height) continue;

        video_mode mode;
        mode.x = sm.w;
        mode.y = sm.h;
        mode.resolution_id = sm.res_id;
        mode.depth = VDEPTH_32BIT;
        mode.bytes_per_row = sm.w * 4;
        mode.user_data = 0;
        modes.push_back(mode);

        if (sm.w == default_width && sm.h == default_height) {
            default_res_id = sm.res_id;
        }
    }

    // Create monitor descriptor
    shm_monitor_desc *monitor = new shm_monitor_desc(modes, VDEPTH_32BIT, default_res_id);

    // Set Mac frame buffer address
    uint32 mac_fb_addr = Host2MacAddr(shm_the_buffer);
    if (mac_fb_addr == 0) {
        fprintf(stderr, "[SHM Video] Host2MacAddr returned 0\n");
        delete monitor;
        return false;
    }
    monitor->set_mac_frame_base(mac_fb_addr);
    VideoMonitors.push_back(monitor);

    fprintf(stderr, "[SHM Video] Initialized %dx%dx32 (%zu modes), fb at Mac 0x%08x\n",
            default_width, default_height, modes.size(), mac_fb_addr);
    return true;
}

static void video_shm_exit(void)
{
    // Child process will die — OS cleans up everything
    shm_the_buffer = nullptr;
}

static void video_shm_refresh(void)
{
    if (!g_child_shm || !shm_the_buffer) return;

    const int width = shm_video_width;
    const int height = shm_video_height;
    const size_t frame_bytes = width * height * 4;

    // Triple buffer protocol: write to write_index, publish via ready_index
    int idx = g_child_shm->video_write_index.load(std::memory_order_acquire);
    auto& vbuf = g_child_shm->video_buffers[idx];

    memcpy(vbuf.pixels, shm_the_buffer, frame_bytes);
    vbuf.width = width;
    vbuf.height = height;
    vbuf.pixel_format = 0;  // PIXFMT_ARGB

    // Timestamp
    auto now = std::chrono::system_clock::now();
    vbuf.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();

    // Sequence number
    uint64_t seq = g_child_shm->video_frame_count.fetch_add(1, std::memory_order_relaxed) + 1;
    vbuf.sequence = seq;

    // Advance write index, publish ready index
    int next = (idx + 1) % SHM_VIDEO_NUM_BUFFERS;
    g_child_shm->video_write_index.store(next, std::memory_order_release);
    g_child_shm->video_ready_index.store(idx, std::memory_order_release);

    // Update cursor state in shared memory (read Mac low-memory globals)
    MacCursorState cs;
    boot_progress_get_cursor_state(&cs);
    g_child_shm->cursor_x.store(cs.cursor_x, std::memory_order_relaxed);
    g_child_shm->cursor_y.store(cs.cursor_y, std::memory_order_relaxed);
    g_child_shm->raw_x.store(cs.raw_x, std::memory_order_relaxed);
    g_child_shm->raw_y.store(cs.raw_y, std::memory_order_relaxed);
    g_child_shm->mtemp_x.store(cs.mtemp_x, std::memory_order_relaxed);
    g_child_shm->mtemp_y.store(cs.mtemp_y, std::memory_order_relaxed);
    g_child_shm->crsr_new.store(cs.crsr_new, std::memory_order_relaxed);
    g_child_shm->crsr_couple.store(cs.crsr_couple, std::memory_order_relaxed);
    g_child_shm->crsr_busy.store(cs.crsr_busy, std::memory_order_relaxed);
}

// ========================================================================
// Child-side input polling (called from timer interrupt at 60Hz)
// ========================================================================

// Global pointer set in child process
SharedState* g_child_shared_state = nullptr;

extern "C" void ADBPollSharedInput(void)
{
    SharedState* shm = g_child_shared_state;
    if (!shm) return;

    int32_t read_pos = shm->input_read_pos.load(std::memory_order_relaxed);
    int32_t write_pos = shm->input_write_pos.load(std::memory_order_acquire);

    while (read_pos != write_pos) {
        auto& ev = shm->input_queue[read_pos & 0xFF];

        switch (ev.type) {
            case SHM_INPUT_MOUSE_REL:
                ADBSetRelMouseMode(true);
                ADBMouseMoved(ev.x, ev.y);
                break;
            case SHM_INPUT_MOUSE_BUTTON:
                if (ev.flags)
                    ADBMouseDown(ev.keycode);
                else
                    ADBMouseUp(ev.keycode);
                break;
            case SHM_INPUT_KEY:
                if (ev.flags)
                    ADBKeyDown(ev.keycode);
                else
                    ADBKeyUp(ev.keycode);
                break;
            case SHM_INPUT_MOUSE_ABS:
                ADBSetRelMouseMode(false);
                ADBMouseMoved(ev.x, ev.y);
                break;
            case SHM_INPUT_MOUSE_MODE:
                ADBSetRelMouseMode(ev.flags == 1);
                break;
        }

        read_pos++;
    }

    shm->input_read_pos.store(read_pos, std::memory_order_release);
}

// ========================================================================
// Child-side boot progress writing
// ========================================================================

// Called from boot_progress.cpp when g_boot_shm is set
extern void boot_progress_set_shared_state(SharedState* shm);

// ========================================================================
// Child main function (runs after fork in child process)
// ========================================================================

void CPUProcess::child_main(SharedState* shm)
{
    fprintf(stderr, "[Child] CPU process started (pid %d)\n", getpid());

    // Reset signal handlers inherited from parent
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_IGN);  // Ignore SIGINT in child; parent kills us with SIGKILL
    signal(SIGHUP, SIG_DFL);

    // Set child shared state pointers
    g_child_shm = shm;
    g_child_shared_state = shm;

    // Read config from shared memory
    std::string config_str(shm->config_json, shm->config_json_len);
    config::EmulatorConfig& cfg = config::EmulatorConfig::instance();
    try {
        nlohmann::json j = nlohmann::json::parse(config_str);
        cfg.merge_json(j);
    } catch (const std::exception& e) {
        snprintf(shm->error_msg, sizeof(shm->error_msg),
                 "Failed to parse config: %s", e.what());
        shm->child_state.store(SHM_STATE_ERROR, std::memory_order_release);
        _exit(1);
    }

    // Apply debug flags from config
    extern bool g_debug_mode_switch;
    g_debug_mode_switch = cfg.debug_mode_switch;
    extern bool g_debug_network;
    g_debug_network = cfg.debug_network;

    // Set boot progress to write to shared memory
    boot_progress_set_shared_state(shm);

    // Install SHM video driver
    g_platform.video_init = video_shm_init;
    g_platform.video_exit = video_shm_exit;
    g_platform.video_refresh = video_shm_refresh;

    // Null audio driver (audio not supported in fork mode yet)
    g_platform.audio_init = []() {};
    g_platform.audio_exit = []() {};

    // Install CPU backend
    Platform* platform = g_cpu_ctx.get_platform();
    *platform = g_platform;

    switch (cfg.cpu_backend) {
        case config::CPUBackend::Unicorn:
            cpu_unicorn_install(platform);
            break;
        case config::CPUBackend::DualCPU:
            cpu_dualcpu_install(platform);
            break;
        case config::CPUBackend::UAE:
        default:
            cpu_uae_install(platform);
            break;
    }

    // Copy platform to global
    g_platform = *platform;

    // Set RAM size from config
    RAMSize = cfg.ram_mb * 1024 * 1024;

    // Initialize CPU (loads ROM, patches ROM, inits Mac subsystems)
    if (!g_cpu_ctx.init_m68k(cfg)) {
        snprintf(shm->error_msg, sizeof(shm->error_msg),
                 "Failed to initialize CPU");
        shm->child_state.store(SHM_STATE_ERROR, std::memory_order_release);
        fprintf(stderr, "[Child] init_m68k failed\n");
        _exit(1);
    }

    g_emulator_initialized = true;

    // Mark as running
    shm->child_state.store(SHM_STATE_RUNNING, std::memory_order_release);
    fprintf(stderr, "[Child] CPU initialized, starting execution\n");

    // Run CPU (blocks until process is killed)
    if (g_platform.cpu_execute_fast) {
        g_platform.cpu_execute_fast();
    } else {
        while (true) {
            g_platform.cpu_execute_one();
        }
    }

    // Normal exit (shouldn't normally reach here)
    fprintf(stderr, "[Child] CPU execution ended\n");
    _exit(0);
}

// ========================================================================
// CPUProcess implementation (parent side)
// ========================================================================

CPUProcess::CPUProcess(SharedState* shm, config::EmulatorConfig* config)
    : shm_(shm), config_(config)
{
}

CPUProcess::~CPUProcess()
{
    stop_relay();
    stop();
}

bool CPUProcess::start()
{
    if (child_pid_ > 0) {
        fprintf(stderr, "[CPUProcess] Already running (pid %d)\n", child_pid_);
        return false;
    }

    // Serialize config to shared memory
    std::string json_str = config_->to_json().dump();
    if ((int)json_str.size() >= (int)sizeof(shm_->config_json)) {
        fprintf(stderr, "[CPUProcess] Config JSON too large (%zu bytes)\n", json_str.size());
        return false;
    }
    memcpy(shm_->config_json, json_str.c_str(), json_str.size());
    shm_->config_json_len = json_str.size();

    // Reset runtime state
    shm_->reset_runtime();

    // Fork
    pid_t pid = fork();
    if (pid < 0) {
        perror("[CPUProcess] fork failed");
        shm_->child_state.store(SHM_STATE_ERROR, std::memory_order_release);
        snprintf(shm_->error_msg, sizeof(shm_->error_msg), "fork() failed: %s", strerror(errno));
        return false;
    }

    if (pid == 0) {
        // ── Child process ──
        child_main(shm_);
        _exit(0);  // Should not reach here
    }

    // ── Parent process ──
    child_pid_ = pid;
    fprintf(stderr, "[CPUProcess] Forked child process (pid %d)\n", child_pid_);

    // Start monitor thread (waits for child to exit)
    if (monitor_thread_.joinable()) {
        monitor_thread_.detach();
    }
    monitor_thread_ = std::thread([this]() {
        int status;
        pid_t result = waitpid(child_pid_, &status, 0);
        if (result > 0) {
            if (WIFSIGNALED(status)) {
                int sig = WTERMSIG(status);
                if (sig != SIGKILL) {
                    // Unexpected signal (crash)
                    fprintf(stderr, "[CPUProcess] Child killed by signal %d\n", sig);
                    shm_->child_state.store(SHM_STATE_ERROR, std::memory_order_release);
                    snprintf(shm_->error_msg, sizeof(shm_->error_msg),
                             "CPU process crashed (signal %d)", sig);
                } else {
                    // Expected kill (Stop was called)
                    shm_->child_state.store(SHM_STATE_STOPPED, std::memory_order_release);
                }
            } else if (WIFEXITED(status)) {
                int code = WEXITSTATUS(status);
                if (code != 0) {
                    fprintf(stderr, "[CPUProcess] Child exited with code %d\n", code);
                    if (shm_->child_state.load() != SHM_STATE_ERROR) {
                        shm_->child_state.store(SHM_STATE_ERROR, std::memory_order_release);
                    }
                } else {
                    shm_->child_state.store(SHM_STATE_STOPPED, std::memory_order_release);
                }
            }
            child_pid_ = -1;
        }
    });

    // Start video relay
    start_relay();

    return true;
}

bool CPUProcess::stop()
{
    if (child_pid_ <= 0) {
        return true;  // Already stopped
    }

    fprintf(stderr, "[CPUProcess] Stopping child process (pid %d)\n", child_pid_);

    // Kill child
    kill(child_pid_, SIGKILL);

    // Wait for monitor thread to reap
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }

    // Stop video relay
    stop_relay();

    child_pid_ = -1;
    fprintf(stderr, "[CPUProcess] Child stopped\n");
    return true;
}

bool CPUProcess::reset()
{
    stop();
    return start();
}

bool CPUProcess::is_running() const
{
    return child_pid_ > 0 &&
           shm_->child_state.load(std::memory_order_acquire) == SHM_STATE_RUNNING;
}

void CPUProcess::set_video_output(VideoOutput* vo)
{
    video_output_ = vo;
}

void CPUProcess::start_relay()
{
    if (!video_output_ || relay_running_.load()) return;

    relay_running_.store(true, std::memory_order_release);
    relay_thread_ = std::thread(&CPUProcess::video_relay_main, this);
}

void CPUProcess::stop_relay()
{
    relay_running_.store(false, std::memory_order_release);
    if (relay_thread_.joinable()) {
        relay_thread_.join();
    }
}

void CPUProcess::video_relay_main()
{
    fprintf(stderr, "[VideoRelay] Started\n");
    uint64_t last_seq = 0;

    while (relay_running_.load(std::memory_order_acquire)) {
        // Check if child has written a new frame
        int idx = shm_->video_ready_index.load(std::memory_order_acquire);
        auto& vbuf = shm_->video_buffers[idx];

        if (vbuf.sequence > last_seq && vbuf.width > 0 && vbuf.height > 0) {
            // Copy frame to VideoOutput
            video_output_->submit_frame(
                reinterpret_cast<const uint32_t*>(vbuf.pixels),
                vbuf.width, vbuf.height,
                static_cast<PixelFormat>(vbuf.pixel_format));
            last_seq = vbuf.sequence;
        }

        // Poll at ~1ms intervals
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    fprintf(stderr, "[VideoRelay] Stopped\n");
}
