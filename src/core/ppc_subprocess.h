/*
 * ppc_subprocess.h - PPC subprocess management for webserver mode
 *
 * PPC subprocess management. Parent execs `mac-phoenix --ipc` as a
 * child process, connects via SHM+socket, relays video to VideoOutput.
 */

#ifndef PPC_SUBPROCESS_H
#define PPC_SUBPROCESS_H

#include "../config/emulator_config.h"
#include "../ipc/ipc_client.h"
#include <thread>
#include <atomic>
#include <sys/types.h>

class VideoOutput;

class PPCSubprocess {
public:
    explicit PPCSubprocess(config::EmulatorConfig* config);
    ~PPCSubprocess();

    // Lifecycle
    bool start();
    bool stop();
    bool reset();

    // State
    bool is_running() const;

    // IPC access (for API handlers)
    IPCClient* ipc_client() { return &ipc_client_; }
    const IPCClient* ipc_client() const { return &ipc_client_; }

    // Video relay
    void set_video_output(VideoOutput* vo);

private:
    config::EmulatorConfig* config_;
    pid_t child_pid_ = -1;
    VideoOutput* video_output_ = nullptr;

    IPCClient ipc_client_;

    // Video relay thread (epoll on eventfd)
    std::thread relay_thread_;
    std::atomic<bool> relay_running_{false};

    // Monitor thread (waitpid)
    std::thread monitor_thread_;

    void video_relay_main();
    void start_relay();
    void stop_relay();

    // Build argv for child process
    std::vector<std::string> build_child_args();

    PPCSubprocess(const PPCSubprocess&) = delete;
    PPCSubprocess& operator=(const PPCSubprocess&) = delete;
};

#endif // PPC_SUBPROCESS_H
