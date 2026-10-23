/* MacPhoenix: the host network edge with no network (rsys/hostnet.h):
 * every socket call fails with ENETDOWN, every lookup with EAI_FAIL, and
 * nothing runs on a host thread. MacTCP and Open Transport still load and
 * answer their calls with their own errors. */
#include <rsys/hostnet.h>

#include <cerrno>

using namespace Executor;

namespace
{
int down()
{
    errno = ENETDOWN;
    return -1;
}
}

int hostnet::socket(int, int, int) { return down(); }
int hostnet::close(int) { errno = EBADF; return -1; }
int hostnet::connect(int, const struct sockaddr *, socklen_t) { return down(); }
int hostnet::bind(int, const struct sockaddr *, socklen_t) { return down(); }
int hostnet::listen(int, int) { return down(); }
int hostnet::accept(int, struct sockaddr *, socklen_t *) { return down(); }
int hostnet::shutdown(int, int) { return down(); }
ssize_t hostnet::send(int, const void *, size_t, int) { return down(); }
ssize_t hostnet::sendto(int, const void *, size_t, int, const struct sockaddr *, socklen_t) { return down(); }
ssize_t hostnet::sendmsg(int, const struct msghdr *, int) { return down(); }
ssize_t hostnet::writev(int, const struct iovec *, int) { return down(); }
ssize_t hostnet::recv(int, void *, size_t, int) { return down(); }
ssize_t hostnet::recvfrom(int, void *, size_t, int, struct sockaddr *, socklen_t *) { return down(); }
int hostnet::setsockopt(int, int, int, const void *, socklen_t) { return down(); }
int hostnet::getsockopt(int, int, int, void *, socklen_t *) { return down(); }
int hostnet::getsockname(int, struct sockaddr *, socklen_t *) { return down(); }
int hostnet::getpeername(int, struct sockaddr *, socklen_t *) { return down(); }
int hostnet::set_nonblocking(int) { return down(); }
int hostnet::bytes_available(int) { return down(); }
ssize_t hostnet::next_datagram_size(int) { return down(); }

/* No socket is ever open: every descriptor asked about is invalid. */
int hostnet::poll(struct pollfd *fds, nfds_t count, int)
{
    int n = 0;
    for(nfds_t i = 0; i < count; i++)
    {
        fds[i].revents = fds[i].fd >= 0 ? POLLNVAL : 0;
        n += fds[i].revents != 0;
    }
    return n;
}

int hostnet::getifaddrs(struct ifaddrs **list)
{
    *list = nullptr;
    return down();
}
void hostnet::freeifaddrs(struct ifaddrs *) {}
int hostnet::getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **result)
{
    *result = nullptr;
    return EAI_FAIL;
}
void hostnet::freeaddrinfo(struct addrinfo *) {}
int hostnet::getnameinfo(const struct sockaddr *, socklen_t, char *, socklen_t, char *, socklen_t, int)
{
    return EAI_FAIL;
}
int hostnet::res_query(const char *, int, int, unsigned char *, int) { return down(); }

void hostnet::run_detached(std::function<void()> work)
{
    work();
}

hostnet::Watcher::Watcher(Interrupt &irq) : irq_(irq) {}
void hostnet::Watcher::set(std::vector<struct pollfd>, Clock::time_point) {}
