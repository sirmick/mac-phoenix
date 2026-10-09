/*
 * emulator_subprocess.h - Emulator subprocess management for webserver mode
 *
 * Subprocess management (m68k and PPC). Parent execs `mac-phoenix --ipc` as a
 * child process, connects via SHM+socket. Video frames are read
 * directly from IPC SHM by the encoder thread (zero-copy).
 */

#ifndef EMULATOR_SUBPROCESS_H
#define EMULATOR_SUBPROCESS_H

#include "../config/emulator_config.h"
#include "../ipc/ipc_client.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <vector>

class QProcess;
struct IPCBuffer;

class EmulatorSubprocess {
public:
    explicit EmulatorSubprocess(config::EmulatorConfig* config);
    ~EmulatorSubprocess();

    // Lifecycle
    bool start();
    bool stop();
    bool reset();

    // State
    bool is_running() const;

    // How the child last died on its own (crash or nonzero exit), with
    // the tail of its stdout/stderr. Cleared by start(); a stop() through
    // the API is not recorded.
    struct ChildExit {
        uint64_t id = 0;        // changes with every exit
        bool crashed = false;   // killed by a signal
        int code = 0;           // exit code, or the signal number
        std::vector<std::string> log;
    };
    bool last_exit(ChildExit& out) const;

    // IPC access (for API handlers)
    IPCClient* ipc_client() { return &ipc_client_; }
    const IPCClient* ipc_client() const { return &ipc_client_; }

    // Zero-copy video: encoder reads directly from IPC SHM
    void set_ipc_shm_atoms(std::atomic<IPCBuffer*>* shm, std::atomic<int>* notify_fd);

private:
    config::EmulatorConfig* config_;

    // QProcess owns child lifecycle. PID is cached separately because
    // IPCClient::connect() and the SHM key /macemu-video-{PID} need it.
    std::unique_ptr<QProcess> child_process_;
    pid_t child_pid_ = -1;
    std::mutex reap_mutex_;  // is_running() is called from many HTTP threads

    IPCClient ipc_client_;

    // The child's stdout/stderr come through a pipe: a reader thread
    // copies them to our stderr and keeps the last lines (LogTail).
    struct LogTail;
    std::shared_ptr<LogTail> log_tail_;  // guarded by exit_mutex_
    mutable std::mutex exit_mutex_;
    ChildExit exit_;
    bool have_exit_ = false;
    void record_exit(bool crashed, int code);

    // Atomic pointers set by encoder thread for zero-copy IPC reads
    std::atomic<IPCBuffer*>* ipc_shm_atom_ = nullptr;
    std::atomic<int>* ipc_notify_fd_atom_ = nullptr;

    void publish_ipc_shm();
    void clear_ipc_shm();
    // Lazily detect that the child has died (no monitor thread); called
    // from is_running()/stop() to keep ipc_shm_atom_ honest.
    void reap_if_dead();

    // Build argv for child process
    std::vector<std::string> build_child_args();

    EmulatorSubprocess(const EmulatorSubprocess&) = delete;
    EmulatorSubprocess& operator=(const EmulatorSubprocess&) = delete;
};

#endif // EMULATOR_SUBPROCESS_H
