/*
 * ipc_client.cpp - Parent-side IPC connection to child emulator
 *
 * Phase 3b: AF_UNIX + SCM_RIGHTS + eventfd replaced with QSharedMemory
 * (already 3a) + two QLocalSockets (control + notify, name-based
 * discovery). Same wire format, portable to Windows.
 */

#include "ipc_client.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <mutex>
#include <shared_mutex>
#include <unistd.h>
#include <sys/socket.h>

#include <QSharedMemory>
#include <QLocalSocket>
#include <QString>

std::shared_mutex g_ipc_shm_mutex;

IPCClient::IPCClient() = default;

IPCClient::~IPCClient()
{
    disconnect();
}

// ── Shared memory ─────────────────────────────────────────────────

bool IPCClient::connect_shm(pid_t pid)
{
    shm_name_ = std::string(IPC_VIDEO_SHM_PREFIX) + std::to_string(pid);

    shm_owner_ = std::make_unique<QSharedMemory>(QString::fromStdString(shm_name_));
    if (!shm_owner_->attach()) {
        // Caller polls — silent failure here is normal during the
        // initial wait for the child to publish.
        shm_owner_.reset();
        return false;
    }

    shm_ = static_cast<IPCBuffer*>(shm_owner_->data());
    if (!shm_) {
        fprintf(stderr, "IPC Client: QSharedMemory::data() returned null for PID %d\n", pid);
        shm_owner_->detach();
        shm_owner_.reset();
        return false;
    }

    int result = ipc_validate_buffer(shm_, pid);
    if (result != 0) {
        fprintf(stderr, "IPC Client: SHM validation failed for PID %d (error %d)\n",
                pid, result);
        shm_owner_->detach();
        shm_owner_.reset();
        shm_ = nullptr;
        return false;
    }

    fprintf(stderr, "IPC Client: Connected to SHM '%s' (%dx%d)\n",
            shm_name_.c_str(), shm_->width, shm_->height);
    return true;
}

void IPCClient::disconnect_shm()
{
    if (shm_owner_) {
        shm_owner_->detach();
        shm_owner_.reset();
    }
    shm_ = nullptr;
    shm_name_.clear();
}

// ── Sockets ───────────────────────────────────────────────────────

namespace {
// Connect a QLocalSocket to a named server with a 2s timeout. Returns
// nullptr on failure. The child opens both servers in its IPC worker
// thread, so by the time the parent's IPCClient::connect runs, both
// names should resolve. Timeout protects against the child spawning
// the worker late (or never).
std::unique_ptr<QLocalSocket> connect_local_socket(const QString& name,
                                                   const char*    role)
{
    auto sock = std::make_unique<QLocalSocket>();
    sock->connectToServer(name, QIODevice::ReadWrite);
    if (!sock->waitForConnected(2000)) {
        fprintf(stderr, "IPC Client: %s connect to '%s' failed: %s\n",
                role,
                name.toUtf8().constData(),
                sock->errorString().toUtf8().constData());
        return nullptr;
    }
    return sock;
}
}  // namespace

bool IPCClient::connect_control_socket(pid_t pid)
{
    control_name_ = std::string(IPC_CONTROL_NAME_PREFIX) + std::to_string(pid);
    control_socket_ = connect_local_socket(QString::fromStdString(control_name_),
                                           "control");
    if (!control_socket_) return false;
    control_fd_ = (int)control_socket_->socketDescriptor();
    fprintf(stderr, "IPC Client: Connected to control '%s' (fd=%d)\n",
            control_name_.c_str(), control_fd_);
    return true;
}

bool IPCClient::connect_notify_socket(pid_t pid)
{
    notify_name_ = std::string(IPC_NOTIFY_NAME_PREFIX) + std::to_string(pid);
    notify_socket_ = connect_local_socket(QString::fromStdString(notify_name_),
                                          "notify");
    if (!notify_socket_) return false;
    notify_fd_ = (int)notify_socket_->socketDescriptor();
    fprintf(stderr, "IPC Client: Connected to notify '%s' (fd=%d)\n",
            notify_name_.c_str(), notify_fd_);
    return true;
}

void IPCClient::disconnect_sockets()
{
    control_socket_.reset();   // Qt closes the underlying fd
    notify_socket_.reset();
    control_fd_ = -1;
    notify_fd_  = -1;
    control_name_.clear();
    notify_name_.clear();
}

// ── Lifecycle ─────────────────────────────────────────────────────

bool IPCClient::connect(pid_t pid)
{
    if (connected_) {
        disconnect();
    }
    if (!connect_shm(pid)) {
        return false;
    }
    if (!connect_control_socket(pid)) {
        disconnect_shm();
        return false;
    }
    if (!connect_notify_socket(pid)) {
        disconnect_sockets();
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
    disconnect_sockets();
    disconnect_shm();
    pid_ = -1;
    connected_ = false;
}

// ── Send helpers (control socket) ─────────────────────────────────
//
// We grab the raw fd and ::send() directly. QLocalSocket::write() would
// also work but requires the QObject's owning thread; multiple parent
// threads call these methods (api handlers, audio request thread, etc.)
// so the fd is the safer common substrate. The fd itself is shared
// across threads with no locking — kernel send() is atomic for messages
// up to PIPE_BUF.

bool IPCClient::send_key(int mac_keycode, bool down)
{
    if (control_fd_ < 0) return false;
    IPCKeyInput msg{};
    msg.hdr.type = IPC_INPUT_KEY;
    msg.hdr.flags = down ? IPC_KEY_DOWN : IPC_KEY_UP;
    msg.mac_keycode = (uint8_t)mac_keycode;
    return ::send(control_fd_, &msg, sizeof(msg), MSG_NOSIGNAL) == sizeof(msg);
}

bool IPCClient::send_mouse(int x, int y, uint8_t buttons, bool absolute)
{
    if (control_fd_ < 0) return false;
    IPCMouseInput msg{};
    msg.hdr.type = IPC_INPUT_MOUSE;
    msg.hdr.flags = absolute ? IPC_MOUSE_ABSOLUTE : 0;
    msg.x = (int16_t)x;
    msg.y = (int16_t)y;
    msg.buttons = buttons;
    return ::send(control_fd_, &msg, sizeof(msg), MSG_NOSIGNAL) == sizeof(msg);
}

bool IPCClient::send_command(uint8_t command)
{
    if (control_fd_ < 0) return false;
    IPCCommandInput msg{};
    msg.hdr.type = IPC_INPUT_COMMAND;
    msg.command = command;
    return ::send(control_fd_, &msg, sizeof(msg), MSG_NOSIGNAL) == sizeof(msg);
}

bool IPCClient::send_audio_request(uint32_t requested_samples)
{
    if (control_fd_ < 0) return false;
    IPCAudioRequestInput msg{};
    msg.hdr.type = IPC_INPUT_AUDIO_REQUEST;
    msg.requested_samples = requested_samples;
    return ::send(control_fd_, &msg, sizeof(msg), MSG_NOSIGNAL) == sizeof(msg);
}
