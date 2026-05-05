/*
 * control_ipc_child.cpp - Control + notify endpoints (child side)
 *
 * Phase 3b: replaced AF_UNIX socket() + epoll + SCM_RIGHTS with two
 * QLocalServer instances using the same Qt API on Linux (Unix sockets)
 * and Windows (named pipes).
 *
 *   macemu-control-{PID}  parent → child commands (key/mouse/cmd/audio)
 *   macemu-notify-{PID}   child → parent: 1 byte per published frame
 *
 * Threading: a dedicated worker thread owns both QLocalServer/Socket
 * objects and runs a poll loop using waitForReadyRead(1ms). All Qt
 * objects are constructed on this thread, so cross-thread affinity
 * issues don't arise. The video frame thread can publish a notify by
 * writing to the raw notify socket fd — captured at peer-connect time —
 * via ::send() without any QObject method calls.
 */

#include "ipc_protocol.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>

#include <QLocalServer>
#include <QLocalSocket>
#include <QString>

// ADB input functions (C++ linkage, defined in adb.cpp)
extern void ADBMouseMoved(int x, int y);
extern void ADBMouseDown(int button);
extern void ADBMouseUp(int button);
extern void ADBSetRelMouseMode(bool relative);
extern void ADBKeyDown(int code);
extern void ADBKeyUp(int code);
extern "C" void InvokeDebugger(void);

// Audio request handler (defined in audio_direct.cpp)
extern void audio_request_data(uint32_t requested_samples);

// ── State ─────────────────────────────────────────────────────────

namespace {
std::string         g_control_name;
std::string         g_notify_name;
IPCBuffer*          g_video_shm = nullptr;

std::thread         g_worker_thread;
std::atomic<bool>   g_worker_running{false};

// Notify socket fd, captured at peer-connect time. Written by the
// video frame thread via the global ipc_frame_notifier hook below.
// Independent of QLocalSocket's QObject lifetime — we never close
// this fd; QLocalSocket owns it and will close on its destructor.
std::atomic<int>    g_notify_fd{-1};
}  // namespace

// Provide the global notifier symbol declared in ipc_protocol.h.
ipc_frame_notifier_fn g_ipc_frame_notifier = nullptr;

