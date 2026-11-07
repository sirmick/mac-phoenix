/* MacPhoenix: the host network edge in a browser tab (rsys/hostnet.h).
 *
 * A page can't open sockets, so the descriptors here are the tab's own:
 * a table of sockets in this file. What leaves the tab goes over one
 * WebSocket to executor/server's relay (/relay; the protocol is in
 * docs/executor/DIRECTION.md, "The network relay"): TCP connections out,
 * UDP datagrams, DNS. Listeners and loopback stay in the tab: a connect
 * to our own address, or to 127/8 where a listener of the tab has the
 * port, is a pair of sockets here (OT's and MacTCP's passive opens, a
 * program talking to itself). The relay never listens for the tab.
 *
 * executor/web/net.js owns the WebSocket; the C side sends through it and
 * takes what came, as web_main.cpp takes input: JS never calls into C.
 * Messages arrive while the module yields to the browser: each yield
 * (host/wasm/idle.cpp) pumps them into the sockets and looks at the
 * watchers, and a call that waits (poll, a lookup, a blocking socket)
 * yields until what it waits for is there. */
#include <rsys/hostidle.h>
#include <rsys/hostnet.h>
#include <time/syncint.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <emscripten.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <strings.h>

using namespace Executor;

/* ---- the WebSocket (executor/web/net.js) ---------------------------- */

EM_JS(int, js_net_available, (), {
    return Module.net && Module.net.url ? 1 : 0;
});

/* 0 closed (or never opened), 1 opening, 2 open, 3 failed. */
EM_JS(int, js_net_state, (), {
    return Module.net ? Module.net.state : 3;
});

EM_JS(void, js_net_open, (), {
    if (Module.net)
        Module.net.open();
});

EM_JS(void, js_net_send, (const uint8_t *data, int len), {
    if (Module.net)
        Module.net.send(HEAPU8.slice(data, data + len));
});

/* The oldest message in buf: its length; 0 none; -n when it needs n bytes
 * (left queued). */
EM_JS(int, js_net_take, (uint8_t *buf, int max), {
    const q = Module.net ? Module.net.inbox : [];
    if (!q.length)
        return 0;
    const m = q[0];
    if (m.length > max)
        return -m.length;
    q.shift();
    HEAPU8.set(m, buf);
    return m.length;
});

namespace
{
/* The protocol (executor/server/relay.go has the same numbers). */
enum : uint8_t
{
    CONNECT = 0x01, UDP = 0x02, SEND = 0x03, SENDTO = 0x04, SHUTDOWN = 0x05, CLOSE = 0x06,
    CREDIT = 0x07, LOOKUP = 0x10, REVERSE = 0x11, QUERY = 0x12,
    HELLO = 0x80, CONNECTED = 0x81, DATA = 0x83, DATAFROM = 0x84, EOF_ = 0x85, ERROR = 0x86,
    ACK = 0x87, ADDRS = 0x90, NAME = 0x91, ANSWER = 0x92,
};
enum : uint8_t { kNoName = 1, kTryAgain = 2 };   /* lookups' codes (0: found) */

using Clock = hostidle::Clock;

/* Our addresses: lo, and the one interface the relay is behind. */
constexpr uint32_t kLoopback = 0x7F000001;
constexpr uint32_t kOurs = 0x0A00020F;         /* 10.0.2.15 */
constexpr uint32_t kOurMask = 0xFFFFFF00;
constexpr size_t kLocalBuffer = 256 * 1024;    /* a tab-local stream's unread bytes */
constexpr size_t kDatagrams = 64;              /* queued per UDP socket */
constexpr int kFirstFd = 3;
constexpr auto kLookupTime = std::chrono::seconds(10);
constexpr auto kRetry = std::chrono::seconds(5);   /* after the relay failed */

bool is_loopback(uint32_t a) { return (a >> 24) == 127; }
bool is_ours(uint32_t a) { return is_loopback(a) || a == kOurs || a == INADDR_ANY; }

struct Datagram
{
    uint32_t addr;
    uint16_t port;
    std::vector<uint8_t> data;
};

struct Sock
{
    int type;                       /* SOCK_STREAM, SOCK_DGRAM */
    bool nonblocking = false;
    bool reuse = false;
    bool linger0 = false;           /* SO_LINGER on, 0 s: close resets */
    uint32_t local_addr = 0;
    uint16_t local_port = 0;        /* 0: not bound */
    uint32_t peer_addr = 0;
    uint16_t peer_port = 0;
    bool has_peer = false;

    enum { idle, listening, connecting, connected } state = idle;
    int error = 0;                  /* SO_ERROR, and every call's once dead */
    bool dead = false;              /* reset, refused: nothing more moves */

    std::deque<uint8_t> rx;
    bool rx_eof = false;            /* the peer's FIN, after rx */
    bool shut_wr = false;

    /* Through the relay: its id (0: none); bytes sent not yet written by
     * it; bytes read not yet credited back. */
    uint32_t rid = 0;
    size_t in_flight = 0;
    size_t uncredited = 0;

