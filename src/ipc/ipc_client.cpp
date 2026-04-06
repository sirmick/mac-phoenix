/*
 * ipc_client.cpp - Parent-side IPC connection to child emulator
 *
 * Based on legacy/web-streaming/server/ipc/ipc_connection.cpp.
 */

#include "ipc_client.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <mutex>
#include <shared_mutex>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>

std::shared_mutex g_ipc_shm_mutex;

IPCClient::IPCClient() = default;

IPCClient::~IPCClient()
{
    disconnect();
    // Clean up any zombie SHM
    if (zombie_shm_ && zombie_shm_ != MAP_FAILED) {
        munmap(zombie_shm_, sizeof(IPCBuffer));
        zombie_shm_ = nullptr;
    }
}

bool IPCClient::connect_shm(pid_t pid)
{
    shm_name_ = std::string(IPC_VIDEO_SHM_PREFIX) + std::to_string(pid);

    shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0);
    if (shm_fd_ < 0) {
        return false;
    }

    shm_ = (IPCBuffer*)mmap(nullptr, sizeof(IPCBuffer),
                             PROT_READ | PROT_WRITE, MAP_SHARED,
                             shm_fd_, 0);
    if (shm_ == MAP_FAILED) {
        fprintf(stderr, "IPC Client: Failed to map SHM for PID %d: %s\n",
                pid, strerror(errno));
        close(shm_fd_);
        shm_fd_ = -1;
        shm_ = nullptr;
        return false;
    }

    int result = ipc_validate_buffer(shm_, pid);
    if (result != 0) {
        fprintf(stderr, "IPC Client: SHM validation failed for PID %d (error %d)\n",
                pid, result);
        munmap(shm_, sizeof(IPCBuffer));
        close(shm_fd_);
        shm_fd_ = -1;
        shm_ = nullptr;
        return false;
    }

    fprintf(stderr, "IPC Client: Connected to SHM '%s' (%dx%d)\n",
            shm_name_.c_str(), shm_->width, shm_->height);
    return true;
}

void IPCClient::disconnect_shm()
{
    // Don't munmap immediately — keep it as a zombie so encoder threads
    // reading stale pointers hit valid (stale) memory instead of SIGSEGV.
    // The child's SHM object is unlinked, but mmap keeps the pages alive.
    if (shm_ && shm_ != MAP_FAILED) {
        // Clean up any previous zombie first
        if (zombie_shm_ && zombie_shm_ != MAP_FAILED) {
            munmap(zombie_shm_, sizeof(IPCBuffer));
        }
        zombie_shm_ = shm_;
        shm_ = nullptr;
    }
    if (shm_fd_ >= 0) {
        close(shm_fd_);
        shm_fd_ = -1;
    }
    shm_name_.clear();
}

