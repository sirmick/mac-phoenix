/* ot_provider.cpp — Open Transport's providers: endpoints on host sockets,
 * their modes, notifiers and events ("OTClnt$LclClnt").
 *
 * Model, as MacTCP's (mactcp/mactcp.cpp):
 *   - A ProviderRef is a cookie ('OT' and a number), never a pointer.
 *   - Sockets are non-blocking. A blocking-mode call waits in poll(2) on
 *     the 68k thread; a non-blocking one answers kOTNoDataErr (or
 *     kOTFlowErr) when it would wait.
 *   - What happens later (a connect completes, data or the peer's FIN
 *     arrives, a connection comes in, a send can go on) is noticed by a
 *     watcher thread that polls the sockets and raises an Executor
 *     interrupt; at interrupt time the pump sets what OTLook reports and
 *     delivers the events to the notifiers. OT called notifiers at
 *     deferred task time; this is the same for a client: never inside one
 *     of its OT calls, never re-entered.
 *   - Completions of asynchronous calls (T_BINDCOMPLETE, T_OPENCOMPLETE,
 *     ...) are queued and delivered the same way, after the call returned.
 *   - Notifiers are pascal (context, code, result, cookie), called with the
 *     A5 world the client had when it installed them.
 *   - T_DATA is sent once for what is waiting, and again only after OTRcv
 *     answered kOTNoDataErr (OT's rule).
 */

#include "ot_provider.h"
#include "ot_client.h"

#include <MemoryMgr.h>
#include <time/syncint.h>
#include <rsys/opentransport.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

using namespace Executor;
using namespace Executor::ot;

