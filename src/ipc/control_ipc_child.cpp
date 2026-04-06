/*
 * control_ipc_child.cpp - Control socket and input handling for IPC child
 *
 * Creates a Unix domain socket, accepts connection from parent, sends
 * frame_ready_eventfd via SCM_RIGHTS, and processes binary input.
 *
 * Based on legacy/BasiliskII/src/IPC/control_ipc.cpp.
 */

#include "ipc_protocol.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <errno.h>

// ADB input functions (C++ linkage, defined in adb.cpp)
extern void ADBMouseMoved(int x, int y);
extern void ADBMouseDown(int button);
extern void ADBMouseUp(int button);
extern void ADBSetRelMouseMode(bool relative);
extern void ADBKeyDown(int code);
extern void ADBKeyUp(int code);

// Audio request handler (defined in audio_direct.cpp)
extern void audio_request_data(uint32_t requested_samples);

// Global state
static int g_listen_socket = -1;
static int g_control_socket = -1;
static std::string g_socket_path;
static IPCBuffer* g_video_shm = nullptr;

static std::thread g_control_thread;
static std::atomic<bool> g_control_running(false);

/*
 * Create Unix socket (child owns this)
 */

static bool create_control_socket()
{
    pid_t pid = getpid();
    g_socket_path = std::string(IPC_CONTROL_SOCK_PREFIX) + std::to_string(pid) +
                    std::string(IPC_CONTROL_SOCK_SUFFIX);

    unlink(g_socket_path.c_str());

    g_listen_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_listen_socket < 0) {
        fprintf(stderr, "IPC: Failed to create socket: %s\n", strerror(errno));
        return false;
    }

    int flags = fcntl(g_listen_socket, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(g_listen_socket, F_SETFL, flags | O_NONBLOCK);
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, g_socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(g_listen_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "IPC: Failed to bind socket to %s: %s\n",
                g_socket_path.c_str(), strerror(errno));
        close(g_listen_socket);
        g_listen_socket = -1;
        return false;
    }

    if (listen(g_listen_socket, 1) < 0) {
        fprintf(stderr, "IPC: Failed to listen: %s\n", strerror(errno));
        close(g_listen_socket);
        g_listen_socket = -1;
        unlink(g_socket_path.c_str());
        return false;
    }

    fprintf(stderr, "IPC: Listening on '%s'\n", g_socket_path.c_str());
    return true;
}

static void destroy_control_socket()
{
    if (g_control_socket >= 0) {
        close(g_control_socket);
        g_control_socket = -1;
    }
    if (g_listen_socket >= 0) {
        close(g_listen_socket);
        g_listen_socket = -1;
    }
    if (!g_socket_path.empty()) {
        unlink(g_socket_path.c_str());
        g_socket_path.clear();
    }
}

/*
 * Process binary input from parent
 */

