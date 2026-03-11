/*
 * cpu_process.h - Fork-based CPU process management
 *
 * Manages the CPU as a child process. Start = fork(), Stop = kill(SIGKILL).
 * The child initializes the CPU and runs it. The parent manages video relay,
 * input forwarding, and status monitoring via shared memory.
 */

#ifndef CPU_PROCESS_H
#define CPU_PROCESS_H

#include "shared_state.h"
#include "../config/emulator_config.h"
#include <thread>
#include <atomic>
#include <sys/types.h>

class VideoOutput;  // Forward declaration

class CPUProcess {
public:
    CPUProcess(SharedState* shm, config::EmulatorConfig* config);
    ~CPUProcess();

    // Lifecycle
    bool start();   // Serialize config, fork child. Returns true on success.
    bool stop();    // Kill child, waitpid. Returns true on success.
    bool reset();   // stop() + start()

    // State
    bool is_running() const;

    // Video relay: parent thread that copies frames from SharedState to VideoOutput
    void set_video_output(VideoOutput* vo);
    void start_relay();
    void stop_relay();

private:
    SharedState* shm_;
    config::EmulatorConfig* config_;
    pid_t child_pid_ = -1;
    VideoOutput* video_output_ = nullptr;

    // Video relay thread
    std::thread relay_thread_;
    std::atomic<bool> relay_running_{false};

    // Child monitor thread (waitpid)
    std::thread monitor_thread_;

    void video_relay_main();
    static void child_main(SharedState* shm);

    // Non-copyable
    CPUProcess(const CPUProcess&) = delete;
    CPUProcess& operator=(const CPUProcess&) = delete;
};

#endif // CPU_PROCESS_H