    int pair = -1;                  /* a tab-local stream's other end */
    std::deque<int> backlog;        /* a listener's connections, not yet accepted */
    int backlog_max = 0;
    std::deque<Datagram> datagrams;

    std::map<std::pair<int, int>, std::vector<uint8_t>> options;
};

std::map<int, Sock> socks;
int next_fd = kFirstFd;
uint16_t next_port = 49152;

/* The relay connection. */
uint32_t next_rid = 1;
std::map<uint32_t, int> by_rid;     /* relay id -> descriptor */
size_t window = 64 * 1024;          /* HELLO's */
bool hello = false;
Clock::time_point failed_at;
bool ever_failed = false;

struct Lookup
{
    bool done = false;
    uint8_t code = 0;
    std::vector<uint8_t> answer;
};
std::map<uint32_t, Lookup> lookups;

struct WatchState;
std::vector<WatchState *> watchers;

Sock *find(int fd)
{
    auto it = socks.find(fd);
    if(it == socks.end())
    {
        errno = EBADF;
        return nullptr;
    }
    return &it->second;
}

int fail(int e)
{
    errno = e;
    return -1;
}

int code_errno(uint8_t code)
{
    switch(code)
    {
        case 1: return ECONNREFUSED;
        case 2: return ENETUNREACH;
        case 3: return EHOSTUNREACH;
        case 4: return ETIMEDOUT;
        case 5: return ECONNRESET;
        case 6: return EACCES;
        case 7: return EMFILE;
        default: return ECONNREFUSED;
    }
}

void put32(std::vector<uint8_t> &m, uint32_t v)
{
    for(int s = 24; s >= 0; s -= 8)
        m.push_back(v >> s);
}
void put16(std::vector<uint8_t> &m, uint16_t v)
{
    m.push_back(v >> 8);
    m.push_back(v);
}
uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
uint16_t get16(const uint8_t *p) { return p[0] << 8 | p[1]; }

std::vector<uint8_t> message(uint8_t op, uint32_t id)
{
    std::vector<uint8_t> m{ op };
    put32(m, id);
    return m;
}

void send_message(const std::vector<uint8_t> &m)
{
    js_net_send(m.data(), m.size());
}

void pump();

/* Yields to the browser until done() or the deadline; false at the deadline. */
template<typename F>
bool wait_until(F done, Clock::time_point deadline)
{
    static F *current;
    F *saved = current;
    current = &done;
    pump();
    bool ok = done();
    if(!ok)
    {
        hostidle::wait(deadline, [] { pump(); return (*current)(); });
        ok = done();
    }
    current = saved;
    return ok;
}

/* The relay, open (and HELLO heard), or false: none, or it failed lately. */
bool relay_ready()
{
    if(!js_net_available())
        return false;
    int st = js_net_state();
    if(st == 2 && hello)
        return true;
    if(st == 3 || st == 0)
    {
        if(ever_failed && Clock::now() - failed_at < kRetry)
            return false;
        hello = false;
        js_net_open();
    }
    bool ok = wait_until([] { return hello || js_net_state() == 3; }, Clock::now() + std::chrono::seconds(10));
    if(!ok || !hello)
    {
        ever_failed = true;
        failed_at = Clock::now();
        return false;
    }
    return true;
}

uint32_t relay_open(int fd, Sock &s)
{
    s.rid = next_rid++;
    by_rid[s.rid] = fd;
    return s.rid;
}

/* The relay is gone: its sockets are reset, its lookups fail. */
void relay_lost()
{
    for(auto &[rid, fd] : by_rid)
        if(Sock *s = find(fd))
        {
            s->dead = true;
            s->error = s->state == Sock::connecting ? ENETUNREACH : ECONNRESET;
            s->rid = 0;
        }
    by_rid.clear();
    for(auto &[id, l] : lookups)
        if(!l.done)
        {
            l.done = true;
            l.code = kTryAgain;
        }
    hello = false;
    ever_failed = true;
    failed_at = Clock::now();
}

void credit(Sock &s, bool all)
{
    if(!s.rid || !s.uncredited || (!all && s.uncredited < 4096 && !s.rx.empty()))
        return;
    auto m = message(CREDIT, s.rid);
    put32(m, s.uncredited);
    send_message(m);
    s.uncredited = 0;
}

void on_message(const uint8_t *p, size_t n)
{
    if(n < 5)
        return;
    uint8_t op = p[0];
    uint32_t id = get32(p + 1);
    p += 5;
    n -= 5;
    if(op == HELLO)
    {
        if(n >= 8)
            window = get32(p + 4);
        hello = true;
        return;
    }
    if(op >= ADDRS)
    {
        auto it = lookups.find(id);
        if(it != lookups.end() && n >= 1)
        {
            it->second.done = true;
            it->second.code = p[0];
            it->second.answer.assign(p + 1, p + n);
        }
        return;
    }
    auto it = by_rid.find(id);
    if(it == by_rid.end())
        return;
    Sock *s = find(it->second);
    if(!s)
        return;
    switch(op)
    {
        case CONNECTED:
            s->state = Sock::connected;
            break;
        case DATA:
            s->rx.insert(s->rx.end(), p, p + n);
            break;
        case DATAFROM:
            if(n >= 6 && s->datagrams.size() < kDatagrams)
                s->datagrams.push_back({ get32(p), get16(p + 4), std::vector<uint8_t>(p + 6, p + n) });
            break;
        case EOF_:
            s->rx_eof = true;
            break;
        case ERROR:
            s->dead = true;
            s->error = code_errno(n ? p[0] : 0);
            if(s->state == Sock::connecting)
                s->state = Sock::idle;
            break;
        case ACK:
            if(n >= 4)
                s->in_flight -= std::min<size_t>(s->in_flight, get32(p));
            break;
    }
}

int revents(int fd, short events);
size_t room(const Sock &s);

/* ---- the watchers ---- */
struct WatchState
{
    Interrupt *irq;
    std::vector<struct pollfd> fds;
    Clock::time_point deadline = Clock::time_point::max();
    bool armed = false;
};

void watch()
{
    for(WatchState *w : watchers)
    {
        if(!w->armed)
            continue;
        bool fire = Clock::now() >= w->deadline;
        for(auto &p : w->fds)
            fire |= revents(p.fd, p.events) != 0;
        if(fire)
        {
            w->armed = false;
            w->irq->trigger();
        }
    }
}

/* What came over the WebSocket, into the sockets; then the watchers. */
void pump()
{
    static std::vector<uint8_t> buf(80 * 1024);
    if(js_net_available())
    {
        for(;;)
        {
            int n = js_net_take(buf.data(), buf.size());
            if(n < 0)
            {
                buf.resize(-n);
                continue;
            }
            if(n == 0)
                break;
            on_message(buf.data(), n);
        }
        if((hello || !by_rid.empty()) && js_net_state() != 2)
            relay_lost();
    }
    watch();
}

int revents(int fd, short events)
{
    auto it = socks.find(fd);
    if(it == socks.end())
        return fd >= 0 ? POLLNVAL : 0;
    Sock &s = it->second;
    int r = 0;
    if(s.dead)
        r |= POLLERR | POLLHUP;
    if(s.state == Sock::listening)
        r |= s.backlog.empty() ? 0 : POLLIN;
    else if(s.type == SOCK_DGRAM)
        r |= (s.datagrams.empty() ? 0 : POLLIN) | POLLOUT;
    else
    {
        if(!s.rx.empty() || s.rx_eof || s.dead)
            r |= POLLIN;
        if(s.rx_eof && s.shut_wr)
            r |= POLLHUP;
        /* Writable: room, or a write would fail at once (the other end of
         * a tab-local stream gone). */
        bool broken = !s.rid && s.pair < 0;
        if(s.dead || (s.state == Sock::connected && !s.shut_wr && (room(s) || broken)))
            r |= POLLOUT;
    }
    return r & (events | POLLERR | POLLHUP | POLLNVAL);
}

uint16_t ephemeral(int type)
{
    for(int tries = 0; tries < 16384; tries++)
    {
        uint16_t p = next_port++;
        if(next_port == 0)
            next_port = 49152;
        bool used = false;
        for(auto &[fd, s] : socks)
            used |= s.type == type && s.local_port == p;
        if(!used)
            return p;
    }
    return 0;
}

/* The tab's socket bound to port that answers for addr, or -1. */
int local_for(int type, uint32_t addr, uint16_t port, bool listener)
{
    for(auto &[fd, s] : socks)
        if(s.type == type && s.local_port == port && !s.dead
           && (s.local_addr == INADDR_ANY || s.local_addr == addr
               || (is_loopback(addr) && is_loopback(s.local_addr)))
           && (!listener || s.state == Sock::listening))
            return fd;
    return -1;
}

void fill_addr(struct sockaddr *a, socklen_t *len, uint32_t addr, uint16_t port)
{
    if(!a || !len)
        return;
    struct sockaddr_in sin = {};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = htonl(addr);
    memcpy(a, &sin, std::min<socklen_t>(*len, sizeof sin));
    *len = sizeof sin;
}

bool read_addr(const struct sockaddr *a, socklen_t len, uint32_t &addr, uint16_t &port)
{
    if(!a || len < (socklen_t)sizeof(struct sockaddr_in) || a->sa_family != AF_INET)
        return false;
    auto *sin = (const struct sockaddr_in *)a;
    addr = ntohl(sin->sin_addr.s_addr);
    port = ntohs(sin->sin_port);
    return true;
}

/* Bound to a port: an ephemeral one if not yet. */
void bind_port(Sock &s)
{
    if(!s.local_port)
        s.local_port = ephemeral(s.type);
}

/* The address a peer sees: the bound one, else the interface toward it. */
uint32_t source(const Sock &s)
{
    if(s.local_addr != INADDR_ANY || !s.has_peer)
        return s.local_addr;
    return is_loopback(s.peer_addr) ? kLoopback : kOurs;
}

/* Room to write: what the relay or the other end will take. */
size_t room(const Sock &s)
{
    if(s.rid)
        return window - std::min(window, s.in_flight);
    auto it = socks.find(s.pair);
    return it == socks.end() ? 0 : kLocalBuffer - std::min(kLocalBuffer, it->second.rx.size());
}

/* Blocking sockets wait here for what non-blocking ones would get EAGAIN for. */
template<typename F>
bool block(Sock *&s, int fd, F ready)
{
    if(s->nonblocking)
        return false;
    wait_until([&] { return !(s = find(fd)) || ready(*s); }, Clock::time_point::max());
    return s != nullptr;
}

ssize_t stream_send(int fd, Sock *s, const uint8_t *data, size_t len)
{
    for(;;)
    {
        if(s->dead)
            return fail(s->error ? s->error : ECONNRESET);
        if(s->shut_wr)
            return fail(EPIPE);
        if(s->state != Sock::connected)
        {
            if(s->state != Sock::connecting)
                return fail(ENOTCONN);
            if(block(s, fd, [](Sock &x) { return x.state != Sock::connecting || x.dead; }))
                continue;
            return fail(s ? EAGAIN : EBADF);
        }
        Sock *peer = s->rid ? nullptr : find(s->pair);
        if(!s->rid && !peer)
            return fail(EPIPE);
        size_t n = std::min(len, room(*s));
        if(n == 0)
        {
            if(block(s, fd, [](Sock &x) { return room(x) > 0 || x.dead || x.shut_wr || (!x.rid && x.pair < 0); }))
                continue;
            return fail(s ? EAGAIN : EBADF);
        }
        if(s->rid)
        {
            for(size_t at = 0; at < n; at += 16384)
            {
                auto m = message(SEND, s->rid);
                m.insert(m.end(), data + at, data + std::min(n, at + 16384));
                send_message(m);
            }
            s->in_flight += n;
        }
        else
            peer->rx.insert(peer->rx.end(), data, data + n);
        watch();
        return n;
    }
}

ssize_t dgram_send(int fd, Sock *s, const uint8_t *data, size_t len, uint32_t addr, uint16_t port)
{
    if(len > 65507)
        return fail(EMSGSIZE);
    bind_port(*s);
    if(is_ours(addr))
    {
        int to = local_for(SOCK_DGRAM, addr, port, false);
        if(to >= 0)
        {
            Sock *t = find(to);
            if(t->datagrams.size() < kDatagrams)
                t->datagrams.push_back({ is_loopback(addr) ? kLoopback : kOurs, s->local_port,
                                         std::vector<uint8_t>(data, data + len) });
            watch();
            return len;
        }
        if(!is_loopback(addr))
            return len;              /* nobody here on that port: lost */
    }
    if(!relay_ready())
        return fail(ENETUNREACH);
    if(!s->rid)
        send_message(message(UDP, relay_open(fd, *s)));
    auto m = message(SENDTO, s->rid);
    put32(m, addr);
    put16(m, port);
    m.insert(m.end(), data, data + len);
    send_message(m);
    return len;
}

ssize_t do_send(int fd, const uint8_t *data, size_t len, const struct sockaddr *to, socklen_t tolen)
{
    Sock *s = find(fd);
    if(!s)
        return -1;
    if(s->type == SOCK_STREAM)
        return stream_send(fd, s, data, len);
    uint32_t addr = s->peer_addr;
    uint16_t port = s->peer_port;
    if(to && !read_addr(to, tolen, addr, port))
        return fail(EINVAL);
    if(!to && !s->has_peer)
        return fail(EDESTADDRREQ);
    return dgram_send(fd, s, data, len, addr, port);
}

ssize_t do_recv(int fd, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    Sock *s = find(fd);
    if(!s)
        return -1;
    bool peek = flags & MSG_PEEK;
    bool dontwait = flags & MSG_DONTWAIT;
    if(s->type == SOCK_DGRAM)
    {
        if(s->datagrams.empty())
        {
            if(dontwait || !block(s, fd, [](Sock &x) { return !x.datagrams.empty() || x.dead; }))
                return fail(EAGAIN);
            if(s->datagrams.empty())
                return fail(s->error ? s->error : EAGAIN);
        }
        Datagram &d = s->datagrams.front();
        size_t n = std::min(len, d.data.size());
        if(buf)
            memcpy(buf, d.data.data(), n);
        fill_addr(from, fromlen, d.addr, d.port);
        ssize_t r = (flags & MSG_TRUNC) ? (ssize_t)d.data.size() : (ssize_t)n;
        if(!peek)
            s->datagrams.pop_front();
        return r;
    }
    for(;;)
    {
        if(!s->rx.empty())
        {
            size_t n = std::min(len, s->rx.size());
            std::copy_n(s->rx.begin(), n, (uint8_t *)buf);
            if(!peek)
            {
                s->rx.erase(s->rx.begin(), s->rx.begin() + n);
                if(s->rid)
                {
                    s->uncredited += n;
                    credit(*s, false);
                }
                watch();                 /* the writer's room, a local pair's */
            }
            fill_addr(from, fromlen, s->peer_addr, s->peer_port);
            return n;
        }
        if(s->dead)
        {
            int e = s->error ? s->error : ECONNRESET;
            return fail(e);
        }
        if(s->rx_eof)
            return 0;
        if(s->state != Sock::connected && s->state != Sock::connecting)
            return fail(ENOTCONN);
        if(dontwait || !block(s, fd, [](Sock &x) { return !x.rx.empty() || x.rx_eof || x.dead; }))
            return fail(EAGAIN);
    }
}

/* A tab-local stream's end goes: the other end reads EOF (or a reset). */
void unpair(Sock &s, bool reset)
{
    Sock *peer = s.pair >= 0 ? find(s.pair) : nullptr;
    if(peer)
    {
        peer->rx_eof = true;
        if(reset)
        {
            peer->dead = true;
            peer->error = ECONNRESET;
        }
        peer->pair = -1;
    }
    s.pair = -1;
}

struct ifaddrs *new_ifaddr(const char *name, uint32_t addr, uint32_t mask, unsigned flags, struct ifaddrs *next)
{
    struct Block
    {
        struct ifaddrs ifa;
        struct sockaddr_in addr, mask;
        char name[8];
    };
    auto *b = (Block *)calloc(1, sizeof(Block));
    b->addr.sin_family = b->mask.sin_family = AF_INET;
    b->addr.sin_addr.s_addr = htonl(addr);
    b->mask.sin_addr.s_addr = htonl(mask);
    snprintf(b->name, sizeof b->name, "%s", name);
    b->ifa.ifa_next = next;
    b->ifa.ifa_name = b->name;
    b->ifa.ifa_flags = flags;
    b->ifa.ifa_addr = (struct sockaddr *)&b->addr;
    b->ifa.ifa_netmask = (struct sockaddr *)&b->mask;
    return &b->ifa;
}

/* A lookup over the relay: its code (0: found) and answer. */
uint8_t relay_lookup(uint8_t op, const std::vector<uint8_t> &body, std::vector<uint8_t> &answer)
{
    if(!relay_ready())
        return 3;
    uint32_t id = next_rid++;
    lookups[id];
    auto m = message(op, id);
    m.insert(m.end(), body.begin(), body.end());
    send_message(m);
    wait_until([id] { return lookups[id].done; }, Clock::now() + kLookupTime);
    Lookup l = lookups[id];
    lookups.erase(id);
    if(!l.done)
        return kTryAgain;
    answer = std::move(l.answer);
    return l.code;
}
}