namespace
{

/* ---- providers -------------------------------------------------------- */

constexpr uint32_t REF_BASE = 0x4F540000; /* 'OT' */
uint32_t next_ref = 2;
std::map<uint32_t, Provider> providers;

struct Event
{
    uint32_t ref, code;
    int32_t result;
    uint32_t cookie;
};
std::deque<Event> events;

std::vector<std::function<void()>> pump_hooks;

void rewatch();
bool receiving(const Provider &p);
void peer_closed(Provider &p);

int set_nonblocking(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    return fl < 0 ? fl : fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

/* The start of what went over a socket, for --executor-debug ot. */
void log_bytes(const char *what, uint32_t ref, const uint8_t *p, size_t n)
{
    if(!log_enabled() || !n)
        return;
    std::string s;
    for(size_t i = 0; i < n && i < 160; i++)
        s += p[i] >= 32 && p[i] < 127 ? (char)p[i] : p[i] == '\r' ? '|' : p[i] == '\n' ? '~' : '.';
    fprintf(stderr, "[ot] %s %08x: \"%s\"%s\n", what, ref, s.c_str(), n > 160 ? "..." : "");
    if(n > 160)
    {
        std::string t;
        for(size_t i = n > 320 ? n - 160 : 160; i < n; i++)
            t += p[i] >= 32 && p[i] < 127 ? (char)p[i] : p[i] == '\r' ? '|' : p[i] == '\n' ? '~' : '.';
        fprintf(stderr, "[ot] %s %08x ... \"%s\"\n", what, ref, t.c_str());
    }
}

/* errno -> OT error. */
int32_t ot_errno(int e)
{
    switch(e)
    {
        case 0:
            return kOTNoError;
        case ECONNREFUSED:
            return kECONNREFUSEDErr;
        case EHOSTUNREACH:
        case ENETUNREACH:
            return kEHOSTUNREACHErr;
        case ETIMEDOUT:
            return kETIMEDOUTErr;
        case EADDRINUSE:
            return kOTAddressBusyErr;
        case EADDRNOTAVAIL:
            return kOTBadAddressErr;
        case EACCES:
            return kOTAccessErr;
        case ENOMEM:
        case ENOBUFS:
            return kOTOutOfMemoryErr;
        default:
            return kOTSysErrorErr;
    }
}

/* ---- the watcher ------------------------------------------------------ */

using Clock = std::chrono::steady_clock;

std::mutex watch_mutex;
std::condition_variable watch_cv;
std::vector<struct pollfd> watch_fds;
uint64_t watch_gen = 0, handled_gen = 0;
int wake_pipe[2] = { -1, -1 };
bool watcher_started = false;

void pump();
Interrupt ot_interrupt{ [] { pump(); } };

void watcher_main()
{
    for(;;)
    {
        std::vector<struct pollfd> fds;
        uint64_t gen;
        {
            std::unique_lock<std::mutex> lock(watch_mutex);
            watch_cv.wait(lock, [] { return watch_gen != handled_gen; });
            fds = watch_fds;
            gen = watch_gen;
        }
        fds.push_back({ wake_pipe[0], POLLIN, 0 });
        poll(fds.data(), fds.size(), -1);
        if(fds.back().revents)
        {
            /* rewatch(): a new set to wait for. */
            char buf[64];
            while(read(wake_pipe[0], buf, sizeof buf) > 0)
                ;
        }
        bool fire = false;
        for(size_t i = 0; i + 1 < fds.size(); i++)
            fire |= fds[i].revents != 0;
        if(fire)
        {
            {
                std::lock_guard<std::mutex> lock(watch_mutex);
                handled_gen = gen;
            }
            ot_interrupt.trigger();
        }
    }
}

void start_watcher()
{
    if(watcher_started)
        return;
    if(pipe(wake_pipe) < 0)
        return;
    set_nonblocking(wake_pipe[0]);
    set_nonblocking(wake_pipe[1]);
    std::thread(watcher_main).detach();
    watcher_started = true;
}

/* What the watcher waits for, after every call and every pump. */
void rewatch()
{
    std::vector<struct pollfd> fds;
    for(auto &[ref, p] : providers)
    {
        if(p.fd < 0)
            continue;
        short ev = 0;
        if(p.connecting)
            ev = POLLOUT;
        else if(p.listener)
        {
            if(p.listen_armed)
                ev = POLLIN;
        }
        else if(receiving(p))
        {
            if(p.data_armed && !p.eof_seen && !p.look)
                ev |= POLLIN;
            if(p.flow_blocked)
                ev |= POLLOUT;
        }
        if(ev)
            fds.push_back({ p.fd, ev, 0 });
    }
    start_watcher();
    {
        std::lock_guard<std::mutex> lock(watch_mutex);
        watch_fds = std::move(fds);
        watch_gen++;
    }
    watch_cv.notify_all();
    char c = 0;
    (void)!write(wake_pipe[1], &c, 1); /* out of a poll on the old set */
}

bool ready_now(int fd, short events)
{
    struct pollfd pf = { fd, events, 0 };
    return fd >= 0 && poll(&pf, 1, 0) > 0;
}

/* ---- notifiers -------------------------------------------------------- */

void call_notifier(Provider &p, uint32_t code, int32_t result, uint32_t cookie)
{
    uint32_t proc = p.notifier, ctx = p.context, a5 = p.a5, ref = p.ref;
    OT_LOG("notify %08x code $%08x result %d cookie $%08x\n", ref, code, result, cookie);
    p.in_notifier = true;
    uint32_t saved_a7 = EM_A7, saved_a5 = EM_A5;
    if(a5)
        EM_A5 = a5;
    EM_A7 -= 16;
    WRITEUL(EM_A7 + 12, ctx);
    WRITEUL(EM_A7 + 8, code);
    WRITEUL(EM_A7 + 4, (uint32_t)result);
    WRITEUL(EM_A7, cookie);
    execute68K(proc);
    EM_A7 = saved_a7;
    EM_A5 = saved_a5;
    if(Provider *q = find_provider(ref)) /* it may have closed itself */
        q->in_notifier = false;
}

/* The peer's FIN: OTLook's T_ORDREL, and the notifier's, once. */
void peer_closed(Provider &p)
{
    if(p.eof_seen)
        return;
    p.eof_seen = true;
    p.look = T_ORDREL;
    post(p, T_ORDREL, kOTNoError, 0);
}

bool receiving(const Provider &p)
{
    if(p.kind == Kind::UDP)
        return p.state == T_IDLE;
    return p.state == T_DATAXFER || p.state == T_OUTREL;
}

/* What a provider's socket says now: connects finished, data, the peer's
   FIN, connections in, sends able to go on. Sets OTLook's event and
   queues the notifier's. */
void service(Provider &p)
{
    if(p.fd < 0)
        return;
    if(p.connecting)
    {
        if(!ready_now(p.fd, POLLOUT))
            return;
        int err = 0;
        socklen_t len = sizeof err;
        getsockopt(p.fd, SOL_SOCKET, SO_ERROR, &err, &len);
        p.connecting = false;
        if(err)
        {
            OT_LOG("connect %08x failed: %s\n", p.ref, strerror(err));
            close(p.fd);
            p.fd = -1;
            p.look = T_DISCONNECT;
            post(p, T_DISCONNECT, ot_errno(err), 0);
        }
        else
        {
            p.look = T_CONNECT;
            post(p, T_CONNECT, kOTNoError, 0);
        }
        return;
    }
    if(p.listener)
    {
        if(p.listen_armed && ready_now(p.fd, POLLIN))
        {
            p.listen_armed = false;
            p.look = T_LISTEN;
            post(p, T_LISTEN, kOTNoError, 0);
        }
        return;
    }
    /* Data only where it can come: a connected TCP endpoint, a bound UDP
       one. (An unconnected TCP socket polls as hung up.) */
    if(!receiving(p))
        return;
    if(p.flow_blocked && ready_now(p.fd, POLLOUT))
    {
        p.flow_blocked = false;
        post(p, T_GODATA, kOTNoError, 0);
    }
    if(!p.data_armed || p.look || !ready_now(p.fd, POLLIN))
        return;
    if(p.kind == Kind::TCP)
    {
        char c;
        ssize_t n = recv(p.fd, &c, 1, MSG_PEEK | MSG_DONTWAIT);
        if(n == 0)
        {
            peer_closed(p);
            return;
        }
        if(n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            p.look = T_DISCONNECT;
            post(p, T_DISCONNECT, ot_errno(errno), 0);
            return;
        }
        if(n < 0)
            return;
    }
    p.data_armed = false;
    post(p, T_DATA, kOTNoError, 0);
}

bool pumping = false;

void pump()
{
    if(pumping)
        return;
    pumping = true;
    interrupt_level++; /* deferred task time */
    for(auto &hook : pump_hooks)
        hook();
    std::vector<uint32_t> refs;
    for(auto &[ref, p] : providers)
        refs.push_back(ref);
    for(uint32_t ref : refs)
        if(Provider *p = find_provider(ref))
            service(*p);
    /* Deliver what's queued, oldest first; a notifier may queue more. */
    for(int guard = 0; !events.empty() && guard < 1000; guard++)
    {
        Event e = events.front();
        events.pop_front();
        Provider *p = find_provider(e.ref);
        if(!p || !p->notifier)
            continue;
        if(p->in_notifier)
        {
            events.push_back(e);
            break;
        }
        call_notifier(*p, e.code, e.result, e.cookie);
    }
    interrupt_level--;
    pumping = false;
    rewatch();
}

/* ---- addresses -------------------------------------------------------- */

/* A TNetbuf: maxlen, len, buf. */
struct Netbuf
{
    uint32_t at;
    uint32_t maxlen() const { return READUL(at); }
    uint32_t len() const { return READUL(at + 4); }
    uint32_t buf() const { return READUL(at + 8); }
    void set_len(uint32_t n) const { WRITEUL(at + 4, n); }
};

/* An InetAddress (16 bytes: type, port, host, 8 unused) from a netbuf. */
bool read_inet(Netbuf nb, struct sockaddr_in &sin)
{
    std::memset(&sin, 0, sizeof sin);
    sin.sin_family = AF_INET;
    if(!nb.buf() || nb.len() < 8)
        return false;
    uint32_t a = nb.buf();
    if(READUW(a) != 2) /* AF_INET */
        return false;
    sin.sin_port = htons(READUW(a + 2));
    sin.sin_addr.s_addr = htonl(READUL(a + 4));
    return true;
}

void write_inet(Netbuf nb, const struct sockaddr_in &sin)
{
    if(!nb.at || !nb.buf() || nb.maxlen() < 16)
    {
        if(nb.at)
            nb.set_len(0);
        return;
    }
    uint32_t a = nb.buf();
    WRITEUW(a, 2);
    WRITEUW(a + 2, ntohs(sin.sin_port));
    WRITEUL(a + 4, ntohl(sin.sin_addr.s_addr));
    WRITEUL(a + 8, 0);
    WRITEUL(a + 12, 0);
    nb.set_len(16);
}

/* A DNSAddress (AF_DNS, "name" or "name:port") or an InetAddress, from a
   netbuf, as a socket address; the name looked up on the host's resolver
   (blocking: OT's providers do the same for a synchronous endpoint). */
bool dns_name(Netbuf nb, std::string &name, uint16_t &port)
{
    if(!nb.buf() || nb.len() < 3 || READUW(nb.buf()) != 42) /* AF_DNS */
        return false;
    std::string s;
    for(uint32_t i = 2; i < nb.len() && i < 2 + 256; i++)
    {
        char ch = READUB(nb.buf() + i);
        if(!ch)
            break;
        s += ch;
    }
    port = 0;
    auto colon = s.rfind(':');
    if(colon != std::string::npos)
    {
        port = (uint16_t)atoi(s.c_str() + colon + 1);
        s.resize(colon);
    }
    name = s;
    return true;
}

int32_t read_address(Netbuf nb, struct sockaddr_in &sin)
{
    std::string name;
    uint16_t port;
    if(dns_name(nb, name, port))
    {
        std::memset(&sin, 0, sizeof sin);
        sin.sin_family = AF_INET;
        sin.sin_port = htons(port);
        uint32_t ip;
        if(int32_t e = inet_resolve(name, ip))
            return e;
        sin.sin_addr.s_addr = htonl(ip);
        return kOTNoError;
    }
    return read_inet(nb, sin) ? kOTNoError : kOTBadAddressErr;
}

/* ---- opening and closing ---------------------------------------------- */

Provider *lookup(const Call &c, int i = 0)
{
    return find_provider(c.l(i));
}

/* TEndpointInfo for tcp and udp. */
void fill_info(uint32_t info, Kind kind)
{
    if(!info)
        return;
    bool tcp = kind == Kind::TCP;
    WRITEUL(info + 0, 16);                        /* addr */
    WRITEUL(info + 4, 1024);                      /* options */
    WRITEUL(info + 8, tcp ? 0 : 65535 - 28);      /* tsdu: TCP is a stream */
    WRITEUL(info + 12, tcp ? (uint32_t)-1 : (uint32_t)-2); /* etsdu */
    WRITEUL(info + 16, (uint32_t)-2);             /* connect: no data */
    WRITEUL(info + 20, (uint32_t)-2);             /* discon */
    WRITEUL(info + 24, tcp ? 2 : 3);              /* T_COTS_ORD, T_CLTS */
    WRITEUL(info + 28, tcp ? 0 : 0);              /* flags */
}

/* Open an endpoint for a configuration path ("tcp", "udp", with options
   after a colon or comma, which are ignored). */
int32_t open_endpoint(uint32_t cfg, uint32_t info, uint32_t client, uint32_t &ref)
{
    std::string path;
    if(!config_path(cfg, path))
        return kOTBadConfigurationErr;
    dispose_config(cfg);
    std::string proto = path.substr(0, path.find_first_of(":,("));
    Kind kind;
    if(proto == "tcp")
        kind = Kind::TCP;
    else if(proto == "udp")
        kind = Kind::UDP;
    else
    {
        OT_LOG("open \"%s\": not a protocol here\n", path.c_str());
        return kOTBadConfigurationErr;
    }
    Provider &p = new_provider(client, kind);
    fill_info(info, kind);
    ref = p.ref;
    OT_LOG("open \"%s\" -> %08x\n", path.c_str(), ref);
    return kOTNoError;
}

/* EndpointRef OTOpenEndpointPriv(cfig, oflag, info, OSStatus *err, client) */
uint32_t open_endpoint_priv(const Call &c)
{
    uint32_t ref = 0;
    int32_t err = open_endpoint(c.l(0), c.l(2), c.l(4), ref);
    if(c.l(3))
        WRITEUL(c.l(3), (uint32_t)err);
    return ref;
}

/* OSStatus OTAsyncOpenEndpointPriv(cfig, oflag, info, proc, ctx, client) */
uint32_t async_open_endpoint_priv(const Call &c)
{
    uint32_t ref = 0;
    int32_t err = open_endpoint(c.l(0), c.l(2), c.l(5), ref);
    if(err)
        return err;
    Provider &p = *find_provider(ref);
    p.notifier = c.l(3);
    p.context = c.l(4);
    p.a5 = current_a5();
    p.async = true;
    post(p, T_OPENCOMPLETE, kOTNoError, ref);
    return kOTNoError;
}

/* OSStatus OTCloseProviderPriv(ref, client) */
uint32_t close_provider_priv(const Call &c)
{
    if(!lookup(c))
        return kOTBadReferenceErr;
    close_provider(c.l(0));
    return kOTNoError;
}

/* ---- modes and notifiers ---------------------------------------------- */

uint32_t set_async(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    p->async = true;
    OT_LOG("%08x asynchronous\n", p->ref);
    return kOTNoError;
}

uint32_t set_sync(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    p->async = false;
    OT_LOG("%08x synchronous\n", p->ref);
    return kOTNoError;
}

uint32_t is_sync(const Call &c)
{
    Provider *p = lookup(c);
    return p && !p->async;
}

uint32_t set_blocking(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    p->blocking = true;
    OT_LOG("%08x blocking\n", p->ref);
    return kOTNoError;
}

uint32_t set_nonblocking(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    p->blocking = false;
    OT_LOG("%08x non-blocking\n", p->ref);
    return kOTNoError;
}

uint32_t is_blocking(const Call &c)
{
    Provider *p = lookup(c);
    return p && p->blocking;
}

uint32_t ack_sends(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    p->ack_sends = true;
    return kOTNoError;
}

uint32_t dont_ack_sends(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    p->ack_sends = false;
    return kOTNoError;
}

uint32_t is_acking(const Call &c)
{
    Provider *p = lookup(c);
    return p && p->ack_sends;
}

/* OSStatus OTInstallNotifier(ref, proc, context) */
uint32_t install_notifier(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    p->notifier = c.l(1);
    p->context = c.l(2);
    p->a5 = current_a5();
    OT_LOG("%08x notifier $%08x context $%08x\n", p->ref, p->notifier, p->context);
    rewatch();
    return kOTNoError;
}

uint32_t remove_notifier(const Call &c)
{
    if(Provider *p = lookup(c))
        p->notifier = 0;
    return 0;
}

/* ---- endpoint state --------------------------------------------------- */

uint32_t get_endpoint_state(const Call &c)
{
    Provider *p = lookup(c);
    return p ? p->state : (uint32_t)kOTBadReferenceErr;
}

uint32_t look(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    service(*p); /* what the socket says now, not only at the last pump */
    if(!p->look && !p->udata_rest.empty())
        return T_DATA;
    if(!p->look && p->fd >= 0 && !p->listener && receiving(*p) && ready_now(p->fd, POLLIN))
        return T_DATA;
    return p->look;
}

uint32_t get_endpoint_info(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    fill_info(c.l(1), p->kind);
    return kOTNoError;
}

/* OTResult OTCountDataBytes(ref, size_t *count) */
uint32_t count_data_bytes(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    int n = 0;
    if(p->fd < 0 || ioctl(p->fd, FIONREAD, &n) < 0 || n == 0)
        return kOTNoDataErr;
    if(c.l(1))
        WRITEUL(c.l(1), n);
    return kOTNoError;
}

/* OSStatus OTGetProtAddress(ref, TBind *bound, TBind *peer) */
uint32_t get_prot_address(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    struct sockaddr_in sin = {};
    socklen_t len = sizeof sin;
    if(c.l(1))
    {
        sin = {};
        if(p->fd >= 0)
            getsockname(p->fd, (struct sockaddr *)&sin, &len);
        sin.sin_family = AF_INET;
        write_inet({ c.l(1) }, sin);
    }
    if(c.l(2))
    {
        sin = {};
        len = sizeof sin;
        if(p->fd >= 0 && !p->listener)
            getpeername(p->fd, (struct sockaddr *)&sin, &len);
        sin.sin_family = AF_INET;
        write_inet({ c.l(2) }, sin);
    }
    if(p->async)
        post(*p, T_GETPROTADDRCOMPLETE, kOTNoError, c.l(1));
    return kOTNoError;
}

/* ---- bind and unbind -------------------------------------------------- */

/* OSStatus OTBind(ref, TBind *req, TBind *ret) */
uint32_t bind_(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    if(p->state != T_UNBND)
        return kOTOutStateErr;
    uint32_t req = c.l(1), ret = c.l(2);
    struct sockaddr_in sin = {};
    sin.sin_family = AF_INET;
    bool have_addr = req && read_inet({ req }, sin);
    uint32_t qlen = req ? READUL(req + 12) : 0;

    int fd = socket(AF_INET, p->kind == Kind::UDP ? SOCK_DGRAM : SOCK_STREAM, 0);
    if(fd < 0)
        return ot_errno(errno);
    set_nonblocking(fd);
    int one = 1;
    if(p->kind == Kind::TCP)
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    /* A connecting endpoint binds where its connect goes; one that names
       an address or listens binds now. */
    if((have_addr && (sin.sin_port || sin.sin_addr.s_addr)) || qlen || p->kind == Kind::UDP)
    {
        if(::bind(fd, (struct sockaddr *)&sin, sizeof sin) < 0)
        {
            int e = errno;
            close(fd);
            OT_LOG("bind %08x port %u: %s\n", p->ref, ntohs(sin.sin_port), strerror(e));
            return e == EADDRINUSE ? kOTAddressBusyErr : ot_errno(e);
        }
    }
    if(qlen && p->kind == Kind::TCP)
    {
        if(listen(fd, std::max<int>(qlen, 1)) < 0)
        {
            int e = errno;
            close(fd);
            return ot_errno(e);
        }
        p->listener = true;
        p->qlen = qlen;
    }
    p->fd = fd;
    apply_pending_options(*p);
    p->state = T_IDLE;
    if(ret)
    {
        struct sockaddr_in got = {};
        socklen_t len = sizeof got;
        getsockname(fd, (struct sockaddr *)&got, &len);
        got.sin_family = AF_INET;
        write_inet({ ret }, got);
        WRITEUL(ret + 12, qlen);
    }
    OT_LOG("bind %08x%s\n", p->ref, qlen ? " (listening)" : "");
    if(p->async)
        post(*p, T_BINDCOMPLETE, kOTNoError, ret);
    rewatch();
    return kOTNoError;
}

void drop_socket(Provider &p)
{
    if(p.fd >= 0)
        close(p.fd);
    p.fd = -1;
    for(auto &in : p.incoming)
        close(in.fd);
    p.incoming.clear();
    p.connecting = p.listener = p.eof_seen = p.flow_blocked = false;
    p.look = 0;
    p.data_armed = p.listen_armed = true;
}

uint32_t unbind(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    if(p->state != T_IDLE)
        return kOTOutStateErr;
    drop_socket(*p);
    p->state = T_UNBND;
    if(p->async)
        post(*p, T_UNBINDCOMPLETE, kOTNoError, 0);
    rewatch();
    return kOTNoError;
}

/* ---- connections ------------------------------------------------------ */

/* The socket an idle endpoint connects with: bound at OTBind for a
   listener or a named address, else made now. */
int32_t ensure_socket(Provider &p)
{
    if(p.fd >= 0)
        return kOTNoError;
    p.fd = socket(AF_INET, SOCK_STREAM, 0);
    if(p.fd < 0)
        return ot_errno(errno);
    set_nonblocking(p.fd);
    apply_pending_options(p);
    return kOTNoError;
}

/* A blocking call's wait for its socket: in slices, with the interrupts
   that came due run in between (Time Manager and VBL tasks, OT's own pump
   and so notifiers, deferred tasks), as a Mac's interrupts go on while an
   application spins in a blocking call. A notifier may cancel the call
   (OTCancelSynchronousCalls) or close the provider. kOTNoError and ready
   set, or kOTNoError on timeout (timeout_ms; -1 never), or the cancel's
   error, or kOTBadReferenceErr if the provider went. */
int32_t block_wait(uint32_t ref, short events, int timeout_ms, bool &ready)
{
    ready = false;
    auto until = Clock::now() + std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms);
    Provider *p = find_provider(ref);
    if(p)
        p->in_sync_wait = true;
    int32_t err = kOTNoError;
    for(;;)
    {
        p = find_provider(ref);
        if(!p || p->fd < 0)
        {
            err = p ? kOTLookErr : kOTBadReferenceErr;
            break;
        }
        if(p->cancel_err)
        {
            err = p->cancel_err;
            p->cancel_err = 0;
            break;
        }
        int slice = 20;
        if(timeout_ms >= 0)
        {
            auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - Clock::now()).count();
            if(left <= 0)
                break;
            slice = std::min<int>(slice, left);
        }
        struct pollfd pf = { p->fd, events, 0 };
        if(poll(&pf, 1, slice) > 0)
        {
            ready = true;
            break;
        }
        syncint_check_interrupt();
    }
    if((p = find_provider(ref)))
        p->in_sync_wait = false;
    return err;
}