namespace {
// Thread-safe: writes 1 byte to the notify fd. Called from the video
// frame thread (potentially many threads in the future). EAGAIN means
// the parent's read buffer is full — drop, the parent's next poll
// pass will pick up the latest frame_count anyway.
void notify_frame_ready()
{
    int fd = g_notify_fd.load(std::memory_order_acquire);
    if (fd < 0) return;
    char b = 1;
    ::send(fd, &b, 1, MSG_NOSIGNAL | MSG_DONTWAIT);
}

// ── Input parsing ─────────────────────────────────────────────────

void process_binary_input(const uint8_t* data, size_t len)
{
    if (len < sizeof(IPCInputHeader)) return;

    const IPCInputHeader* hdr = (const IPCInputHeader*)data;

    switch (hdr->type) {
        case IPC_INPUT_KEY: {
            if (len < sizeof(IPCKeyInput)) return;
            const IPCKeyInput* key = (const IPCKeyInput*)data;
            if (hdr->flags & IPC_KEY_DOWN) {
                ADBKeyDown(key->mac_keycode);
            } else {
                ADBKeyUp(key->mac_keycode);
            }
            break;
        }
        case IPC_INPUT_MOUSE: {
            if (len < sizeof(IPCMouseInput)) return;
            const IPCMouseInput* mouse = (const IPCMouseInput*)data;

            bool absolute = (mouse->hdr.flags & IPC_MOUSE_ABSOLUTE) != 0;
            bool has_motion = (mouse->x != 0 || mouse->y != 0) || absolute;

            // Only change mouse mode when there's actual motion.
            // Button-only events (dx=dy=0, relative) must not toggle the mode,
            // because ADBSetRelMouseMode resets mouse_x/y to 0 on change.
            if (has_motion) {
                if (absolute) {
                    ADBSetRelMouseMode(false);
                    ADBMouseMoved(static_cast<uint16_t>(mouse->x),
                                  static_cast<uint16_t>(mouse->y));
                } else {
                    ADBSetRelMouseMode(true);
                    ADBMouseMoved(mouse->x, mouse->y);
                }
            }

            // Handle button changes
            static uint8_t last_buttons = 0;
            uint8_t changed = mouse->buttons ^ last_buttons;
            if (changed & IPC_MOUSE_LEFT) {
                if (mouse->buttons & IPC_MOUSE_LEFT) ADBMouseDown(0);
                else                                  ADBMouseUp(0);
            }
            if (changed & IPC_MOUSE_RIGHT) {
                if (mouse->buttons & IPC_MOUSE_RIGHT) ADBMouseDown(1);
                else                                  ADBMouseUp(1);
            }
            last_buttons = mouse->buttons;
            break;
        }
        case IPC_INPUT_COMMAND: {
            if (len < sizeof(IPCCommandInput)) return;
            const IPCCommandInput* cmd = (const IPCCommandInput*)data;
            switch (cmd->command) {
                case IPC_CMD_STOP:
                    fprintf(stderr, "IPC: Stop command received\n");
                    exit(0);
                    break;
                case IPC_CMD_RESET:
                    fprintf(stderr, "IPC: Reset command received\n");
                    exit(75);  // Special exit code for restart
                    break;
                case IPC_CMD_INVOKE_DEBUG:
                    fprintf(stderr, "IPC: Invoke debugger command received\n");
                    InvokeDebugger();
                    break;
                default:
                    break;
            }
            break;
        }
        case IPC_INPUT_AUDIO_REQUEST: {
            if (len < sizeof(IPCAudioRequestInput)) return;
            const IPCAudioRequestInput* req = (const IPCAudioRequestInput*)data;
            audio_request_data(req->requested_samples);
            break;
        }
        default:
            fprintf(stderr, "IPC: Unknown input type %d\n", hdr->type);
            break;
    }
}

// Drain whatever the control client just sent and demux into individual
// IPCInput messages. Mirrors the recv() loop in the legacy code.
void drain_control_socket(QLocalSocket* sock)
{
    QByteArray buf = sock->readAll();
    const uint8_t* data = (const uint8_t*)buf.constData();
    size_t total = (size_t)buf.size();
    size_t offset = 0;
    while (offset < total) {
        if (offset + sizeof(IPCInputHeader) > total) break;
        const IPCInputHeader* hdr = (const IPCInputHeader*)(data + offset);
        size_t msg_size = 0;
        switch (hdr->type) {
            case IPC_INPUT_KEY:           msg_size = sizeof(IPCKeyInput); break;
            case IPC_INPUT_MOUSE:         msg_size = sizeof(IPCMouseInput); break;
            case IPC_INPUT_COMMAND:       msg_size = sizeof(IPCCommandInput); break;
            case IPC_INPUT_AUDIO_REQUEST: msg_size = sizeof(IPCAudioRequestInput); break;
            default: msg_size = sizeof(IPCInputHeader); break;
        }
        if (offset + msg_size > total) break;
        process_binary_input(data + offset, msg_size);
        offset += msg_size;
    }
}

// ── Worker thread ─────────────────────────────────────────────────

void worker_main()
{
    ADBSetRelMouseMode(true);

    // Construct QObjects on this thread so their thread affinity matches
    // the methods we'll call. QLocalServer::listen() is the equivalent
    // of bind+listen; we listen synchronously and then poll for
    // connections + readable bytes via waitFor* APIs (no event loop).
    QLocalServer control_server;
    QLocalServer notify_server;

    QString control_name = QString::fromStdString(g_control_name);
    QString notify_name  = QString::fromStdString(g_notify_name);

    // QLocalServer::removeServer cleans up a stale socket file from a
    // previous crashed run — same role as the unlink() in the old code.
    QLocalServer::removeServer(control_name);
    QLocalServer::removeServer(notify_name);

    if (!control_server.listen(control_name)) {
        fprintf(stderr, "IPC: control listen(%s) failed: %s\n",
                control_name.toUtf8().constData(),
                control_server.errorString().toUtf8().constData());
        return;
    }
    if (!notify_server.listen(notify_name)) {
        fprintf(stderr, "IPC: notify listen(%s) failed: %s\n",
                notify_name.toUtf8().constData(),
                notify_server.errorString().toUtf8().constData());
        return;
    }
    fprintf(stderr, "IPC: Listening on '%s' + '%s'\n",
            control_name.toUtf8().constData(),
            notify_name.toUtf8().constData());

    QLocalSocket* control_client = nullptr;
    QLocalSocket* notify_client  = nullptr;

    while (g_worker_running.load(std::memory_order_acquire)) {
        // Accept new control connection. Single-client (parent only).
        if (!control_client &&
            control_server.waitForNewConnection(1)) {
            control_client = control_server.nextPendingConnection();
            fprintf(stderr, "IPC: Parent connected to control\n");
        }
        // Accept new notify connection — same one-shot pattern.
        if (!notify_client &&
            notify_server.waitForNewConnection(1)) {
            notify_client = notify_server.nextPendingConnection();
            int fd = (int)notify_client->socketDescriptor();
            g_notify_fd.store(fd, std::memory_order_release);
            // Hook the global notifier so ipc_frame_complete can write
            // to the parent's notify socket from any thread.
            g_ipc_frame_notifier = notify_frame_ready;
            fprintf(stderr, "IPC: Parent connected to notify (fd=%d)\n", fd);
        }

        // Drain control reads. waitForReadyRead returns true if data
        // arrived in the last <1ms, false on timeout — both fine.
        if (control_client) {
            if (control_client->waitForReadyRead(1)) {
                drain_control_socket(control_client);
            }
            if (control_client->state() == QLocalSocket::UnconnectedState) {
                fprintf(stderr, "IPC: Parent disconnected from control\n");
                control_client->deleteLater();
                control_client = nullptr;
            }
        }
        // Notify socket is write-only from our side; just check
        // disconnect state so we drop the fd if the parent dies.
        if (notify_client &&
            notify_client->state() == QLocalSocket::UnconnectedState) {
            fprintf(stderr, "IPC: Parent disconnected from notify\n");
            g_ipc_frame_notifier = nullptr;
            g_notify_fd.store(-1, std::memory_order_release);
            notify_client->deleteLater();
            notify_client = nullptr;
        }

        // No connections, no readable bytes — yield briefly so we
        // don't burn CPU when the parent isn't there yet.
        if (!control_client && !notify_client) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    g_ipc_frame_notifier = nullptr;
    g_notify_fd.store(-1, std::memory_order_release);
    if (control_client) control_client->deleteLater();
    if (notify_client)  notify_client->deleteLater();
    fprintf(stderr, "IPC: Control thread exiting\n");
}

}  // namespace

// ── Public C API ─────────────────────────────────────────────────

extern "C" {

bool control_ipc_init(IPCBuffer* shm)
{
    g_video_shm = shm;
    pid_t pid = getpid();
    g_control_name = std::string(IPC_CONTROL_NAME_PREFIX) + std::to_string(pid);
    g_notify_name  = std::string(IPC_NOTIFY_NAME_PREFIX)  + std::to_string(pid);
    return true;  // QLocalServer::listen happens on the worker thread
}

void control_ipc_start(void)
{
    g_worker_running.store(true, std::memory_order_release);
    g_worker_thread = std::thread(worker_main);
}

void control_ipc_exit(void)
{
    g_worker_running.store(false, std::memory_order_release);
    if (g_worker_thread.joinable()) {
        g_worker_thread.join();
    }
    // QLocalServer::removeServer cleans up the socket file even if the
    // server already destructed — safe to call on a stale name.
    QLocalServer::removeServer(QString::fromStdString(g_control_name));
    QLocalServer::removeServer(QString::fromStdString(g_notify_name));
    g_control_name.clear();
    g_notify_name.clear();
}

// Signal-safe: only unlinks the socket files (no malloc, no thread join).
// Safe to call from a signal handler — uses POSIX unlink(2) directly,
// not Qt APIs.
void control_ipc_unlink(void)
{
    if (!g_control_name.empty()) {
        std::string p = "/tmp/" + g_control_name;
        unlink(p.c_str());
    }
    if (!g_notify_name.empty()) {
        std::string p = "/tmp/" + g_notify_name;
        unlink(p.c_str());
    }
}

} // extern "C"