/* Each yield to the browser (host/wasm/idle.cpp). */
extern "C" void executor_hostnet_pump() { pump(); }

/* ---- the calls ------------------------------------------------------- */

bool hostnet::available() { return js_net_available(); }

int hostnet::socket(int domain, int type, int protocol)
{
    type &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
    if(domain != AF_INET)
        return fail(EAFNOSUPPORT);
    if(type != SOCK_STREAM && type != SOCK_DGRAM)
        return fail(EPROTONOSUPPORT);
    (void)protocol;
    while(socks.count(next_fd))
        next_fd++;
    int fd = next_fd++;
    socks[fd].type = type;
    return fd;
}

int hostnet::close(int fd)
{
    Sock *s = find(fd);
    if(!s)
        return -1;
    if(s->rid)
    {
        auto m = message(CLOSE, s->rid);
        m.push_back(s->linger0 ? 1 : 0);
        send_message(m);
        by_rid.erase(s->rid);
    }
    unpair(*s, s->linger0);
    for(int c : s->backlog)
    {
        if(Sock *x = find(c))
            unpair(*x, true);
        socks.erase(c);
    }
    socks.erase(fd);
    watch();
    return 0;
}

int hostnet::connect(int fd, const struct sockaddr *a, socklen_t n)
{
    Sock *s = find(fd);
    if(!s)
        return -1;
    uint32_t addr;
    uint16_t port;
    if(!read_addr(a, n, addr, port))
        return fail(EAFNOSUPPORT);
    if(s->type == SOCK_DGRAM)
    {
        /* Only the default peer; nothing is sent. */
        s->peer_addr = addr;
        s->peer_port = port;
        s->has_peer = true;
        bind_port(*s);
        return 0;
    }
    if(s->state == Sock::connecting)
        return fail(EALREADY);
    if(s->state != Sock::idle || s->dead)
        return fail(s->dead ? s->error : EISCONN);
    if(addr == INADDR_ANY)
        addr = kLoopback;
    s->peer_addr = addr;
    s->peer_port = port;
    s->has_peer = true;
    bind_port(*s);

    /* A listener of the tab's: a pair here. */
    int lfd = is_ours(addr) ? local_for(SOCK_STREAM, addr, port, true) : -1;
    if(lfd >= 0)
    {
        Sock *l = find(lfd);
        if((int)l->backlog.size() >= std::max(l->backlog_max, 1))
            return fail(ECONNREFUSED);
        int afd = hostnet::socket(AF_INET, SOCK_STREAM, 0);
        s = find(fd);
        l = find(lfd);
        Sock &acc = socks[afd];
        acc.local_addr = is_loopback(addr) ? addr : kOurs;
        acc.local_port = port;
        acc.peer_addr = source(*s);
        acc.peer_port = s->local_port;
        acc.has_peer = true;
        acc.state = Sock::connected;
        acc.pair = fd;
        s->pair = afd;
        s->state = Sock::connected;
        l->backlog.push_back(afd);
        watch();
        return 0;
    }
    if(is_ours(addr) && !is_loopback(addr))
        return fail(ECONNREFUSED);
    /* Anything else, and loopback nobody here listens on: the relay's host. */
    if(!relay_ready())
        return fail(ENETUNREACH);
    s = find(fd);
    auto m = message(CONNECT, relay_open(fd, *s));
    put32(m, addr);
    put16(m, port);
    send_message(m);
    s->state = Sock::connecting;
    if(s->nonblocking)
        return fail(EINPROGRESS);
    block(s, fd, [](Sock &x) { return x.state != Sock::connecting || x.dead; });
    if(!s)
        return fail(EBADF);
    if(s->dead)
        return fail(s->error);
    return 0;
}

