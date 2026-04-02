/*
 * ppc_subprocess.cpp - PPC subprocess management for webserver mode
 *
 * Parent execs `mac-phoenix --ipc` as a child subprocess, connects
 * via SHM + Unix socket, and relays video frames to VideoOutput.
 *
 * Based on src/core/cpu_process.cpp (same interface, different transport).
 */

#include "ppc_subprocess.h"
#include "../drivers/video/video_output.h"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/epoll.h>

PPCSubprocess::PPCSubprocess(config::EmulatorConfig* config)
    : config_(config)
{
}

PPCSubprocess::~PPCSubprocess()
{
    stop_relay();
    stop();
}

std::vector<std::string> PPCSubprocess::build_child_args()
{
    // Get path to our own binary
    char exe_path[4096];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len <= 0) {
        fprintf(stderr, "[PPCSubprocess] Failed to read /proc/self/exe\n");
        return {};
    }
    exe_path[len] = '\0';

    std::vector<std::string> args;
    args.push_back(exe_path);
    args.push_back("--ipc");
    args.push_back("--arch");
    args.push_back(config_->architecture == config::Architecture::PPC ? "ppc" : "m68k");
    args.push_back("--no-webserver");

    // Pass CPU backend for all architectures
    args.push_back("--backend");
    switch (config_->cpu_backend) {
        case config::CPUBackend::Unicorn: args.push_back("unicorn"); break;
        case config::CPUBackend::DualCPU: args.push_back("dualcpu"); break;
        case config::CPUBackend::KPX:     args.push_back("kpx"); break;
        default: args.push_back("uae"); break;
    }

    if (!config_->rom_path.empty()) {
        args.push_back("--rom");
        args.push_back(config_->rom_path);
    }

    for (const auto& d : config_->disk_paths) {
        args.push_back("--disk");
        args.push_back(d);
    }
    for (const auto& c : config_->cdrom_paths) {
        args.push_back("--cdrom");
        args.push_back(c);
    }
    for (const auto& e : config_->extfs_paths) {
        args.push_back("--extfs");
        args.push_back(e);
    }

    args.push_back("--ram");
    args.push_back(std::to_string(config_->ram_mb));

    args.push_back("--screen");
    args.push_back(config_->screen_string());

    args.push_back("--log-level");
    args.push_back(std::to_string(config_->log_level));

    if (config_->dismiss_shutdown_dialog) {
        args.push_back("--dismiss-shutdown-dialog");
    }

    // PPC-specific options
    args.push_back(config_->ppc.jit ? "--ppc-jit" : "--no-ppc-jit");

    return args;
}

