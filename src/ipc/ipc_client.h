/*
 * ipc_client.h - Parent-side IPC connection to child emulator
 *
 * Connects to the child's SHM and Unix socket, receives eventfd,
 * sends input events. Based on legacy/web-streaming/server/ipc/ipc_connection.h.
 */

#ifndef IPC_CLIENT_H
#define IPC_CLIENT_H

#include "ipc_protocol.h"
#include <memory>
#include <string>
#include <shared_mutex>
#include <sys/types.h>

class QSharedMemory;

// Global reader-writer lock protecting the parent-side lifetime of any
// IPC SHM mapping. Parent-side threads that dereference IPCBuffer* (api
// handlers, video encoder, audio reader, webrtc metadata, etc.) MUST hold
// a shared lock for the full duration of each dereference. IPCClient::
// disconnect() takes an exclusive lock before munmap so no reader can
// observe an unmapped page. Readers should hold the lock briefly — on
// the order of one frame at most — to avoid stalling stop()/restart().
extern std::shared_mutex g_ipc_shm_mutex;

class IPCClient {
public:
    IPCClient();
    ~IPCClient();

    // Connection
    bool connect(pid_t pid);
    void disconnect();
    bool is_connected() const { return connected_; }

    // Resource access
    IPCBuffer* shm() { return shm_; }
    const IPCBuffer* shm() const { return shm_; }
    int frame_eventfd() const { return frame_eventfd_; }

    // Input sending
    bool send_key(int mac_keycode, bool down);
    bool send_mouse(int x, int y, uint8_t buttons, bool absolute);
    bool send_command(uint8_t command);
    bool send_audio_request(uint32_t requested_samples);

    // Socket access (for audio thread)
    int control_socket() const { return control_socket_; }

private:
    bool connect_shm(pid_t pid);
    void disconnect_shm();
    bool connect_socket(pid_t pid);
    void disconnect_socket();

    pid_t pid_ = -1;
    bool connected_ = false;

    IPCBuffer* shm_ = nullptr;
    std::unique_ptr<QSharedMemory> shm_owner_;
    int control_socket_ = -1;
    int frame_eventfd_ = -1;

    std::string shm_name_;
    std::string socket_path_;

    IPCClient(const IPCClient&) = delete;
    IPCClient& operator=(const IPCClient&) = delete;
};

#endif // IPC_CLIENT_H