int hostnet::bind(int fd, const struct sockaddr *a, socklen_t n)
{
    Sock *s = find(fd);
    if(!s)
        return -1;
    uint32_t addr;
    uint16_t port;
    if(!read_addr(a, n, addr, port))
        return fail(EAFNOSUPPORT);
    if(s->local_port)
        return fail(EINVAL);
    if(addr != INADDR_ANY && !is_ours(addr))
        return fail(EADDRNOTAVAIL);
    if(port)
        for(auto &[f, x] : socks)
            if(f != fd && x.type == s->type && x.local_port == port
               && (x.local_addr == addr || x.local_addr == INADDR_ANY || addr == INADDR_ANY)
               && !(s->reuse && x.state != Sock::listening))
                return fail(EADDRINUSE);
    s->local_addr = addr;
    s->local_port = port ? port : ephemeral(s->type);
    return 0;
}

int hostnet::listen(int fd, int backlog)
{
    Sock *s = find(fd);
    if(!s)
        return -1;
    if(s->type != SOCK_STREAM || s->state == Sock::connecting || s->state == Sock::connected)
        return fail(EINVAL);
    if(!s->local_port)
        s->local_port = ephemeral(SOCK_STREAM);
    s->state = Sock::listening;
    s->backlog_max = std::clamp(backlog, 1, 64);
    return 0;
}

