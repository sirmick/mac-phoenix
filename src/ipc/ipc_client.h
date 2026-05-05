/*
 * ipc_client.h - Parent-side IPC connection to child emulator
 *
 * Phase 3b: AF_UNIX + eventfd + SCM_RIGHTS replaced with
 * QSharedMemory (3a, already in) + two QLocalSockets (control + notify).
 * Same wire semantics, but cross-platform (Unix sockets on Linux,
 * named pipes on Windows).
 */

#ifndef IPC_CLIENT_H
#define IPC_CLIENT_H

#include "ipc_protocol.h"
#include <memory>
#include <string>
#include <shared_mutex>
#include <sys/types.h>

class QSharedMemory;
class QLocalSocket;

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

    // Notify socket file descriptor — encoder thread polls this for
    // frame-ready wakeups. Replaces the eventfd from the legacy
    // SCM_RIGHTS handshake. Returns -1 if not connected.
    int frame_notify_fd() const { return notify_fd_; }

    // Input sending
    bool send_key(int mac_keycode, bool down);
    bool send_mouse(int x, int y, uint8_t buttons, bool absolute);
    bool send_command(uint8_t command);
    bool send_audio_request(uint32_t requested_samples);

    // Control socket fd — exposed for the audio thread which still
    // does direct ::send() for IPC_INPUT_AUDIO_REQUEST. -1 if not
    // connected. (Long-term, audio should also go through send_*
    // methods on this class.)
    int control_socket() const { return control_fd_; }

private:
    bool connect_shm(pid_t pid);
    void disconnect_shm();
    bool connect_control_socket(pid_t pid);
    bool connect_notify_socket(pid_t pid);
    void disconnect_sockets();

    pid_t pid_ = -1;
    bool connected_ = false;

    IPCBuffer* shm_ = nullptr;
    std::unique_ptr<QSharedMemory> shm_owner_;
    std::unique_ptr<QLocalSocket>  control_socket_;
    std::unique_ptr<QLocalSocket>  notify_socket_;
    // Cached raw fds (QLocalSocket::socketDescriptor()) — the encoder
    // thread polls notify_fd_; the audio thread sends on control_fd_.
    // Captured once at connect; we don't close these (QLocalSocket owns).
    int control_fd_ = -1;
    int notify_fd_  = -1;

    std::string shm_name_;
    std::string control_name_;
    std::string notify_name_;

    IPCClient(const IPCClient&) = delete;
    IPCClient& operator=(const IPCClient&) = delete;
};

#endif // IPC_CLIENT_H
