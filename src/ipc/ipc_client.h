/*
 * ipc_client.h - Parent-side IPC connection to child emulator
 *
 * Connects to the child's SHM and Unix socket, receives eventfd,
 * sends input events. Based on legacy/web-streaming/server/ipc/ipc_connection.h.
 */

#ifndef IPC_CLIENT_H
#define IPC_CLIENT_H

#include "ipc_protocol.h"
#include <string>
#include <sys/types.h>

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
    int shm_fd_ = -1;
    int control_socket_ = -1;
    int frame_eventfd_ = -1;

    std::string shm_name_;
    std::string socket_path_;

    IPCClient(const IPCClient&) = delete;
    IPCClient& operator=(const IPCClient&) = delete;
};

#endif // IPC_CLIENT_H