int hostnet::accept(int fd, struct sockaddr *a, socklen_t *n)
{
    Sock *s = find(fd);
    if(!s)
        return -1;
    if(s->state != Sock::listening)
        return fail(EINVAL);
    if(s->backlog.empty()
       && !block(s, fd, [](Sock &x) { return !x.backlog.empty(); }))
        return fail(EAGAIN);
    int c = s->backlog.front();
    s->backlog.pop_front();
    Sock *x = find(c);
    fill_addr(a, n, x->peer_addr, x->peer_port);
    return c;
}

int hostnet::shutdown(int fd, int how)
{
    Sock *s = find(fd);
    if(!s)
        return -1;
    if(s->type != SOCK_STREAM || s->state != Sock::connected)
        return fail(ENOTCONN);
    if((how == SHUT_WR || how == SHUT_RDWR) && !s->shut_wr)
    {
        s->shut_wr = true;
        if(s->rid)
            send_message(message(SHUTDOWN, s->rid));
        else if(Sock *peer = find(s->pair))
            peer->rx_eof = true;
        watch();
    }
    return 0;
}

ssize_t hostnet::send(int fd, const void *b, size_t n, int)
{
    return do_send(fd, (const uint8_t *)b, n, nullptr, 0);
}

ssize_t hostnet::sendto(int fd, const void *b, size_t n, int, const struct sockaddr *to, socklen_t tl)
{
    return do_send(fd, (const uint8_t *)b, n, to, tl);
}

