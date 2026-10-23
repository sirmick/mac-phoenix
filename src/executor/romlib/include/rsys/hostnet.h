#pragma once

/* MacPhoenix: the host network edge (docs/executor/DIRECTION.md, D3).
 * MacTCP, Open Transport and their resolvers reach the host's network only
 * through here. The backend is picked by CMake (EXECUTOR_NET):
 *
 *   posix  host sockets and the host's resolver (host/posix/net.cpp)
 *   none   no network: sockets fail with ENETDOWN, lookups with EAI_FAIL,
 *          no host threads (host/none/net.cpp); what the wasm build has
 *          until the socket relay exists
 *
 * Sockets are POSIX-shaped: descriptors, sockaddrs and flags as
 * <sys/socket.h> has them, so a backend is a relay of the same calls. */

#include <chrono>
#include <functional>
#include <vector>

#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/uio.h>

namespace Executor
{
class Interrupt;

namespace hostnet
{
int socket(int domain, int type, int protocol);
int close(int fd);
int connect(int fd, const struct sockaddr *addr, socklen_t len);
int bind(int fd, const struct sockaddr *addr, socklen_t len);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr *addr, socklen_t *len);
int shutdown(int fd, int how);
ssize_t send(int fd, const void *buf, size_t len, int flags);
ssize_t sendto(int fd, const void *buf, size_t len, int flags,
               const struct sockaddr *to, socklen_t tolen);
ssize_t sendmsg(int fd, const struct msghdr *msg, int flags);
ssize_t writev(int fd, const struct iovec *iov, int count);
ssize_t recv(int fd, void *buf, size_t len, int flags);
ssize_t recvfrom(int fd, void *buf, size_t len, int flags,
                 struct sockaddr *from, socklen_t *fromlen);
int setsockopt(int fd, int level, int name, const void *value, socklen_t len);
int getsockopt(int fd, int level, int name, void *value, socklen_t *len);
int getsockname(int fd, struct sockaddr *addr, socklen_t *len);
int getpeername(int fd, struct sockaddr *addr, socklen_t *len);
int set_nonblocking(int fd);
/* Bytes queued to read (FIONREAD), or -1. */
int bytes_available(int fd);
/* The size of the next datagram, or -1 (none, or unknown). */
ssize_t next_datagram_size(int fd);
int poll(struct pollfd *fds, nfds_t count, int timeout_ms);

int getifaddrs(struct ifaddrs **list);
void freeifaddrs(struct ifaddrs *list);
int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                struct addrinfo **result);
void freeaddrinfo(struct addrinfo *result);
int getnameinfo(const struct sockaddr *addr, socklen_t len, char *host, socklen_t hostlen,
                char *serv, socklen_t servlen, int flags);
/* A DNS query in the host's name servers' wire format (res_query). */
int res_query(const char *name, int cls, int type, unsigned char *answer, int len);

/* Work off the emulator thread (a lookup). posix: a detached host thread;
 * none: at once, on the caller's. */
void run_detached(std::function<void()> work);

/* Waits on the sockets a subsystem's calls are parked on and raises its
 * interrupt when one is ready or the deadline passes; then waits for the
 * next set() (the interrupt's service ends in one) before looking again,
 * so a ready socket nobody reads is not a busy loop. posix: a host thread,
 * started at the first set() with something to wait for. */
class Watcher
{
public:
    using Clock = std::chrono::steady_clock;

    explicit Watcher(Interrupt &irq);
    void set(std::vector<struct pollfd> fds, Clock::time_point deadline = Clock::time_point::max());

    struct State;

private:
    Interrupt &irq_;
    State *state_ = nullptr;
};
}
}