static void process_binary_input(const uint8_t* data, size_t len)
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
                if (mouse->buttons & IPC_MOUSE_LEFT)
                    ADBMouseDown(0);
                else
                    ADBMouseUp(0);
            }
            if (changed & IPC_MOUSE_RIGHT) {
                if (mouse->buttons & IPC_MOUSE_RIGHT)
                    ADBMouseDown(1);
                else
                    ADBMouseUp(1);
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

/*
 * Control socket thread (epoll-based)
 */

static void control_socket_thread()
{
    uint8_t buffer[256];

    ADBSetRelMouseMode(true);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        fprintf(stderr, "IPC: Failed to create epoll: %s\n", strerror(errno));
        return;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = g_listen_socket;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, g_listen_socket, &ev);

    fprintf(stderr, "IPC: Control thread started (epoll)\n");

    struct epoll_event events[2];

    while (g_control_running.load(std::memory_order_acquire)) {
        int n = epoll_wait(epoll_fd, events, 2, 1);  // 1ms for responsive input
        if (n < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "IPC: epoll_wait error: %s\n", strerror(errno));
            break;
        }

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            if (fd == g_listen_socket && (events[i].events & EPOLLIN)) {
                if (g_control_socket < 0) {
                    struct sockaddr_un addr;
                    socklen_t len = sizeof(addr);
                    int new_fd = accept(g_listen_socket, (struct sockaddr*)&addr, &len);
                    if (new_fd >= 0) {
                        int flags = fcntl(new_fd, F_GETFL, 0);
                        if (flags >= 0) {
                            fcntl(new_fd, F_SETFL, flags | O_NONBLOCK);
                        }

                        g_control_socket = new_fd;
                        fprintf(stderr, "IPC: Parent connected\n");

                        // Send eventfd via SCM_RIGHTS
                        if (g_video_shm && g_video_shm->frame_ready_eventfd >= 0) {
                            int fds[1] = { g_video_shm->frame_ready_eventfd };

                            struct msghdr msg = {};
                            struct cmsghdr *cmsg;
                            char ctrl_buf[CMSG_SPACE(sizeof(int))];
                            char data = 'E';
                            struct iovec iov = { &data, 1 };

                            msg.msg_iov = &iov;
                            msg.msg_iovlen = 1;
                            msg.msg_control = ctrl_buf;
                            msg.msg_controllen = sizeof(ctrl_buf);

                            cmsg = CMSG_FIRSTHDR(&msg);
                            cmsg->cmsg_level = SOL_SOCKET;
                            cmsg->cmsg_type = SCM_RIGHTS;
                            cmsg->cmsg_len = CMSG_LEN(sizeof(int));
                            memcpy(CMSG_DATA(cmsg), fds, sizeof(int));

                            if (sendmsg(g_control_socket, &msg, 0) > 0) {
                                fprintf(stderr, "IPC: Sent eventfd %d to parent\n",
                                        g_video_shm->frame_ready_eventfd);
                            } else {
                                fprintf(stderr, "IPC: Failed to send eventfd: %s\n",
                                        strerror(errno));
                            }
                        }

                        ev.events = EPOLLIN;
                        ev.data.fd = g_control_socket;
                        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, g_control_socket, &ev);
                    }
                }
            }
            else if (fd == g_control_socket && (events[i].events & EPOLLIN)) {
                ssize_t nr = recv(g_control_socket, buffer, sizeof(buffer), 0);
                if (nr > 0) {
                    size_t offset = 0;
                    while (offset < (size_t)nr) {
                        if (offset + sizeof(IPCInputHeader) > (size_t)nr) break;
                        const IPCInputHeader* hdr = (const IPCInputHeader*)(buffer + offset);
                        size_t msg_size = 0;
                        switch (hdr->type) {
                            case IPC_INPUT_KEY:     msg_size = sizeof(IPCKeyInput); break;
                            case IPC_INPUT_MOUSE:   msg_size = sizeof(IPCMouseInput); break;
                            case IPC_INPUT_COMMAND: msg_size = sizeof(IPCCommandInput); break;
                            case IPC_INPUT_AUDIO_REQUEST: msg_size = sizeof(IPCAudioRequestInput); break;
                            default: msg_size = sizeof(IPCInputHeader); break;
                        }
                        if (offset + msg_size > (size_t)nr) break;
                        process_binary_input(buffer + offset, msg_size);
                        offset += msg_size;
                    }
                } else if (nr == 0 || (nr < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    fprintf(stderr, "IPC: Parent disconnected\n");
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, g_control_socket, nullptr);
                    close(g_control_socket);
                    g_control_socket = -1;
                }
            }
        }
    }

    close(epoll_fd);
    fprintf(stderr, "IPC: Control thread exiting\n");
}

/*
 * Public API
 */

extern "C" {

bool control_ipc_init(IPCBuffer* shm)
{
    g_video_shm = shm;
    return create_control_socket();
}

void control_ipc_start(void)
{
    g_control_running.store(true, std::memory_order_release);
    g_control_thread = std::thread(control_socket_thread);
}

void control_ipc_exit(void)
{
    g_control_running.store(false, std::memory_order_release);
    if (g_control_thread.joinable()) {
        g_control_thread.join();
    }
    destroy_control_socket();
}

// Signal-safe: only unlinks the socket path (no malloc, no thread join).
// Safe to call from a signal handler.
void control_ipc_unlink(void)
{
    if (!g_socket_path.empty()) {
        unlink(g_socket_path.c_str());
    }
}

} // extern "C"