ssize_t hostnet::sendmsg(int fd, const struct msghdr *m, int)
{
    std::vector<uint8_t> all;
    for(size_t i = 0; i < (size_t)m->msg_iovlen; i++)
        all.insert(all.end(), (uint8_t *)m->msg_iov[i].iov_base,
                   (uint8_t *)m->msg_iov[i].iov_base + m->msg_iov[i].iov_len);
    return do_send(fd, all.data(), all.size(), (const struct sockaddr *)m->msg_name, m->msg_namelen);
}

ssize_t hostnet::writev(int fd, const struct iovec *iov, int count)
{
    struct msghdr m = {};
    m.msg_iov = (struct iovec *)iov;
    m.msg_iovlen = count;
    return hostnet::sendmsg(fd, &m, 0);
}

ssize_t hostnet::recv(int fd, void *b, size_t n, int f) { return do_recv(fd, b, n, f, nullptr, nullptr); }

ssize_t hostnet::recvfrom(int fd, void *b, size_t n, int f, struct sockaddr *from, socklen_t *fl)
{
    return do_recv(fd, b, n, f, from, fl);
}

int hostnet::setsockopt(int fd, int level, int name, const void *v, socklen_t n)
{
    Sock *s = find(fd);
    if(!s)
        return -1;
    int on = n >= (socklen_t)sizeof(int) ? *(const int *)v : 0;
    if(level == SOL_SOCKET && name == SO_REUSEADDR)
        s->reuse = on;
    if(level == SOL_SOCKET && name == SO_LINGER && n >= (socklen_t)sizeof(struct linger))
    {
        auto *l = (const struct linger *)v;
        s->linger0 = l->l_onoff && !l->l_linger;
    }
    /* The rest are kept to be read back: the relay's sockets have their
     * host's defaults (TCP_NODELAY on). */
    s->options[{ level, name }].assign((const uint8_t *)v, (const uint8_t *)v + n);
    return 0;
}