bool IPCClient::connect_socket(pid_t pid)
{
    socket_path_ = std::string(IPC_CONTROL_SOCK_PREFIX) + std::to_string(pid) +
                   std::string(IPC_CONTROL_SOCK_SUFFIX);

    control_socket_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (control_socket_ < 0) {
        fprintf(stderr, "IPC Client: Failed to create socket: %s\n", strerror(errno));
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(control_socket_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(control_socket_);
        control_socket_ = -1;
        return false;
    }

    fprintf(stderr, "IPC Client: Connected to socket '%s'\n", socket_path_.c_str());

    // Receive eventfd via SCM_RIGHTS
    struct msghdr msg = {};
    struct cmsghdr *cmsg;
    char buf[CMSG_SPACE(sizeof(int))];
    char data;
    struct iovec iov = { &data, 1 };

    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    // Set blocking with timeout for eventfd reception
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(control_socket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ssize_t n = recvmsg(control_socket_, &msg, 0);
    if (n > 0 && data == 'E') {
        for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
            if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
                int* fds = (int*)CMSG_DATA(cmsg);
                frame_eventfd_ = fds[0];
                fprintf(stderr, "IPC Client: Received eventfd %d\n", frame_eventfd_);
                break;
            }
        }
    }

    // Set non-blocking for normal operation
    int flags = fcntl(control_socket_, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(control_socket_, F_SETFL, flags | O_NONBLOCK);
    }

    // Clear receive timeout
    tv = { .tv_sec = 0, .tv_usec = 0 };
    setsockopt(control_socket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    return true;
}

void IPCClient::disconnect_socket()
{
    if (control_socket_ >= 0) {
        close(control_socket_);
        control_socket_ = -1;
    }
    if (frame_eventfd_ >= 0) {
        close(frame_eventfd_);
        frame_eventfd_ = -1;
    }
    socket_path_.clear();
}

bool IPCClient::connect(pid_t pid)
{
    if (connected_) {
        disconnect();
    }

    // Clean up zombie SHM from previous disconnect — safe now because
    // the encoder will have seen the nullptr atom by the time start()
    // calls connect() (subprocess stop() already cleared it + slept).
    if (zombie_shm_ && zombie_shm_ != MAP_FAILED) {
        munmap(zombie_shm_, sizeof(IPCBuffer));
        zombie_shm_ = nullptr;
    }

    if (!connect_shm(pid)) {
        return false;
    }

    if (!connect_socket(pid)) {
        disconnect_shm();
        return false;
    }

    pid_ = pid;
    connected_ = true;
    return true;
}

void IPCClient::disconnect()
{
    // Exclusive lock serializes munmap against all parent-side SHM readers
    // (api handlers, encoder, audio reader, webrtc). Readers hold a shared
    // lock for the duration of their dereference, so this will block until
    // in-flight reads finish — then no thread can observe an unmapped page.
    std::unique_lock<std::shared_mutex> shm_guard(g_ipc_shm_mutex);
    disconnect_socket();
    disconnect_shm();
    pid_ = -1;
    connected_ = false;
}

bool IPCClient::send_key(int mac_keycode, bool down)
{
    if (control_socket_ < 0) return false;

    IPCKeyInput msg;
    memset(&msg, 0, sizeof(msg));
    msg.hdr.type = IPC_INPUT_KEY;
    msg.hdr.flags = down ? IPC_KEY_DOWN : IPC_KEY_UP;
    msg.mac_keycode = mac_keycode;

    return send(control_socket_, &msg, sizeof(msg), MSG_NOSIGNAL) == sizeof(msg);
}

bool IPCClient::send_mouse(int x, int y, uint8_t buttons, bool absolute)
{
    if (control_socket_ < 0) return false;

    IPCMouseInput msg;
    memset(&msg, 0, sizeof(msg));
    msg.hdr.type = IPC_INPUT_MOUSE;
    msg.hdr.flags = absolute ? IPC_MOUSE_ABSOLUTE : 0;
    msg.x = static_cast<int16_t>(x);
    msg.y = static_cast<int16_t>(y);
    msg.buttons = buttons;

    return send(control_socket_, &msg, sizeof(msg), MSG_NOSIGNAL) == sizeof(msg);
}

bool IPCClient::send_command(uint8_t command)
{
    if (control_socket_ < 0) return false;

    IPCCommandInput msg;
    memset(&msg, 0, sizeof(msg));
    msg.hdr.type = IPC_INPUT_COMMAND;
    msg.command = command;

    return send(control_socket_, &msg, sizeof(msg), MSG_NOSIGNAL) == sizeof(msg);
}

bool IPCClient::send_audio_request(uint32_t requested_samples)
{
    if (control_socket_ < 0) return false;

    IPCAudioRequestInput msg;
    memset(&msg, 0, sizeof(msg));
    msg.hdr.type = IPC_INPUT_AUDIO_REQUEST;
    msg.requested_samples = requested_samples;

    return send(control_socket_, &msg, sizeof(msg), MSG_NOSIGNAL) == sizeof(msg);
}
