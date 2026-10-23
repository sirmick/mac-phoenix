/* MacPhoenix: the host network edge on host sockets (rsys/hostnet.h). */
#include <rsys/hostnet.h>
#include <time/syncint.h>

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <arpa/nameser.h>
#include <fcntl.h>
#include <resolv.h>
#include <sys/ioctl.h>
#include <unistd.h>

using namespace Executor;

bool hostnet::available() { return true; }
int hostnet::socket(int domain, int type, int protocol) { return ::socket(domain, type, protocol); }
int hostnet::close(int fd) { return ::close(fd); }
int hostnet::connect(int fd, const struct sockaddr *a, socklen_t n) { return ::connect(fd, a, n); }
int hostnet::bind(int fd, const struct sockaddr *a, socklen_t n) { return ::bind(fd, a, n); }
int hostnet::listen(int fd, int backlog) { return ::listen(fd, backlog); }
int hostnet::accept(int fd, struct sockaddr *a, socklen_t *n) { return ::accept(fd, a, n); }
int hostnet::shutdown(int fd, int how) { return ::shutdown(fd, how); }
ssize_t hostnet::send(int fd, const void *b, size_t n, int f) { return ::send(fd, b, n, f); }
ssize_t hostnet::sendto(int fd, const void *b, size_t n, int f, const struct sockaddr *to, socklen_t tl)
{
    return ::sendto(fd, b, n, f, to, tl);
}
ssize_t hostnet::sendmsg(int fd, const struct msghdr *m, int f) { return ::sendmsg(fd, m, f); }
ssize_t hostnet::writev(int fd, const struct iovec *iov, int count) { return ::writev(fd, iov, count); }
ssize_t hostnet::recv(int fd, void *b, size_t n, int f) { return ::recv(fd, b, n, f); }
ssize_t hostnet::recvfrom(int fd, void *b, size_t n, int f, struct sockaddr *from, socklen_t *fl)
{
    return ::recvfrom(fd, b, n, f, from, fl);
}
int hostnet::setsockopt(int fd, int level, int name, const void *v, socklen_t n)
{
    return ::setsockopt(fd, level, name, v, n);
}
int hostnet::getsockopt(int fd, int level, int name, void *v, socklen_t *n)
{
    return ::getsockopt(fd, level, name, v, n);
}
int hostnet::getsockname(int fd, struct sockaddr *a, socklen_t *n) { return ::getsockname(fd, a, n); }
int hostnet::getpeername(int fd, struct sockaddr *a, socklen_t *n) { return ::getpeername(fd, a, n); }

int hostnet::set_nonblocking(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    return fl < 0 ? fl : fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

int hostnet::bytes_available(int fd)
{
    int n = 0;
    return ioctl(fd, FIONREAD, &n) < 0 ? -1 : n;
}

ssize_t hostnet::next_datagram_size(int fd)
{
#if defined(__APPLE__)
    struct pollfd p = { fd, POLLIN, 0 };
    if(::poll(&p, 1, 0) <= 0)
        return -1;
    int n = 0;
    socklen_t len = sizeof n;
    return ::getsockopt(fd, SOL_SOCKET, SO_NREAD, &n, &len) == 0 ? n : -1;
#else
    return ::recv(fd, nullptr, 0, MSG_PEEK | MSG_TRUNC | MSG_DONTWAIT);
#endif
}

int hostnet::poll(struct pollfd *fds, nfds_t count, int timeout_ms) { return ::poll(fds, count, timeout_ms); }

int hostnet::getifaddrs(struct ifaddrs **list) { return ::getifaddrs(list); }
void hostnet::freeifaddrs(struct ifaddrs *list) { ::freeifaddrs(list); }
int hostnet::getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                         struct addrinfo **result)
{
    return ::getaddrinfo(node, service, hints, result);
}
void hostnet::freeaddrinfo(struct addrinfo *result) { ::freeaddrinfo(result); }
int hostnet::getnameinfo(const struct sockaddr *a, socklen_t n, char *host, socklen_t hostlen,
                         char *serv, socklen_t servlen, int flags)
{
    return ::getnameinfo(a, n, host, hostlen, serv, servlen, flags);
}
int hostnet::res_query(const char *name, int cls, int type, unsigned char *answer, int len)
{
    return ::res_query(name, cls, type, answer, len);
}

void hostnet::run_detached(std::function<void()> work)
{
    std::thread(std::move(work)).detach();
}

/* ---- the watcher ---------------------------------------------------- */

struct hostnet::Watcher::State
{
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<struct pollfd> fds;
    Clock::time_point deadline = Clock::time_point::max();
    uint64_t gen = 0, handled = 0;
    int wake[2] = { -1, -1 };
};

hostnet::Watcher::Watcher(Interrupt &irq) : irq_(irq) {}

static void watcher_main(hostnet::Watcher::State *st, Interrupt *irq)
{
    using Clock = hostnet::Watcher::Clock;
    for(;;)
    {
        std::vector<struct pollfd> fds;
        Clock::time_point deadline;
        uint64_t gen;
        {
            std::unique_lock<std::mutex> lock(st->mutex);
            st->cv.wait(lock, [st] { return st->gen != st->handled; });
            fds = st->fds;
            deadline = st->deadline;
            gen = st->gen;
        }
        fds.push_back({ st->wake[0], POLLIN, 0 });
        int timeout = -1;
        if(deadline != Clock::time_point::max())
        {
            auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now());
            timeout = std::max<int64_t>(0, left.count());
        }
        int r = ::poll(fds.data(), fds.size(), timeout);
        if(fds.back().revents)
        {
            /* set(): a new set to wait for. */
            char buf[64];
            while(::read(st->wake[0], buf, sizeof buf) > 0)
                ;
        }
        bool fire = r == 0;
        for(size_t i = 0; i + 1 < fds.size(); i++)
            fire |= fds[i].revents != 0;
        if(fire)
        {
            {
                std::lock_guard<std::mutex> lock(st->mutex);
                st->handled = gen;
            }
            irq->trigger();
        }
    }
}

void hostnet::Watcher::set(std::vector<struct pollfd> fds, Clock::time_point deadline)
{
    if(!state_)
    {
        if(fds.empty() && deadline == Clock::time_point::max())
            return;
        auto *st = new State;
        if(pipe(st->wake) < 0)
        {
            delete st;
            return;
        }
        set_nonblocking(st->wake[0]);
        set_nonblocking(st->wake[1]);
        state_ = st;
        std::thread(watcher_main, st, &irq_).detach();
    }
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->fds = std::move(fds);
        state_->deadline = deadline;
        state_->gen++;
    }
    state_->cv.notify_all();
    char c = 0;
    (void)!::write(state_->wake[1], &c, 1); /* out of a poll on the old set */
}