int32_t connect_done(Provider &p)
{
    int err = 0;
    socklen_t len = sizeof err;
    getsockopt(p.fd, SOL_SOCKET, SO_ERROR, &err, &len);
    p.connecting = false;
    if(err)
    {
        close(p.fd);
        p.fd = -1;
        p.look = T_DISCONNECT;
        OT_LOG("connect %08x: %s\n", p.ref, strerror(err));
        return kOTLookErr;
    }
    p.state = T_DATAXFER;
    p.data_armed = true;
    /* TCP_NODELAY on unless the client set it (T_DEFAULT says so too). */
    int one = 1;
    if(!option_was_set(p, 0x06, 0x01)) /* INET_TCP, TCP_NODELAY */
        setsockopt(p.fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return kOTNoError;
}

/* Connects to a DNSAddress on an asynchronous or non-blocking endpoint:
   the name looked up on a host thread, then the connect made from the
   pump, its outcome T_CONNECT or T_DISCONNECT as for an address. */
struct Looked
{
    uint32_t ref;
    int32_t err;
    uint32_t ip;
    uint16_t port;
};
std::mutex looked_mutex;
std::vector<Looked> looked;
bool lookup_hooked = false;

void finish_connect_lookups()
{
    std::vector<Looked> ready;
    {
        std::lock_guard<std::mutex> lock(looked_mutex);
        ready.swap(looked);
    }
    for(auto &l : ready)
    {
        Provider *p = find_provider(l.ref);
        if(!p || !p->resolving)
            continue;
        p->resolving = false;
        int err = 0;
        if(!l.err)
        {
            struct sockaddr_in sin = {};
            sin.sin_family = AF_INET;
            sin.sin_port = htons(l.port);
            sin.sin_addr.s_addr = htonl(l.ip);
            OT_LOG("connect %08x to %s:%u\n", p->ref, inet_ntoa(sin.sin_addr), l.port);
            if(::connect(p->fd, (struct sockaddr *)&sin, sizeof sin) == 0 || errno == EINPROGRESS)
            {
                p->connecting = true;
                continue;
            }
            err = errno;
        }
        close(p->fd);
        p->fd = -1;
        p->look = T_DISCONNECT;
        post(*p, T_DISCONNECT, l.err ? l.err : ot_errno(err), 0);
    }
}

void start_connect_lookup(uint32_t ref, const std::string &name, uint16_t port)
{
    if(!lookup_hooked)
    {
        add_pump_hook(finish_connect_lookups);
        lookup_hooked = true;
    }
    std::thread([=]() {
        uint32_t ip = 0;
        int32_t err = inet_resolve(name, ip);
        {
            std::lock_guard<std::mutex> lock(looked_mutex);
            looked.push_back({ ref, err, ip, port });
        }
        wake();
    }).detach();
}

/* OSStatus OTConnect(ref, TCall *snd, TCall *rcv) */
uint32_t connect_(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    if(p->kind != Kind::TCP)
        return kOTNotSupportedErr;
    if(p->state != T_IDLE || p->listener)
        return kOTOutStateErr;
    uint32_t snd = c.l(1), rcv = c.l(2);
    struct sockaddr_in sin;
    if(!snd)
        return kOTBadAddressErr;
    std::string name;
    uint16_t port;
    if((p->async || !p->blocking) && dns_name({ snd }, name, port))
    {
        /* Looked up on a host thread; the connect goes on from the pump. */
        if(int32_t e = ensure_socket(*p))
            return e;
        OT_LOG("connect %08x to \"%s\" port %u: looking it up\n", p->ref, name.c_str(), port);
        p->state = T_OUTCON;
        p->resolving = true;
        p->look = 0;
        start_connect_lookup(p->ref, name, port);
        return kOTNoDataErr;
    }
    if(int32_t e = read_address({ snd }, sin))
        return e;
    if(int32_t e = ensure_socket(*p))
        return e;
    OT_LOG("connect %08x to %s:%u%s%s\n", p->ref, inet_ntoa(sin.sin_addr),
           ntohs(sin.sin_port), p->async ? " async" : "", p->blocking ? " blocking" : "");
    p->look = 0;
    int r = ::connect(p->fd, (struct sockaddr *)&sin, sizeof sin);
    if(r < 0 && errno != EINPROGRESS)
    {
        int e = errno;
        close(p->fd);
        p->fd = -1;
        if(p->async)
        {
            post(*p, T_DISCONNECT, ot_errno(e), 0);
            p->look = T_DISCONNECT;
            return kOTNoDataErr;
        }
        p->look = T_DISCONNECT;
        return kOTLookErr;
    }
    p->state = T_OUTCON;
    p->connecting = true;
    if(!p->async && p->blocking)
    {
        uint32_t ref = p->ref;
        bool ready;
        int32_t werr = block_wait(ref, POLLOUT, 75000, ready);
        if(!(p = find_provider(ref)))
            return kOTBadReferenceErr;
        if(werr || !ready)
        {
            close(p->fd);
            p->fd = -1;
            p->connecting = false;
            p->state = T_IDLE;
            return werr ? werr : kETIMEDOUTErr;
        }
        int32_t e = connect_done(*p);
        if(e)
        {
            p->state = T_IDLE;
            return e;
        }
        if(rcv)
        {
            struct sockaddr_in peer = {};
            socklen_t len = sizeof peer;
            getpeername(p->fd, (struct sockaddr *)&peer, &len);
            peer.sin_family = AF_INET;
            write_inet({ rcv }, peer);
        }
        rewatch();
        return kOTNoError;
    }
    /* Asynchronous, or non-blocking: T_CONNECT (or T_DISCONNECT) comes,
       then OTRcvConnect finishes it. */
    rewatch();
    return kOTNoDataErr;
}

/* OSStatus OTRcvConnect(ref, TCall *call) */
uint32_t rcv_connect(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    if(p->state != T_OUTCON)
        return kOTOutStateErr;
    if(p->resolving)
        return kOTNoDataErr;
    if(p->connecting)
    {
        if(!p->blocking || p->async)
        {
            if(!ready_now(p->fd, POLLOUT))
                return kOTNoDataErr;
        }
        else
        {
            uint32_t ref = p->ref;
            bool ready;
            int32_t werr = block_wait(ref, POLLOUT, 75000, ready);
            if(!(p = find_provider(ref)))
                return kOTBadReferenceErr;
            if(werr)
                return werr;
        }
    }
    if(p->look == T_DISCONNECT)
        return kOTLookErr;
    if(p->connecting || p->look == T_CONNECT)
    {
        p->look = 0;
        if(p->connecting && connect_done(*p))
            return kOTLookErr;
        p->state = T_DATAXFER;
        p->data_armed = true;
    }
    if(uint32_t call = c.l(1))
    {
        struct sockaddr_in peer = {};
        socklen_t len = sizeof peer;
        getpeername(p->fd, (struct sockaddr *)&peer, &len);
        peer.sin_family = AF_INET;
        write_inet({ call }, peer);
    }
    rewatch();
    return kOTNoError;
}

/* OSStatus OTListen(ref, TCall *call): a connection waiting to be
   accepted, its address and sequence number. */
uint32_t listen_(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    if(!p->listener)
        return kOTOutStateErr;
    struct sockaddr_in peer = {};
    socklen_t len = sizeof peer;
    int fd = accept(p->fd, (struct sockaddr *)&peer, &len);
    if(fd < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && p->blocking && !p->async)
    {
        uint32_t ref = p->ref;
        bool ready;
        int32_t werr = block_wait(ref, POLLIN, -1, ready);
        if(!(p = find_provider(ref)))
            return kOTBadReferenceErr;
        if(werr)
            return werr;
        len = sizeof peer;
        fd = accept(p->fd, (struct sockaddr *)&peer, &len);
    }
    if(fd < 0)
    {
        p->listen_armed = true;
        if(p->look == T_LISTEN)
            p->look = 0;
        rewatch();
        return kOTNoDataErr;
    }
    set_nonblocking(fd);
    Provider::Pending in{ fd, p->next_seq++, ntohl(peer.sin_addr.s_addr), ntohs(peer.sin_port) };
    p->incoming.push_back(in);
    p->state = T_INCON;
    if(p->look == T_LISTEN)
        p->look = 0;
    p->listen_armed = true;
    if(uint32_t call = c.l(1))
    {
        peer.sin_family = AF_INET;
        write_inet({ call }, peer);
        WRITEUL(call + 12 + 4, 0); /* opt.len */
        WRITEUL(call + 24 + 4, 0); /* udata.len */
        WRITEUL(call + 36, in.seq);
    }
    OT_LOG("listen %08x: connection %u from %s:%u\n", p->ref, in.seq,
           inet_ntoa(peer.sin_addr), in.port);
    rewatch();
    return kOTNoError;
}

/* OSStatus OTAccept(listener, acceptor, TCall *call) */
uint32_t accept_(const Call &c)
{
    Provider *lis = lookup(c, 0);
    Provider *acc = lookup(c, 1);
    if(!lis || !acc)
        return kOTBadReferenceErr;
    uint32_t call = c.l(2);
    uint32_t seq = call ? READUL(call + 36) : 0;
    auto it = std::find_if(lis->incoming.begin(), lis->incoming.end(),
                           [seq](auto &in) { return in.seq == seq; });
    if(it == lis->incoming.end())
        return kOTBadSequenceErr;
    Provider::Pending in = *it;
    lis->incoming.erase(it);
    if(lis->incoming.empty())
        lis->state = T_IDLE;
    if(acc != lis)
    {
        if(acc->state != T_UNBND && acc->state != T_IDLE)
        {
            lis->incoming.push_front(in);
            return kOTOutStateErr;
        }
        if(acc->fd >= 0)
            close(acc->fd);
    }
    else
    {
        close(lis->fd);
        lis->listener = false;
    }
    acc->fd = in.fd;
    apply_pending_options(*acc);
    acc->state = T_DATAXFER;
    acc->data_armed = true;
    acc->look = 0;
    OT_LOG("accept %u on %08x\n", seq, acc->ref);
    if(lis->async)
        post(*lis, T_ACCEPTCOMPLETE, kOTNoError, acc->ref);
    if(acc != lis)
        post(*acc, T_PASSCON, kOTNoError, acc->ref);
    rewatch();
    return kOTNoError;
}

/* OSStatus OTSndDisconnect(ref, TCall *call): abortive. */
uint32_t snd_disconnect(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    if(p->state == T_INCON && !p->incoming.empty())
    {
        /* Refuse a connection OTListen handed out. */
        close(p->incoming.front().fd);
        p->incoming.pop_front();
        if(p->incoming.empty())
            p->state = T_IDLE;
        return kOTNoError;
    }
    if(p->state < T_OUTCON)
        return kOTOutStateErr;
    if(p->fd >= 0)
    {
        struct linger lg = { 1, 0 }; /* RST, as a disconnect sends */
        setsockopt(p->fd, SOL_SOCKET, SO_LINGER, &lg, sizeof lg);
    }
    drop_socket(*p);
    p->state = T_IDLE;
    if(p->async)
        post(*p, T_DISCONNECTCOMPLETE, kOTNoError, 0);
    rewatch();
    return kOTNoError;
}

/* OSStatus OTRcvDisconnect(ref, TDiscon *) */
uint32_t rcv_disconnect(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    if(p->look != T_DISCONNECT)
        return kOTNoDisconnectErr;
    drop_socket(*p);
    p->state = T_IDLE;
    if(uint32_t d = c.l(1))
    {
        WRITEUL(d + 4, 0);  /* udata.len */
        WRITEUL(d + 12, 0); /* reason */
        WRITEUL(d + 16, 0); /* sequence */
    }
    rewatch();
    return kOTNoError;
}

/* OSStatus OTSndOrderlyDisconnect(ref): our FIN. */
uint32_t snd_orderly(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    if(p->state != T_DATAXFER && p->state != T_INREL)
        return kOTOutStateErr;
    shutdown(p->fd, SHUT_WR);
    if(p->state == T_INREL)
    {
        drop_socket(*p);
        p->state = T_IDLE;
    }
    else
        p->state = T_OUTREL;
    rewatch();
    return kOTNoError;
}

/* OSStatus OTRcvOrderlyDisconnect(ref): the peer's FIN, taken. */
uint32_t rcv_orderly(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    if(!p->eof_seen)
    {
        /* Not seen yet: look now. */
        char ch;
        if(p->fd >= 0 && recv(p->fd, &ch, 1, MSG_PEEK | MSG_DONTWAIT) == 0)
            p->eof_seen = true;
        else
            return kOTNoReleaseErr;
    }
    if(p->look == T_ORDREL)
        p->look = 0;
    if(p->state == T_OUTREL)
    {
        drop_socket(*p);
        p->state = T_IDLE;
    }
    else if(p->state == T_DATAXFER)
        p->state = T_INREL;
    else
        return kOTOutStateErr;
    rewatch();
    return kOTNoError;
}

/* ---- data ------------------------------------------------------------- */

/* OTResult OTSnd(ref, buf, nbytes, OTFlags) */
uint32_t snd(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    if(p->state != T_DATAXFER && p->state != T_INREL)
        return p->look ? kOTLookErr : kOTOutStateErr;
    uint8_t *buf = c.p(1);
    uint32_t n = c.l(2), sent = 0;
    while(sent < n)
    {
        ssize_t r = send(p->fd, buf + sent, n - sent, MSG_NOSIGNAL | MSG_DONTWAIT);
        if(r > 0)
        {
            sent += r;
            continue;
        }
        if(r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            if(p->blocking && !p->async)
            {
                uint32_t ref = p->ref;
                bool ready;
                int32_t werr = block_wait(ref, POLLOUT, -1, ready);
                if(!(p = find_provider(ref)))
                    return kOTBadReferenceErr;
                if(werr)
                    return sent ? sent : (uint32_t)werr;
                continue;
            }
            break;
        }
        if(r < 0 && errno == EINTR)
            continue;
        p->look = T_DISCONNECT;
        return sent ? sent : (uint32_t)kOTLookErr;
    }
    OT_LOG("snd %08x %u of %u bytes\n", p->ref, sent, n);
    log_bytes("sent", p->ref, buf, sent);
    if(!sent && n)
    {
        p->flow_blocked = true;
        rewatch();
        return kOTFlowErr;
    }
    if(sent < n)
    {
        p->flow_blocked = true;
        rewatch();
    }
    return sent;
}

/* OTResult OTRcv(ref, buf, nbytes, OTFlags *flags) */
uint32_t rcv(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    if(c.l(3))
        WRITEUL(c.l(3), 0);
    if(p->state != T_DATAXFER && p->state != T_OUTREL)
        return p->look ? kOTLookErr : kOTOutStateErr;
    uint8_t *buf = c.p(1);
    uint32_t n = c.l(2);
    for(;;)
    {
        ssize_t r = recv(p->fd, buf, n, MSG_DONTWAIT);
        if(r > 0)
        {
            OT_LOG("rcv %08x %zd bytes\n", p->ref, r);
            log_bytes("got", p->ref, buf, r);
            return r;
        }
        if(r == 0)
        {
            if(!p->eof_seen)
                OT_LOG("rcv %08x: the peer's FIN\n", p->ref);
            peer_closed(*p);
            p->look = T_ORDREL;
            return kOTLookErr;
        }
        if(errno == EINTR)
            continue;
        if(errno == EAGAIN || errno == EWOULDBLOCK)
        {
            if(p->blocking && !p->async)
            {
                uint32_t ref = p->ref;
                bool ready;
                int32_t werr = block_wait(ref, POLLIN, -1, ready);
                if(!(p = find_provider(ref)))
                    return kOTBadReferenceErr;
                if(werr)
                    return werr;
                continue;
            }
            OT_LOG("rcv %08x: no data\n", p->ref);
            p->data_armed = true; /* the next data gets a T_DATA */
            rewatch();
            return kOTNoDataErr;
        }
        p->look = T_DISCONNECT;
        return kOTLookErr;
    }
}

/* OSStatus OTSndUData(ref, TUnitData *) */
uint32_t snd_udata(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    if(p->kind != Kind::UDP)
        return kOTNotSupportedErr;
    if(p->state != T_IDLE)
        return kOTOutStateErr;
    uint32_t ud = c.l(1);
    struct sockaddr_in to;
    if(int32_t e = read_address({ ud }, to))
        return e;
    Netbuf data{ ud + 24 };
    ssize_t r = sendto(p->fd, data.buf() ? SYN68K_TO_US(data.buf()) : nullptr, data.len(),
                       MSG_DONTWAIT, (struct sockaddr *)&to, sizeof to);
    if(r < 0)
        return errno == EAGAIN ? kOTFlowErr : ot_errno(errno);
    return kOTNoError;
}

/* Hand out (the next piece of) a datagram kept in udata_rest. */
uint32_t give_udata_rest(Provider &p, uint32_t ud, uint32_t flagsp)
{
    Netbuf data{ ud + 24 };
    struct sockaddr_in from = {};
    from.sin_family = AF_INET;
    from.sin_addr.s_addr = htonl(p.udata_rest_host);
    from.sin_port = htons(p.udata_rest_port);
    write_inet({ ud }, from);
    WRITEUL(ud + 12 + 4, 0); /* opt.len */
    size_t left = p.udata_rest.size() - p.udata_rest_off;
    uint32_t n = std::min<size_t>(left, data.maxlen());
    if(n && data.buf())
        std::memcpy(SYN68K_TO_US(data.buf()), p.udata_rest.data() + p.udata_rest_off, n);
    data.set_len(n);
    p.udata_rest_off += n;
    bool more = p.udata_rest_off < p.udata_rest.size();
    if(flagsp)
        WRITEUL(flagsp, more ? T_MORE : 0);
    if(!more)
    {
        p.udata_rest.clear();
        p.udata_rest_off = 0;
    }
    OT_LOG("rcvudata %08x: %u bytes%s\n", p.ref, n, more ? " (T_MORE)" : "");
    return kOTNoError;
}

/* OSStatus OTRcvUData(ref, TUnitData *, OTFlags *flags): a datagram bigger
   than the buffer comes in pieces, T_MORE set on all but the last, as
   OT's. */
uint32_t rcv_udata(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    if(p->kind != Kind::UDP)
        return kOTNotSupportedErr;
    uint32_t ud = c.l(1), flagsp = c.l(2);
    if(flagsp)
        WRITEUL(flagsp, 0);
    if(!p->udata_rest.empty())
        return give_udata_rest(*p, ud, flagsp);
    if(p->fd < 0)
        return kOTOutStateErr;
    for(;;)
    {
        /* How big the next datagram is, without taking it. */
        char probe;
        ssize_t size = recv(p->fd, &probe, 1, MSG_PEEK | MSG_TRUNC | MSG_DONTWAIT);
        if(size >= 0)
        {
            std::vector<uint8_t> dgram(std::max<ssize_t>(size, 1));
            struct sockaddr_in from = {};
            socklen_t len = sizeof from;
            ssize_t r = recvfrom(p->fd, dgram.data(), dgram.size(), MSG_DONTWAIT,
                                 (struct sockaddr *)&from, &len);
            if(r < 0)
                return ot_errno(errno);
            dgram.resize(r);
            p->udata_rest = std::move(dgram);
            p->udata_rest_off = 0;
            p->udata_rest_host = ntohl(from.sin_addr.s_addr);
            p->udata_rest_port = ntohs(from.sin_port);
            uint32_t res = give_udata_rest(*p, ud, flagsp);
            p->data_armed = true;
            return res;
        }
        if(errno == EINTR)
            continue;
        if(errno == EAGAIN || errno == EWOULDBLOCK)
        {
            if(p->blocking && !p->async)
            {
                uint32_t ref = p->ref;
                bool ready;
                int32_t werr = block_wait(ref, POLLIN, -1, ready);
                if(!(p = find_provider(ref)))
                    return kOTBadReferenceErr;
                if(werr)
                    return werr;
                continue;
            }
            p->data_armed = true;
            rewatch();
            return kOTNoDataErr;
        }
        return ot_errno(errno);
    }
}

/* OSStatus OTRcvUDErr(ref, TUDErr *): datagram errors (ICMP) aren't
   reported here, so there never is one. */
uint32_t rcv_uderr(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    if(p->kind != Kind::UDP)
        return kOTNotSupportedErr;
    return kOTNoUDErrErr;
}

/* OTResult OTSync(ref): the endpoint's state, after a look at its socket
   (OT's brings its own idea of the state up to date). */
uint32_t sync_(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    service(*p);
    if(p->async)
        post(*p, T_SYNCCOMPLETE, kOTNoError, 0);
    return p->state;
}

/* OSStatus OTCancelSynchronousCalls(ref, OSStatus err): a blocking call
   waiting on the provider returns err (from a notifier, a deferred task, a
   Time Manager task). Nothing waiting: nothing to do. */
uint32_t cancel_sync_calls(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    if(p->in_sync_wait)
    {
        p->cancel_err = (int32_t)c.l(1);
        OT_LOG("%08x: synchronous call cancelled (%d)\n", p->ref, p->cancel_err);
    }
    return kOTNoError;
}

/* OSStatus OTResolveAddress(ref, TBind *req, TBind *ret, OTTimeout): an
   InetAddress as it is, a DNSAddress ("name[:port]") looked up. */
uint32_t resolve_address(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    uint32_t req = c.l(1), ret = c.l(2);
    struct sockaddr_in sin;
    if(!req)
        return kOTBadAddressErr;
    if(int32_t e = read_address({ req }, sin))
        return e;
    if(ret)
        write_inet({ ret }, sin);
    if(p->async)
        post(*p, T_RESOLVEADDRCOMPLETE, kOTNoError, ret);
    return kOTNoError;
}

/* OTPortRef OTGetProviderPortRef(ref) */
uint32_t get_provider_port_ref(const Call &c)
{
    return lookup(c) ? enet_port_ref() : 0;
}

/* SInt32 OTIoctl(ref, UInt32 cmd, void *data): the STREAMS module stack
   isn't there to talk to; signing up for miscellaneous events is accepted
   (there are none). */
uint32_t ioctl_(const Call &c)
{
    if(!lookup(c))
        return kOTBadReferenceErr;
    uint32_t cmd = c.l(1);
    OT_LOG("OTIoctl %08x cmd $%04x\n", c.l(0), cmd);
    if(cmd == 0x4F01) /* I_OTGetMiscellaneousEvents */
        return kOTNoError;
    return (uint32_t)kEINVALErr;
}

/* ---- options (ot_options.cpp) -------------------------------------------- */

uint32_t option_management_(const Call &c)
{
    Provider *p = lookup(c);
    if(!p)
        return kOTBadReferenceErr;
    uint32_t err = option_management(*p, c.l(1), c.l(2));
    if(p->async)
        post(*p, T_OPTMGMTCOMPLETE, err, c.l(2));
    return err;
}

/* ---- OTAlloc and OTFree -------------------------------------------------
   void *OTAllocPriv(ref, OTStructType, UInt32 fields, OSStatus *err,
   client): the structure with buffers for the fields asked for (T_ADDR 1,
   T_OPT 2, T_UDATA 4, T_ALL). */
enum : uint32_t
{
    T_BIND_S = 1,
    T_OPTMGMT_S = 2,
    T_CALL_S = 3,
    T_DIS_S = 4,
    T_UNITDATA_S = 5,
    T_UDERROR_S = 6,
    T_INFO_S = 7,
};

uint32_t alloc_priv(const Call &c)
{
    uint32_t type = c.l(1), fields = c.l(2), errp = c.l(3);
    /* Netbuf offsets for each structure: addr, opt, udata. */
    int addr = -1, opt = -1, udata = -1;
    uint32_t size = 0;
    switch(type)
    {
        case T_BIND_S: size = 16; addr = 0; break;
        case T_OPTMGMT_S: size = 16; opt = 0; break;
        case T_CALL_S: size = 40; addr = 0; opt = 12; udata = 24; break;
        case T_DIS_S: size = 20; udata = 0; break;
        case T_UNITDATA_S: size = 36; addr = 0; opt = 12; udata = 24; break;
        case T_UDERROR_S: size = 28; addr = 0; opt = 12; break;
        case T_INFO_S: size = 32; break;
        default:
            if(errp)
                WRITEUL(errp, (uint32_t)kOTNoStructureTypeErr);
            return 0;
    }
    Ptr p = NewPtrClear(size);
    if(!p)
    {
        if(errp)
            WRITEUL(errp, (uint32_t)kOTOutOfMemoryErr);
        return 0;
    }
    uint32_t a = guest_addr(p);
    auto give = [&](int off, uint32_t n) {
        if(off < 0)
            return;
        Ptr b = NewPtrClear(n);
        WRITEUL(a + off, b ? n : 0);
        WRITEUL(a + off + 8, guest_addr(b));
    };
    if(fields & 1)
        give(addr, 16);
    if(fields & 2)
        give(opt, 1024);
    if(fields & 4)
        give(udata, type == T_UNITDATA_S ? 65536 : 1024);
    if(errp)
        WRITEUL(errp, 0);
    return a;
}

/* OTResult OTFree(void *, OTStructType) */
uint32_t free_(const Call &c)
{
    uint32_t a = c.l(0), type = c.l(1);
    if(!a)
        return kOTNoError;
    int offs[3] = { -1, -1, -1 };
    switch(type)
    {
        case T_BIND_S: offs[0] = 0; break;
        case T_OPTMGMT_S: offs[0] = 0; break;
        case T_CALL_S:
        case T_UNITDATA_S: offs[0] = 0; offs[1] = 12; offs[2] = 24; break;
        case T_DIS_S: offs[0] = 0; break;
        case T_UDERROR_S: offs[0] = 0; offs[1] = 12; break;
    }
    for(int off : offs)
        if(off >= 0)
            if(uint32_t b = READUL(a + off + 8))
                DisposePtr((Ptr)SYN68K_TO_US(b));
    DisposePtr((Ptr)SYN68K_TO_US(a));
    return kOTNoError;
}

/* ---- misc ------------------------------------------------------------- */

/* pascal void OTIdle(void) */
uint32_t idle(const Call &)
{
    return 0;
}

/* pascal void OTDelay(UInt32 seconds) */
uint32_t delay(const Call &c)
{
    std::this_thread::sleep_for(std::chrono::seconds(c.l(0)));
    return 0;
}

const char clnt[] = "OTClnt$LclClnt";

const Def table[] = {
    { clnt, "OTOpenEndpointPriv", Conv::C, "LLLLL", 'L', open_endpoint_priv },
    { clnt, "OTAsyncOpenEndpointPriv", Conv::C, "LLLLLL", 'L', async_open_endpoint_priv },
    { clnt, "OTCloseProviderPriv", Conv::C, "LL", 'L', close_provider_priv },
    { clnt, "OTSETASYNCHRONOUS", Conv::Pascal, "L", 'L', set_async },
    { clnt, "OTSETSYNCHRONOUS", Conv::Pascal, "L", 'L', set_sync },
    { clnt, "OTISSYNCHRONOUS", Conv::Pascal, "L", 'B', is_sync },
    { clnt, "OTSETBLOCKING", Conv::Pascal, "L", 'L', set_blocking },
    { clnt, "OTSETNONBLOCKING", Conv::Pascal, "L", 'L', set_nonblocking },
    { clnt, "OTISBLOCKING", Conv::Pascal, "L", 'B', is_blocking },
    { clnt, "OTACKSENDS", Conv::Pascal, "L", 'L', ack_sends },
    { clnt, "OTDONTACKSENDS", Conv::Pascal, "L", 'L', dont_ack_sends },
    { clnt, "OTISACKINGSENDS", Conv::Pascal, "L", 'B', is_acking },
    { clnt, "OTINSTALLNOTIFIER", Conv::Pascal, "LLL", 'L', install_notifier },
    { clnt, "OTREMOVENOTIFIER", Conv::Pascal, "L", 'V', remove_notifier },
    { clnt, "OTGETENDPOINTSTATE", Conv::Pascal, "L", 'L', get_endpoint_state },
    { clnt, "OTGETENDPOINTINFO", Conv::Pascal, "LL", 'L', get_endpoint_info },
    { clnt, "OTLOOK", Conv::Pascal, "L", 'L', look },
    { clnt, "OTCOUNTDATABYTES", Conv::Pascal, "LL", 'L', count_data_bytes },
    { clnt, "OTGETPROTADDRESS", Conv::Pascal, "LLL", 'L', get_prot_address },
    { clnt, "OTBIND", Conv::Pascal, "LLL", 'L', bind_ },
    { clnt, "OTUNBIND", Conv::Pascal, "L", 'L', unbind },
    { clnt, "OTCONNECT", Conv::Pascal, "LLL", 'L', connect_ },
    { clnt, "OTRCVCONNECT", Conv::Pascal, "LL", 'L', rcv_connect },
    { clnt, "OTLISTEN", Conv::Pascal, "LL", 'L', listen_ },
    { clnt, "OTACCEPT", Conv::Pascal, "LLL", 'L', accept_ },
    { clnt, "OTSNDDISCONNECT", Conv::Pascal, "LL", 'L', snd_disconnect },
    { clnt, "OTRCVDISCONNECT", Conv::Pascal, "LL", 'L', rcv_disconnect },
    { clnt, "OTSNDORDERLYDISCONNECT", Conv::Pascal, "L", 'L', snd_orderly },
    { clnt, "OTRCVORDERLYDISCONNECT", Conv::Pascal, "L", 'L', rcv_orderly },
    { clnt, "OTSND", Conv::Pascal, "LLLL", 'L', snd },
    { clnt, "OTRCV", Conv::Pascal, "LLLL", 'L', rcv },
    { clnt, "OTSNDUDATA", Conv::Pascal, "LL", 'L', snd_udata },
    { clnt, "OTRCVUDATA", Conv::Pascal, "LLL", 'L', rcv_udata },
    { clnt, "OTOPTIONMANAGEMENT", Conv::Pascal, "LLL", 'L', option_management_ },
    { clnt, "OTNEXTOPTION", Conv::Pascal, "LLL", 'L', next_option },
    { clnt, "OTFINDOPTION", Conv::Pascal, "LLLL", 'L', find_option },
    { clnt, "OTRCVUDERR", Conv::Pascal, "LL", 'L', rcv_uderr },
    { clnt, "OTSYNC", Conv::Pascal, "L", 'L', sync_ },
    { clnt, "OTCANCELSYNCHRONOUSCALLS", Conv::Pascal, "LL", 'L', cancel_sync_calls },
    { clnt, "OTRESOLVEADDRESS", Conv::Pascal, "LLLL", 'L', resolve_address },
    { clnt, "OTGETPROVIDERPORTREF", Conv::Pascal, "L", 'L', get_provider_port_ref },
    { clnt, "OTIOCTL", Conv::Pascal, "LLL", 'L', ioctl_ },
    { clnt, "OTAllocPriv", Conv::C, "LLLLL", 'L', alloc_priv },
    { clnt, "OTFREE", Conv::Pascal, "LL", 'L', free_ },
    { clnt, "OTIDLE", Conv::Pascal, "", 'V', idle },
    { clnt, "OTDELAY", Conv::Pascal, "L", 'V', delay },
};

} // anonymous namespace