bool PPCSubprocess::start()
{
    if (child_pid_ > 0) {
        fprintf(stderr, "[PPCSubprocess] Already running (pid %d)\n", child_pid_);
        return false;
    }

    auto args = build_child_args();
    if (args.empty()) {
        return false;
    }

    fprintf(stderr, "[PPCSubprocess] Launching:");
    for (const auto& a : args) fprintf(stderr, " %s", a.c_str());
    fprintf(stderr, "\n");

    pid_t pid = fork();
    if (pid < 0) {
        perror("[PPCSubprocess] fork failed");
        return false;
    }

    if (pid == 0) {
        // Child: exec the new process
        std::vector<char*> argv;
        for (auto& a : args) {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);

        execv(argv[0], argv.data());
        // If exec fails
        perror("[PPCSubprocess] execv failed");
        _exit(1);
    }

    // Parent
    child_pid_ = pid;
    fprintf(stderr, "[PPCSubprocess] Child started (pid %d)\n", child_pid_);

    // Poll for SHM to appear (child creates it during init)
    bool connected = false;
    for (int attempt = 0; attempt < 100; attempt++) {  // 10 seconds
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Check if child is still alive
        int status;
        pid_t result = waitpid(child_pid_, &status, WNOHANG);
        if (result > 0) {
            fprintf(stderr, "[PPCSubprocess] Child exited before connection (status %d)\n",
                    WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            child_pid_ = -1;
            return false;
        }

        if (ipc_client_.connect(child_pid_)) {
            connected = true;
            break;
        }
    }

    if (!connected) {
        fprintf(stderr, "[PPCSubprocess] Failed to connect to child after 10s\n");
        kill(child_pid_, SIGKILL);
        waitpid(child_pid_, nullptr, 0);
        child_pid_ = -1;
        return false;
    }

    fprintf(stderr, "[PPCSubprocess] Connected to child IPC\n");

    // Start monitor thread
    if (monitor_thread_.joinable()) {
        monitor_thread_.detach();
    }
    monitor_thread_ = std::thread([this]() {
        int status;
        pid_t result = waitpid(child_pid_, &status, 0);
        if (result > 0) {
            if (WIFSIGNALED(status)) {
                int sig = WTERMSIG(status);
                if (sig != SIGKILL && sig != SIGTERM) {
                    fprintf(stderr, "[PPCSubprocess] Child killed by signal %d\n", sig);
                }
            } else if (WIFEXITED(status)) {
                int code = WEXITSTATUS(status);
                if (code != 0) {
                    fprintf(stderr, "[PPCSubprocess] Child exited with code %d\n", code);
                }
            }
            child_pid_ = -1;
        }
    });

    // Start video relay
    start_relay();

    return true;
}

bool PPCSubprocess::stop()
{
    pid_t pid = child_pid_;
    if (pid <= 0) {
        // Still need to join monitor thread if it's running
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
        return true;
    }

    fprintf(stderr, "[PPCSubprocess] Stopping child (pid %d)\n", pid);

    // Stop relay first
    stop_relay();

    // Disconnect IPC
    ipc_client_.disconnect();

    // Send SIGTERM, then SIGKILL after grace period
    kill(pid, SIGTERM);

    // Wait briefly for graceful exit
    for (int i = 0; i < 10; i++) {
        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result > 0) {
            child_pid_ = -1;
            if (monitor_thread_.joinable()) {
                monitor_thread_.join();
            }
            fprintf(stderr, "[PPCSubprocess] Child stopped gracefully\n");
            return true;
        }
        if (result < 0) {
            // Already reaped (by monitor thread)
            child_pid_ = -1;
            if (monitor_thread_.joinable()) {
                monitor_thread_.join();
            }
            fprintf(stderr, "[PPCSubprocess] Child already exited\n");
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Force kill — use saved pid, never kill(-1)
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    child_pid_ = -1;

    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }

    fprintf(stderr, "[PPCSubprocess] Child killed\n");
    return true;
}

bool PPCSubprocess::reset()
{
    stop();
    return start();
}

bool PPCSubprocess::is_running() const
{
    if (child_pid_ <= 0) return false;
    if (!ipc_client_.is_connected()) return false;
    const IPCBuffer* buf = ipc_client_.shm();
    return buf && buf->state == IPC_STATE_RUNNING;
}

void PPCSubprocess::set_video_output(VideoOutput* vo)
{
    video_output_ = vo;
}

void PPCSubprocess::start_relay()
{
    if (!video_output_ || relay_running_.load()) return;

    relay_running_.store(true, std::memory_order_release);
    relay_thread_ = std::thread(&PPCSubprocess::video_relay_main, this);
}

void PPCSubprocess::stop_relay()
{
    relay_running_.store(false, std::memory_order_release);
    if (relay_thread_.joinable()) {
        relay_thread_.join();
    }
}

void PPCSubprocess::video_relay_main()
{
    fprintf(stderr, "[PPCSubprocess] Video relay started\n");

    int eventfd = ipc_client_.frame_eventfd();
    bool use_epoll = (eventfd >= 0);

    int epoll_fd = -1;
    if (use_epoll) {
        epoll_fd = epoll_create1(0);
        if (epoll_fd >= 0) {
            struct epoll_event ev;
            ev.events = EPOLLIN;
            ev.data.fd = eventfd;
            epoll_ctl(epoll_fd, EPOLL_CTL_ADD, eventfd, &ev);
        } else {
            use_epoll = false;
        }
    }

    uint64_t last_frame_count = 0;

    while (relay_running_.load(std::memory_order_acquire)) {
        if (use_epoll) {
            // Wait for eventfd signal (frame ready)
            struct epoll_event events[1];
            int n = epoll_wait(epoll_fd, events, 1, 16);  // 16ms timeout (~60fps)
            if (n > 0) {
                // Drain eventfd
                uint64_t val;
                ssize_t ignored = read(eventfd, &val, sizeof(val));
                (void)ignored;
            }
        } else {
            // Fallback: poll at ~1ms
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        IPCBuffer* buf = ipc_client_.shm();
        if (!buf) continue;

        // Check for new frame (atomic reads for cross-process safety)
        uint64_t frame_count = IPC_ATOMIC_LOAD(buf->frame_count);
        if (frame_count <= last_frame_count) continue;
        last_frame_count = frame_count;

        uint32_t idx = IPC_ATOMIC_LOAD(buf->ready_index);
        if (idx >= IPC_NUM_BUFFERS) continue;

        const uint8_t* pixels = buf->frames[idx];
        int width = buf->width;
        int height = buf->height;

        if (width <= 0 || height <= 0) continue;

        // Child stores packed pixels (no stride padding) — direct submit
        video_output_->submit_frame(
            reinterpret_cast<const uint32_t*>(pixels),
            width, height,
            static_cast<PixelFormat>(buf->pixel_format));
    }

    if (epoll_fd >= 0) {
        close(epoll_fd);
    }

    fprintf(stderr, "[PPCSubprocess] Video relay stopped\n");
}