int hostnet::getsockopt(int fd, int level, int name, void *v, socklen_t *n)
{
    Sock *s = find(fd);
    if(!s)
        return -1;
    if(level == SOL_SOCKET && name == SO_ERROR)
    {
        int e = s->error;
        if(!s->dead)
            s->error = 0;
        memcpy(v, &e, std::min<socklen_t>(*n, sizeof e));
        *n = sizeof e;
        return 0;
    }
    auto it = s->options.find({ level, name });
    std::vector<uint8_t> value;
    if(it != s->options.end())
        value = it->second;
    else
    {
        int d = 0;
        if(level == SOL_SOCKET && (name == SO_SNDBUF || name == SO_RCVBUF))
            d = window;
        else if(level == SOL_SOCKET && name == SO_TYPE)
            d = s->type;
        else if(level == IPPROTO_TCP && name == TCP_NODELAY)
            d = 1;
#ifdef TCP_KEEPIDLE
        else if(level == IPPROTO_TCP && name == TCP_KEEPIDLE)
            d = 7200;
#endif
        else if(level == IPPROTO_IP && name == IP_TTL)
            d = 64;
        value.assign((uint8_t *)&d, (uint8_t *)&d + sizeof d);
    }
    value.resize(std::max<size_t>(value.size(), std::min<socklen_t>(*n, sizeof(int))));
    memcpy(v, value.data(), std::min<size_t>(*n, value.size()));
    *n = std::min<size_t>(*n, value.size());
    return 0;
}

int hostnet::getsockname(int fd, struct sockaddr *a, socklen_t *n)
{
    Sock *s = find(fd);
    if(!s)
        return -1;
    fill_addr(a, n, source(*s), s->local_port);
    return 0;
}

int hostnet::getpeername(int fd, struct sockaddr *a, socklen_t *n)
{
    Sock *s = find(fd);
    if(!s)
        return -1;
    if(!s->has_peer || (s->type == SOCK_STREAM && s->state != Sock::connected))
        return fail(ENOTCONN);
    fill_addr(a, n, s->peer_addr, s->peer_port);
    return 0;
}

int hostnet::set_nonblocking(int fd)
{
    Sock *s = find(fd);
    if(!s)
        return -1;
    s->nonblocking = true;
    return 0;
}

int hostnet::bytes_available(int fd)
{
    pump();
    Sock *s = find(fd);
    if(!s)
        return -1;
    if(s->type == SOCK_DGRAM)
        return s->datagrams.empty() ? 0 : s->datagrams.front().data.size();
    return s->rx.size();
}

ssize_t hostnet::next_datagram_size(int fd)
{
    pump();
    Sock *s = find(fd);
    if(!s)
        return -1;
    if(s->type != SOCK_DGRAM || s->datagrams.empty())
        return fail(s->dead ? s->error : EAGAIN);   /* as a peeking recv would */
    return s->datagrams.front().data.size();
}

int hostnet::poll(struct pollfd *fds, nfds_t count, int timeout_ms)
{
    auto ready = [fds, count] {
        int n = 0;
        for(nfds_t i = 0; i < count; i++)
        {
            fds[i].revents = revents(fds[i].fd, fds[i].events);
            n += fds[i].revents != 0;
        }
        return n;
    };
    pump();
    int n = ready();
    if(n || timeout_ms == 0)
        return n;
    auto deadline = timeout_ms < 0 ? Clock::time_point::max()
                                   : Clock::now() + std::chrono::milliseconds(timeout_ms);
    wait_until([&] { return ready() != 0; }, deadline);
    return ready();
}

int hostnet::getifaddrs(struct ifaddrs **list)
{
    struct ifaddrs *net = new_ifaddr("relay0", kOurs, kOurMask, IFF_UP | IFF_RUNNING | IFF_BROADCAST, nullptr);
    *list = new_ifaddr("lo", kLoopback, 0xFF000000, IFF_UP | IFF_RUNNING | IFF_LOOPBACK, net);
    return 0;
}

void hostnet::freeifaddrs(struct ifaddrs *list)
{
    while(list)
    {
        struct ifaddrs *next = list->ifa_next;
        free(list);
        list = next;
    }
}

