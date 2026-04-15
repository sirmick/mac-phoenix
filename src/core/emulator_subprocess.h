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
#include <thread>
#include <atomic>
#include <sys/types.h>

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

    // IPC access (for API handlers)
    IPCClient* ipc_client() { return &ipc_client_; }
    const IPCClient* ipc_client() const { return &ipc_client_; }

    // Zero-copy video: encoder reads directly from IPC SHM
    void set_ipc_shm_atoms(std::atomic<IPCBuffer*>* shm, std::atomic<int>* eventfd);

private:
    config::EmulatorConfig* config_;
    pid_t child_pid_ = -1;

    IPCClient ipc_client_;

    // Atomic pointers set by encoder thread for zero-copy IPC reads
    std::atomic<IPCBuffer*>* ipc_shm_atom_ = nullptr;
    std::atomic<int>* ipc_eventfd_atom_ = nullptr;

    // Monitor thread (waitpid)
    std::thread monitor_thread_;

    void publish_ipc_shm();
    void clear_ipc_shm();

    // Build argv for child process
    std::vector<std::string> build_child_args();

    EmulatorSubprocess(const EmulatorSubprocess&) = delete;
    EmulatorSubprocess& operator=(const EmulatorSubprocess&) = delete;
};

#endif // EMULATOR_SUBPROCESS_H