/* ---- the rest of the provider API ---------------------------------------- */

Provider *Executor::ot::find_provider(uint32_t ref)
{
    auto it = providers.find(ref);
    return it == providers.end() ? nullptr : &it->second;
}

Provider &Executor::ot::new_provider(uint32_t client, Kind kind)
{
    uint32_t ref = REF_BASE | ((next_ref += 2) & 0xFFFE);
    Provider &p = providers[ref];
    p.ref = ref;
    p.client = client;
    p.kind = kind;
    return p;
}

void Executor::ot::close_provider(uint32_t ref)
{
    Provider *p = find_provider(ref);
    if(!p)
        return;
    OT_LOG("close %08x\n", ref);
    drop_socket(*p);
    providers.erase(ref);
    events.erase(std::remove_if(events.begin(), events.end(),
                                [ref](const Event &e) { return e.ref == ref; }),
                 events.end());
    rewatch();
}

void Executor::ot::close_client_providers(uint32_t record)
{
    std::vector<uint32_t> refs;
    for(auto &[ref, p] : providers)
        if(p.client == record)
            refs.push_back(ref);
    for(uint32_t ref : refs)
        close_provider(ref);
}

void Executor::ot::post(Provider &p, uint32_t code, int32_t result, uint32_t cookie)
{
    if(!p.notifier)
        return;
    events.push_back({ p.ref, code, result, cookie });
    wake();
}

void Executor::ot::wake()
{
    ot_interrupt.trigger();
}

void Executor::ot::add_pump_hook(std::function<void()> hook)
{
    pump_hooks.push_back(std::move(hook));
}

uint32_t Executor::ot::current_a5()
{
    return EM_A5;
}

void Executor::ot::register_providers()
{
    register_table(table);
}