int hostnet::getaddrinfo(const char *node, const char *service, const struct addrinfo *hints,
                         struct addrinfo **result)
{
    *result = nullptr;
    if(hints && hints->ai_family != AF_UNSPEC && hints->ai_family != AF_INET)
        return EAI_FAMILY;
    uint16_t port = service ? (uint16_t)atoi(service) : 0;
    std::vector<uint32_t> addrs;
    struct in_addr in;
    if(!node || !*node)
        addrs.push_back(hints && (hints->ai_flags & AI_PASSIVE) ? INADDR_ANY : kLoopback);
    else if(inet_aton(node, &in))
        addrs.push_back(ntohl(in.s_addr));
    else if(!strcasecmp(node, "localhost"))
        addrs.push_back(kLoopback);
    else if(hints && (hints->ai_flags & AI_NUMERICHOST))
        return EAI_NONAME;
    else
    {
        std::vector<uint8_t> answer;
        uint8_t code = relay_lookup(LOOKUP, std::vector<uint8_t>(node, node + strlen(node)), answer);
        if(code)
            return code == kNoName ? EAI_NONAME : code == kTryAgain ? EAI_AGAIN : EAI_FAIL;
        for(size_t i = 0; i + 4 <= answer.size(); i += 4)
            addrs.push_back(get32(&answer[i]));
        if(addrs.empty())
            return EAI_NONAME;
    }
    int socktype = hints && hints->ai_socktype ? hints->ai_socktype : SOCK_STREAM;
    struct addrinfo **tail = result;
    for(uint32_t a : addrs)
    {
        struct Block
        {
            struct addrinfo ai;
            struct sockaddr_in sin;
        };
        auto *b = (Block *)calloc(1, sizeof(Block));
        b->sin.sin_family = AF_INET;
        b->sin.sin_port = htons(port);
        b->sin.sin_addr.s_addr = htonl(a);
        b->ai.ai_family = AF_INET;
        b->ai.ai_socktype = socktype;
        b->ai.ai_protocol = socktype == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP;
        b->ai.ai_addrlen = sizeof b->sin;
        b->ai.ai_addr = (struct sockaddr *)&b->sin;
        *tail = &b->ai;
        tail = &b->ai.ai_next;
    }
    return 0;
}

void hostnet::freeaddrinfo(struct addrinfo *result)
{
    while(result)
    {
        struct addrinfo *next = result->ai_next;
        free(result);
        result = next;
    }
}

int hostnet::getnameinfo(const struct sockaddr *a, socklen_t n, char *host, socklen_t hostlen,
                         char *serv, socklen_t servlen, int flags)
{
    uint32_t addr;
    uint16_t port;
    if(!read_addr(a, n, addr, port))
        return EAI_FAMILY;
    if(serv && servlen)
        snprintf(serv, servlen, "%u", port);
    if(!host || !hostlen)
        return 0;
    std::string name;
    if(!(flags & NI_NUMERICHOST))
    {
        if(is_loopback(addr))
            name = "localhost";
        else if(addr != kOurs)
        {
            std::vector<uint8_t> body, answer;
            put32(body, addr);
            uint8_t code = relay_lookup(REVERSE, body, answer);
            if(code == 0)
                name.assign(answer.begin(), answer.end());
            else if(flags & NI_NAMEREQD)
                return code == kTryAgain ? EAI_AGAIN : EAI_NONAME;
        }
        else if(flags & NI_NAMEREQD)
            return EAI_NONAME;
    }
    if(name.empty())
    {
        struct in_addr in;
        in.s_addr = htonl(addr);
        name = inet_ntoa(in);
    }
    if(name.size() + 1 > (size_t)hostlen)
        return EAI_OVERFLOW;
    memcpy(host, name.c_str(), name.size() + 1);
    return 0;
}

int hostnet::res_query(const char *name, int cls, int type, unsigned char *answer, int len)
{
    std::vector<uint8_t> body, reply;
    put16(body, cls);
    put16(body, type);
    body.insert(body.end(), name, name + strlen(name));
    uint8_t code = relay_lookup(QUERY, body, reply);
    if(code)
    {
        h_errno = code == kTryAgain ? TRY_AGAIN : NO_RECOVERY;
        return -1;
    }
    /* What res_query reports from the answer's header: no such name, no
     * such records. */
    if(reply.size() >= 12)
    {
        int rcode = reply[3] & 15, count = get16(&reply[6]);
        if(rcode == 3)
        {
            h_errno = HOST_NOT_FOUND;
            return -1;
        }
        if(rcode == 0 && count == 0)
        {
            h_errno = NO_DATA;
            return -1;
        }
        if(rcode != 0)
        {
            h_errno = rcode == 2 ? TRY_AGAIN : NO_RECOVERY;
            return -1;
        }
    }
    memcpy(answer, reply.data(), std::min<size_t>(len, reply.size()));
    return reply.size();
}

/* No threads in the tab: the work runs now, waiting (yielding) for the
 * relay as it needs. */
void hostnet::run_detached(std::function<void()> work)
{
    work();
}

/* ---- the watcher: looked at after every yield and every change ----- */

struct hostnet::Watcher::State : WatchState
{
};

hostnet::Watcher::Watcher(Interrupt &irq) : irq_(irq) {}

void hostnet::Watcher::set(std::vector<struct pollfd> fds, Clock::time_point deadline)
{
    if(!state_)
    {
        if(fds.empty() && deadline == Clock::time_point::max())
            return;
        state_ = new State;
        state_->irq = &irq_;
        watchers.push_back(state_);
    }
    state_->fds = std::move(fds);
    state_->deadline = deadline;
    state_->armed = true;    /* looked at from the next yield on */
}
