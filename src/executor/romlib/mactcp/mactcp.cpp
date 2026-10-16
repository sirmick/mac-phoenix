/* mactcp.cpp — native `.IPP` (MacTCP) driver for Executor 2000.
 *
 * TCP and UDP streams on host sockets.  Modeled on src/serial.cpp.
 *
 * Design notes:
 *   - Async model, as MacTCP's: a call made with the async bit that
 *     cannot finish at once (a connect in progress, a passive open, a
 *     receive with no data) is parked on its stream with ioResult =
 *     inProgress.  A watcher thread polls the sockets of the parked
 *     calls (and of streams whose ASR waits for data) and raises an
 *     Executor interrupt (time/syncint) when one is ready or a timeout
 *     falls due; at interrupt time the driver finishes the call, sets
 *     ioResult and runs the completion routine, and delivers the ASRs
 *     (data arrival, closing, terminate).  MacTCP did all of that from
 *     its network interrupts too, so completion routines and ASRs are
 *     written for it.
 *   - Synchronous calls block the 68k thread in poll(2), with their
 *     timeout; sends always complete before the call returns (host
 *     socket buffers are large).
 *   - Receive areas: UDPRead and TCPNoCopyRcv lend out pieces of the
 *     area the application gave UDPCreate/TCPCreate; UDPBfrReturn and
 *     TCPRcvBfrReturn give them back.  Data stays in the host socket
 *     until there is room, which is the flow control.
 *   - StreamPtr: an opaque cookie (never a real pointer); apps are
 *     documented to treat it as opaque.
 *   - Byte order: guest is big-endian == network order; GUEST<>
 *     reads yield host-order values, so htonl/htons at the sockaddr
 *     boundary is both necessary and sufficient.
 *   - The resolver is not the driver's: it is the 'dnrp' code
 *     resource, dnr.cpp.  Its lookups finish on the same interrupt.
 */

#include <base/common.h>
#include <DeviceMgr.h>
#include <FileMgr.h>
#include <MemoryMgr.h>
#include <OSUtil.h>
#include <DeskMgr.h> /* accRun */
#include <MacTCP.h>  /* generated from defs/MacTCP.yaml */
#include <rsys/device.h>
#include <rsys/mactcp.h>
#include <base/cpu.h>
#include <error/error.h>
#include <time/syncint.h>
/* Required for REGISTER_FUNCTION_PTR below: it expands to an explicit
 * template instantiation of WrappedFunction<>, whose member definitions
 * live here.  Without it everything compiles and the link fails on
 * WrappedFunction<...>::init().  serial.cpp includes it for the same
 * reason. */
#include <base/traps.impl.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/uio.h> /* writev, struct iovec */
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace Executor;

/* ---- ABI guard rails ------------------------------------------------
 * If the generated headers disagree with the documented MacTCP ABI,
 * fail the build rather than corrupt guest memory at run time.
 */
static_assert(offsetof(TCPiopb, ioCompletion) == 12, "TCPiopb ABI");
static_assert(offsetof(TCPiopb, ioResult) == 16, "TCPiopb ABI");
static_assert(offsetof(TCPiopb, ioCRefNum) == 24, "TCPiopb ABI");
static_assert(offsetof(TCPiopb, csCode) == 26, "TCPiopb ABI");
static_assert(offsetof(TCPiopb, tcpStream) == 28, "TCPiopb ABI");
static_assert(offsetof(TCPiopb, csParam) == 32, "TCPiopb ABI");
static_assert(offsetof(TCPOpenPB, remoteHost) == 4, "TCPOpenPB ABI");
static_assert(offsetof(TCPOpenPB, localHost) == 10, "TCPOpenPB ABI");
static_assert(offsetof(TCPSendPB, wdsPtr) == 6, "TCPSendPB ABI");
static_assert(offsetof(TCPReceivePB, rcvBuff) == 4, "TCPReceivePB ABI");
static_assert(offsetof(TCPReceivePB, markFlag) == 1, "TCPReceivePB ABI");
static_assert(offsetof(TCPReceivePB, urgentFlag) == 2, "TCPReceivePB ABI");
static_assert(sizeof(TCPStatusPB) == 70, "TCPStatusPB ABI");
static_assert(sizeof(TCPiopb) == 102, "TCPiopb ABI");
static_assert(offsetof(GetAddrParamBlock, ourAddress) == 28,
              "GetAddrParamBlock ABI");
static_assert(sizeof(wdsEntry) == 6, "wdsEntry ABI");
static_assert(offsetof(UDPiopb, udpStream) == 28, "UDPiopb ABI");
static_assert(offsetof(UDPiopb, csParam) == 32, "UDPiopb ABI");
static_assert(offsetof(UDPCreatePB, localPort) == 12, "UDPCreatePB ABI");
static_assert(offsetof(UDPSendPB, wdsPtr) == 8, "UDPSendPB ABI");
static_assert(offsetof(UDPReceivePB, rcvBuffLen) == 12, "UDPReceivePB ABI");

namespace
{

using Clock = std::chrono::steady_clock;
constexpr Clock::time_point never = Clock::time_point::max();

/* TCPTerminate reasons (MacTCP Programmer's Guide). */
enum : uint16_t
{
    TCPRemoteAbort = 2,
    TCPULPAbort = 6,
};

/* ---- Stream table -------------------------------------------------- */

enum class StreamState : uint8_t
{
    created,     /* TCPCreate done, no connection yet   */
    connecting,  /* async TCPActiveOpen in progress      */
    listening,   /* TCPPassiveOpen waiting for a peer    */
    established,
    closing,     /* we sent FIN (TCPClose)              */
    terminated,  /* connection gone, stream not yet released */
};

/* A call parked until its socket is ready. */
struct Parked
{
    uint32_t pb;               /* guest address of the iopb */
    Clock::time_point deadline;
};

struct MacTCPStream
{
    bool udp = false;
    int fd = -1;
    int listen_fd = -1;        /* TCPPassiveOpen until a peer comes */
    StreamState state = StreamState::created;
    bool remoteClosed = false; /* peer FIN seen */

    /* App-supplied receive area (guest memory), from TCPCreate or
     * UDPCreate: UDPRead and TCPNoCopyRcv lend pieces of it out. */
    Ptr rcvBuff = nullptr;
    uint32_t rcvBuffLen = 0;
    std::deque<std::pair<uint32_t, uint32_t>> lent; /* (offset, length) */
    bool areaFull = false;     /* a parked read waits for a buffer return */

    /* ASR + per-stream user data, from TCPCreate/UDPCreate (guest
     * addresses). */
    uint32_t notifyProc = 0;
    uint32_t userDataPtr = 0;
    bool arrivalNotified = false; /* data-arrival ASR sent, nothing read since */
    bool closingNotified = false;

    std::optional<Parked> open; /* TCPActiveOpen / TCPPassiveOpen */
    std::deque<Parked> reads;   /* TCPRcv, TCPNoCopyRcv, UDPRead */
};

/* StreamPtr cookies: 'TCP\0' | id.  Never dereferenced. */
constexpr uint32_t STREAM_COOKIE_BASE = 0x54435000;
uint32_t next_stream_id = 1;
std::unordered_map<uint32_t, MacTCPStream> streams;

bool log_enabled()
{
    return ERROR_ENABLED_P(ERROR_MACTCP_LOG);
}

uint32_t cookie_of(GUEST<StreamPtr> sp)
{
    return guest_cast<uint32_t>(sp);
}

MacTCPStream *find(uint32_t cookie)
{
    auto it = streams.find(cookie);
    return it == streams.end() ? nullptr : &it->second;
}

MacTCPStream *lookup(GUEST<StreamPtr> sp, OSErr *err, bool udp)
{
    MacTCPStream *s = find(cookie_of(sp));
    if(!s || s->udp != udp)
    {
        *err = invalidStreamPtr;
        return nullptr;
    }
    return s;
}

/* ---- errno -> MacTCP OSErr ---------------------------------------- */

OSErr map_socket_errno(int e)
{
    switch(e)
    {
        case 0:
            return noErr;
        case ECONNREFUSED:
        case EHOSTUNREACH:
        case ENETUNREACH:
            return openFailed;
        case ETIMEDOUT:
            return commandTimeout;
        case ECONNRESET:
        case EPIPE:
            return connectionTerminated;
        case EADDRINUSE:
            return duplicateSocket;
        case ENOBUFS:
        case ENOMEM:
        case EMFILE:
        case ENFILE:
            return insufficientResources;
        default:
            return ipBadAddr; /* generic; refine as cases surface */
    }
}

/* ---- completion ---------------------------------------------------- */

/* A finished call: ioResult, then the completion routine.  MacTCP's
 * are pascal procedures taking the iopb on the stack
 * (TCPIOCompletionProc, UDPIOCompletionProc), not the Device Manager's
 * register convention; A0 holds it too, as MacTCP left it. */
OSErr complete(TCPiopb *pb, OSErr err)
{
    if(ERROR_ENABLED_P(ERROR_MACTCP_LOG) && pb->ioResult == inProgress
       && (pb->ioTrap & asyncTrpBit))
        fprintf(stderr, "[mactcp] csCode %d stream %08x done -> %d\n",
                (int)pb->csCode, (uint32_t)guest_cast<uint32_t>(pb->tcpStream), err);
    pb->ioResult = err;
    if((pb->ioTrap & asyncTrpBit) && pb->ioCompletion)
    {
        uint32_t saved_a7 = EM_A7;
        EM_A7 -= 4;
        WRITEUL(EM_A7, US_TO_SYN68K(pb));
        EM_A0 = US_TO_SYN68K(pb);
        EM_D0 = (unsigned short)err;
        execute68K(US_TO_SYN68K((ProcPtr)pb->ioCompletion));
        EM_A7 = saved_a7;
    }
    return err;
}

TCPiopb *pb_at(uint32_t addr)
{
    return (TCPiopb *)SYN68K_TO_US(addr);
}

uint32_t pb_addr(void *pb)
{
    return US_TO_SYN68K(pb);
}

bool is_async(void *pb)
{
    return (((TCPiopb *)pb)->ioTrap & asyncTrpBit) != 0;
}

Clock::time_point deadline_after(unsigned secs)
{
    return secs ? Clock::now() + std::chrono::seconds(secs) : never;
}

/* ---- ASR delivery ---------------------------------------------------
 * TCP: pascal void (StreamPtr, u16 eventCode, Ptr userDataPtr,
 *                   u16 terminReason, ICMPReport *icmpMsg)
 * UDP: pascal void (StreamPtr, u16 eventCode, Ptr userDataPtr,
 *                   ICMPReport *icmpMsg)
 * Arguments pushed left to right, the callee pops them.
 */
void call_asr(uint32_t cookie, const MacTCPStream &s, uint16_t event,
              uint16_t reason)
{
    if(!s.notifyProc)
        return;
    if(log_enabled())
        fprintf(stderr, "[mactcp] ASR stream %08x event %d reason %d\n",
                cookie, event, reason);
    uint32_t proc = s.notifyProc;
    uint32_t user = s.userDataPtr;
    bool udp = s.udp;
    uint32_t saved_a7 = EM_A7;
    EM_A7 -= 4;
    WRITEUL(EM_A7, cookie);
    EM_A7 -= 2;
    WRITEUW(EM_A7, event);
    EM_A7 -= 4;
    WRITEUL(EM_A7, user);
    if(!udp)
    {
        EM_A7 -= 2;
        WRITEUW(EM_A7, reason);
    }
    EM_A7 -= 4;
    WRITEUL(EM_A7, 0); /* no ICMP report */
    execute68K(proc);
    EM_A7 = saved_a7;
}

/* ---- helpers ------------------------------------------------------- */

int set_nonblocking(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    return fl < 0 ? fl : fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

/* Wait for fd readiness with a MacTCP-style timeout (0 = default). */
int poll_one(int fd, short events, uint8_t timeout_secs,
             uint8_t default_secs)
{
    struct pollfd p = { fd, events, 0 };
    int secs = timeout_secs ? timeout_secs : default_secs;
    return poll(&p, 1, secs * 1000);
}

bool ready_now(int fd, short events)
{
    struct pollfd p = { fd, events, 0 };
    return fd >= 0 && poll(&p, 1, 0) > 0;
}

/* Gather a WDS (zero-length entry terminates it) into an iovec list. */
OSErr gather(Ptr wdsPtr, std::vector<struct iovec> &iov, uint32_t &total)
{
    wdsEntry *w = guest_cast<wdsEntry *>(wdsPtr);
    if(!w)
        return invalidWDS;
    total = 0;
    for(int n = 0; w[n].length; ++n)
    {
        if(n >= 64)
            return invalidWDS;
        iov.push_back({ (void *)guest_cast<Ptr>(w[n].ptr), w[n].length });
        total += w[n].length;
    }
    return noErr;
}

/* ---- the receive area -----------------------------------------------
 * Pieces are lent and given back roughly in order, so the area is used
 * as a ring: free space is after the newest piece, or before the
 * oldest one once the ring has wrapped.  A piece given back out of order
 * leaves a hole until the pieces before it come back too.
 */
std::optional<uint32_t> lend(MacTCPStream &s, uint32_t &n, bool partial)
{
    uint32_t len = s.rcvBuffLen;
    auto take = [&](uint32_t off, uint32_t room) -> std::optional<uint32_t> {
        if(room < n)
        {
            if(!partial || room == 0)
                return std::nullopt;
            n = room;
        }
        s.lent.push_back({ off, n });
        return off;
    };
    if(s.lent.empty())
        return take(0, len);
    uint32_t head = s.lent.front().first;
    auto [last, last_len] = s.lent.back();
    uint32_t tail = last + last_len;
    if(last >= head)
    {
        /* Not wrapped: room at the end, then at the start. */
        if(len - tail >= n || (partial && len - tail > 0 && head == 0))
            return take(tail, len - tail);
        return take(0, head);
    }
    return take(tail, head - tail);
}

bool give_back(MacTCPStream &s, uint32_t off)
{
    for(auto it = s.lent.begin(); it != s.lent.end(); ++it)
        if(it->first == off)
        {
            s.lent.erase(it);
            s.areaFull = false;
            return true;
        }
    return false;
}

/* ---- the watcher ----------------------------------------------------
 * A host thread that polls the sockets parked calls and ASRs wait on,
 * and raises the network interrupt when one is ready or the earliest
 * deadline passes.  After raising it, it waits for the interrupt's
 * service (which ends in rewatch()) before polling again, so a ready
 * socket that nobody reads is not a busy loop.
 */
struct Watch
{
    std::vector<struct pollfd> fds;
    Clock::time_point deadline = never;
    uint64_t gen = 0;
};

std::mutex watch_mutex;
std::condition_variable watch_cv;
Watch watch;
uint64_t handled_gen = 0;
int wake_pipe[2] = { -1, -1 };
bool watcher_started = false;

void pump();
Interrupt net_interrupt{ [] { pump(); } };

void watcher_main()
{
    for(;;)
    {
        std::vector<struct pollfd> fds;
        Clock::time_point deadline;
        uint64_t gen;
        {
            std::unique_lock<std::mutex> lock(watch_mutex);
            watch_cv.wait(lock, [] { return watch.gen != handled_gen; });
            fds = watch.fds;
            deadline = watch.deadline;
            gen = watch.gen;
        }
        fds.push_back({ wake_pipe[0], POLLIN, 0 });
        int timeout = -1;
        if(deadline != never)
        {
            auto left = std::chrono::ceil<std::chrono::milliseconds>(
                deadline - Clock::now());
            timeout = std::max<int64_t>(0, left.count());
        }
        int r = poll(fds.data(), fds.size(), timeout);
        if(fds.back().revents)
        {
            char buf[64];
            while(read(wake_pipe[0], buf, sizeof buf) > 0)
                ;
        }
        bool fire = r == 0;
        for(size_t i = 0; i + 1 < fds.size(); i++)
            fire |= fds[i].revents != 0;
        if(fire)
        {
            {
                std::lock_guard<std::mutex> lock(watch_mutex);
                handled_gen = gen;
            }
            net_interrupt.trigger();
        }
    }
}

/* Recompute what the watcher waits for, after every call and every
 * service pass. */
void rewatch()
{
    Watch w;
    for(auto &[cookie, s] : streams)
    {
        short events = 0;
        int fd = s.fd;
        if(s.open)
        {
            if(s.state == StreamState::listening)
                fd = s.listen_fd, events = POLLIN;
            else
                events = POLLOUT;
            w.deadline = std::min(w.deadline, s.open->deadline);
        }
        else if(!s.reads.empty())
        {
            if(!s.areaFull)
                events = POLLIN;
            for(auto &p : s.reads)
                w.deadline = std::min(w.deadline, p.deadline);
        }
        else if(s.notifyProc && !s.arrivalNotified && !s.closingNotified
                && (s.udp || s.state == StreamState::established
                    || s.state == StreamState::closing))
            events = POLLIN;
        if(events && fd >= 0)
            w.fds.push_back({ fd, events, 0 });
    }
    if(w.fds.empty() && w.deadline == never && !watcher_started)
        return;
    if(!watcher_started)
    {
        if(pipe(wake_pipe) < 0)
            return;
        set_nonblocking(wake_pipe[0]);
        set_nonblocking(wake_pipe[1]);
        std::thread(watcher_main).detach();
        watcher_started = true;
    }
    {
        std::lock_guard<std::mutex> lock(watch_mutex);
        w.gen = watch.gen + 1;
        watch = std::move(w);
    }
    watch_cv.notify_all();
    char c = 0;
    (void)!write(wake_pipe[1], &c, 1);
}

/* ---- csCode implementations ---------------------------------------- */

/* The address the host sends from by default: what a UDP socket
 * "connected" to a public address gets bound to (no packet is sent).
 * 0 without a default route. */
uint32_t primary_address()
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd < 0)
        return 0;
    struct sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(9);
    a.sin_addr.s_addr = htonl(0xC0000201); /* 192.0.2.1, TEST-NET-1 */
    uint32_t result = 0;
    socklen_t alen = sizeof a;
    if(connect(fd, (struct sockaddr *)&a, sizeof a) == 0
       && getsockname(fd, (struct sockaddr *)&a, &alen) == 0)
        result = ntohl(a.sin_addr.s_addr);
    close(fd);
    return result;
}

OSErr do_getaddr(GetAddrParamBlock *pb)
{
    /* The interface the host's default route uses, else the first
     * non-loopback IPv4 one (applications such as GUSI's take this
     * address for 127.0.0.1, so a container bridge would be wrong). */
    struct ifaddrs *ifa0;
    OSErr err = ipBadCnfgErr;
    uint32_t primary = primary_address();

    pb->ourAddress = 0;
    pb->ourNetMask = 0;
    if(getifaddrs(&ifa0) == 0)
    {
        for(int pass = primary ? 0 : 1; pass < 2 && err; pass++)
        for(struct ifaddrs *ifa = ifa0; ifa; ifa = ifa->ifa_next)
        {
            if(!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
                continue;
            auto *sin = (struct sockaddr_in *)ifa->ifa_addr;
            uint32_t a = ntohl(sin->sin_addr.s_addr);
            if((a >> 24) == 127 || (pass == 0 && a != primary))
                continue;
            pb->ourAddress = a;
            if(ifa->ifa_netmask)
                pb->ourNetMask = ntohl(
                    ((struct sockaddr_in *)ifa->ifa_netmask)
                        ->sin_addr.s_addr);
            err = noErr;
            break;
        }
        freeifaddrs(ifa0);
    }
    return err;
}

/* TCP */

OSErr do_tcp_create(TCPiopb *pb)
{
    auto &create = pb->csParam.create;

    if(!create.rcvBuff || create.rcvBuffLen < 4096)
        return invalidBufPtr; /* MacTCP demanded >= 4K rcv buffer */

    uint32_t cookie = STREAM_COOKIE_BASE + next_stream_id++;
    MacTCPStream &s = streams[cookie];
    s.rcvBuff = create.rcvBuff;
    s.rcvBuffLen = create.rcvBuffLen;
    s.notifyProc = guest_cast<uint32_t>(create.notifyProc);
    s.userDataPtr = guest_cast<uint32_t>(create.userDataPtr);

    pb->tcpStream = guest_cast<StreamPtr>(cookie);
    return noErr;
}

void report_endpoints(int fd, TCPOpenPB &open)
{
    struct sockaddr_in a = {};
    socklen_t alen = sizeof(a);
    if(getsockname(fd, (struct sockaddr *)&a, &alen) == 0)
    {
        open.localHost = ntohl(a.sin_addr.s_addr);
        open.localPort = ntohs(a.sin_port);
    }
    alen = sizeof(a);
    if(getpeername(fd, (struct sockaddr *)&a, &alen) == 0)
    {
        open.remoteHost = ntohl(a.sin_addr.s_addr);
        open.remotePort = ntohs(a.sin_port);
    }
}

/* A finished connect: noErr, or why it failed (and the socket is gone). */
OSErr finish_connect(MacTCPStream &s, TCPiopb *pb)
{
    int soerr = 0;
    socklen_t slen = sizeof(soerr);
    getsockopt(s.fd, SOL_SOCKET, SO_ERROR, &soerr, &slen);
    if(soerr)
    {
        close(s.fd);
        s.fd = -1;
        s.state = StreamState::created;
        return map_socket_errno(soerr);
    }
    report_endpoints(s.fd, pb->csParam.open);
    s.state = StreamState::established;
    return noErr;
}

OSErr do_tcp_active_open(TCPiopb *pb)
{
    OSErr err = noErr;
    MacTCPStream *s = lookup(pb->tcpStream, &err, false);
    if(!s)
        return err;
    if(s->fd >= 0 || s->listen_fd >= 0)
        return connectionExists;

    auto &open = pb->csParam.open;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0)
        return insufficientResources;
    set_nonblocking(fd);

    if(open.localPort)
    {
        struct sockaddr_in la = {};
        la.sin_family = AF_INET;
        la.sin_port = htons(open.localPort);
        la.sin_addr.s_addr = htonl(open.localHost);
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        if(bind(fd, (struct sockaddr *)&la, sizeof(la)) < 0)
        {
            err = map_socket_errno(errno);
            close(fd);
            return err;
        }
    }

    struct sockaddr_in ra = {};
    ra.sin_family = AF_INET;
    ra.sin_port = htons(open.remotePort);
    ra.sin_addr.s_addr = htonl(open.remoteHost);

    if(connect(fd, (struct sockaddr *)&ra, sizeof(ra)) < 0
       && errno != EINPROGRESS)
    {
        err = map_socket_errno(errno);
        close(fd);
        return err;
    }
    s->fd = fd;
    s->state = StreamState::connecting;

    if(is_async(pb) && !ready_now(fd, POLLOUT))
    {
        s->open = Parked{ pb_addr(pb),
                          deadline_after(open.ulpTimeoutValue
                                             ? open.ulpTimeoutValue
                                             : 60) };
        return inProgress;
    }

    int r = poll_one(fd, POLLOUT, open.ulpTimeoutValue, 60);
    if(r <= 0)
    {
        err = r == 0 ? openFailed : map_socket_errno(errno);
        close(fd);
        s->fd = -1;
        s->state = StreamState::created;
        return err;
    }
    return finish_connect(*s, pb);
}

/* A peer for a passive open: noErr, or inProgress while none came. */
OSErr try_accept(MacTCPStream &s, TCPiopb *pb)
{
    int fd = accept(s.listen_fd, nullptr, nullptr);
    if(fd < 0)
        return errno == EAGAIN || errno == EWOULDBLOCK
                   ? inProgress
                   : map_socket_errno(errno);
    close(s.listen_fd);
    s.listen_fd = -1;
    set_nonblocking(fd);
    s.fd = fd;
    s.state = StreamState::established;
    report_endpoints(fd, pb->csParam.open);
    return noErr;
}

OSErr do_tcp_passive_open(TCPiopb *pb)
{
    OSErr err = noErr;
    MacTCPStream *s = lookup(pb->tcpStream, &err, false);
    if(!s)
        return err;
    if(s->fd >= 0 || s->listen_fd >= 0)
        return connectionExists;

    auto &open = pb->csParam.open;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0)
        return insufficientResources;
    set_nonblocking(fd);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in la = {};
    la.sin_family = AF_INET;
    la.sin_port = htons(open.localPort);
    la.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(fd, (struct sockaddr *)&la, sizeof(la)) < 0 || listen(fd, 1) < 0)
    {
        err = map_socket_errno(errno);
        close(fd);
        return err;
    }
    socklen_t alen = sizeof(la);
    if(getsockname(fd, (struct sockaddr *)&la, &alen) == 0)
        open.localPort = ntohs(la.sin_port);
    s->listen_fd = fd;
    s->state = StreamState::listening;

    /* commandTimeoutValue: how long to wait for a peer; 0 = forever. */
    if(is_async(pb))
    {
        err = try_accept(*s, pb);
        if(err == inProgress)
            s->open = Parked{ pb_addr(pb),
                              deadline_after(open.commandTimeoutValue) };
        return err;
    }
    int timeout = open.commandTimeoutValue ? open.commandTimeoutValue * 1000 : -1;
    struct pollfd p = { fd, POLLIN, 0 };
    if(poll(&p, 1, timeout) <= 0)
    {
        close(fd);
        s->listen_fd = -1;
        s->state = StreamState::created;
        return commandTimeout;
    }
    return try_accept(*s, pb);
}

OSErr do_tcp_send(TCPiopb *pb)
{
    OSErr err = noErr;
    MacTCPStream *s = lookup(pb->tcpStream, &err, false);
    if(!s)
        return err;
    if(s->fd < 0 || s->state != StreamState::established)
        return connectionDoesntExist;

    std::vector<struct iovec> iov;
    uint32_t total;
    if((err = gather(pb->csParam.send.wdsPtr, iov, total)))
        return err;

    /* Loop until fully written (sockets are non-blocking). */
    uint32_t written = 0;
    size_t first = 0;
    while(written < total)
    {
        ssize_t r = writev(s->fd, iov.data() + first, iov.size() - first);
        if(r < 0)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                if(poll_one(s->fd, POLLOUT,
                            pb->csParam.send.ulpTimeoutValue, 60)
                   <= 0)
                    return commandTimeout;
                continue;
            }
            return map_socket_errno(errno);
        }
        written += r;
        /* Advance iov past what was written. */
        while(r > 0 && first < iov.size())
        {
            if((size_t)r >= iov[first].iov_len)
                r -= iov[first++].iov_len;
            else
            {
                iov[first].iov_base = (char *)iov[first].iov_base + r;
                iov[first].iov_len -= r;
                r = 0;
            }
        }
    }
    if(pb->csParam.send.pushFlag)
    {
        int one = 1;
        setsockopt(s->fd, IPPROTO_TCP, TCP_NODELAY, &one,
                   sizeof(one));
    }
    return noErr;
}

/* One TCPRcv or TCPNoCopyRcv attempt: inProgress if no data yet. */
OSErr try_tcp_rcv(MacTCPStream &s, TCPiopb *pb)
{
    if(s.fd < 0)
        return connectionDoesntExist;
    auto &rcv = pb->csParam.receive;
    if(pb->csCode == TCPNoCopyRcv)
    {
        rdsEntry *rds = guest_cast<rdsEntry *>(rcv.rdsPtr);
        if(!rds || rcv.rdsLength < 2)
            return invalidRDS;
        int avail = 0;
        if(ioctl(s.fd, FIONREAD, &avail) < 0 || avail <= 0)
        {
            char c;
            ssize_t r = recv(s.fd, &c, 1, MSG_PEEK | MSG_DONTWAIT);
            if(r == 0)
            {
                s.remoteClosed = true;
                return connectionClosing;
            }
            if(r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                return map_socket_errno(errno);
            return inProgress;
        }
        uint32_t n = avail;
        auto off = lend(s, n, true);
        if(!off)
        {
            s.areaFull = true;
            return inProgress;
        }
        ssize_t r = recv(s.fd, (void *)(s.rcvBuff + *off), n, MSG_DONTWAIT);
        if(r <= 0)
        {
            give_back(s, *off);
            return r == 0 ? connectionClosing : map_socket_errno(errno);
        }
        s.lent.back().second = r;
        rds[0].length = r;
        rds[0].ptr = s.rcvBuff + *off;
        rds[1].length = 0;
        rcv.urgentFlag = 0;
        rcv.markFlag = 0;
        return noErr;
    }

    Ptr buf = rcv.rcvBuff;
    uint16_t want = rcv.rcvBuffLen;
    if(!buf || !want)
        return invalidBufPtr;
    ssize_t r = recv(s.fd, (void *)buf, want, MSG_DONTWAIT);
    if(r < 0)
        return errno == EAGAIN || errno == EWOULDBLOCK
                   ? inProgress
                   : map_socket_errno(errno);
    if(r == 0)
    {
        /* Peer FIN: report closing; further reads after drain
         * report connectionTerminated per MacTCP semantics. */
        s.remoteClosed = true;
        rcv.rcvBuffLen = 0;
        return connectionClosing;
    }
    rcv.rcvBuffLen = (uint16_t)r;
    rcv.urgentFlag = 0;
    rcv.markFlag = 0;
    return noErr;
}

OSErr do_tcp_rcv(TCPiopb *pb)
{
    OSErr err = noErr;
    MacTCPStream *s = lookup(pb->tcpStream, &err, false);
    if(!s)
        return err;
    if(s->fd < 0)
        return connectionDoesntExist;
    s->arrivalNotified = false;

    auto &rcv = pb->csParam.receive;
    if(!s->reads.empty())
        err = inProgress; /* behind the calls already waiting */
    else
        err = try_tcp_rcv(*s, pb);
    if(err != inProgress)
        return err;
    if(is_async(pb))
    {
        s->reads.push_back({ pb_addr(pb),
                             deadline_after(rcv.commandTimeoutValue) });
        return inProgress;
    }
    /* Synchronous: wait here; commandTimeoutValue 0 is the default. */
    auto deadline = deadline_after(rcv.commandTimeoutValue
                                       ? rcv.commandTimeoutValue
                                       : 60);
    while((err = try_tcp_rcv(*s, pb)) == inProgress)
    {
        auto left = std::chrono::ceil<std::chrono::milliseconds>(
            deadline - Clock::now()).count();
        if(left <= 0)
            return commandTimeout;
        struct pollfd p = { s->fd, POLLIN, 0 };
        poll(&p, 1, (int)left);
    }
    return err;
}

OSErr do_tcp_bfr_return(TCPiopb *pb)
{
    OSErr err = noErr;
    MacTCPStream *s = lookup(pb->tcpStream, &err, false);
    if(!s)
        return err;
    rdsEntry *rds = guest_cast<rdsEntry *>(pb->csParam.receive.rdsPtr);
    if(!rds)
        return invalidRDS;
    for(int i = 0; rds[i].length; i++)
    {
        Ptr p = rds[i].ptr;
        if(p < s->rcvBuff || !give_back(*s, p - s->rcvBuff))
            return invalidRDS;
    }
    return noErr;
}

/* Finish every parked call on a stream with err (stream torn down). */
void fail_parked(MacTCPStream &s, OSErr err, std::vector<std::pair<uint32_t, OSErr>> &done)
{
    if(s.open)
        done.push_back({ s.open->pb, err });
    for(auto &p : s.reads)
        done.push_back({ p.pb, err });
    s.open.reset();
    s.reads.clear();
}

void finish_all(const std::vector<std::pair<uint32_t, OSErr>> &done)
{
    for(auto [pb, err] : done)
        complete(pb_at(pb), err);
}

OSErr do_tcp_close(TCPiopb *pb)
{
    OSErr err = noErr;
    MacTCPStream *s = lookup(pb->tcpStream, &err, false);
    if(!s)
        return err;
    if(s->fd < 0)
        return connectionDoesntExist;
    shutdown(s->fd, SHUT_WR); /* half-close; stream stays readable */
    s->state = StreamState::closing;
    return noErr;
}

OSErr do_tcp_abort(TCPiopb *pb, std::vector<std::pair<uint32_t, OSErr>> &done)
{
    OSErr err = noErr;
    MacTCPStream *s = lookup(pb->tcpStream, &err, false);
    if(!s)
        return err;
    if(s->fd >= 0)
    {
        struct linger lg = { 1, 0 }; /* RST on close */
        setsockopt(s->fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
        close(s->fd);
        s->fd = -1;
    }
    if(s->listen_fd >= 0)
    {
        close(s->listen_fd);
        s->listen_fd = -1;
    }
    s->state = StreamState::terminated;
    fail_parked(*s, connectionTerminated, done);
    return noErr;
}

OSErr do_tcp_status(TCPiopb *pb)
{
    OSErr err = noErr;
    MacTCPStream *s = lookup(pb->tcpStream, &err, false);
    if(!s)
        return err;

    auto &st = pb->csParam.status;
    std::memset((void *)&st, 0, sizeof(st));

    switch(s->state)
    {
        case StreamState::listening:
            st.connectionState = TCPSListen;
            break;
        case StreamState::connecting:
            st.connectionState = TCPSSynSent;
            break;
        case StreamState::established:
            st.connectionState = s->remoteClosed ? TCPSCloseWait
                                                 : TCPSEstablished;
            break;
        case StreamState::closing:
            st.connectionState = s->remoteClosed ? TCPSClosing
                                                 : TCPSFinWait1;
            break;
        case StreamState::terminated:
        case StreamState::created:
        default:
            st.connectionState = TCPSClosed;
            break;
    }
    if(s->fd >= 0)
    {
        int avail = 0;
        if(ioctl(s->fd, FIONREAD, &avail) == 0)
            st.amtUnreadData = (uint16_t)std::min(avail, 0xffff);

        struct sockaddr_in a = {};
        socklen_t alen = sizeof(a);
        if(getpeername(s->fd, (struct sockaddr *)&a, &alen) == 0)
        {
            st.remoteHost = ntohl(a.sin_addr.s_addr);
            st.remotePort = ntohs(a.sin_port);
        }
        alen = sizeof(a);
        if(getsockname(s->fd, (struct sockaddr *)&a, &alen) == 0)
        {
            st.localHost = ntohl(a.sin_addr.s_addr);
            st.localPort = ntohs(a.sin_port);
        }
        st.sendWindow = 0xFFFF;
        st.rcvWindow = (uint16_t)std::min<uint32_t>(s->rcvBuffLen, 0xFFFF);
    }
    return noErr;
}

void release(uint32_t cookie, std::vector<std::pair<uint32_t, OSErr>> &done)
{
    MacTCPStream &s = streams[cookie];
    if(s.fd >= 0)
        close(s.fd);
    if(s.listen_fd >= 0)
        close(s.listen_fd);
    fail_parked(s, connectionTerminated, done);
    streams.erase(cookie);
}

OSErr do_tcp_release(TCPiopb *pb, std::vector<std::pair<uint32_t, OSErr>> &done)
{
    OSErr err = noErr;
    MacTCPStream *s = lookup(pb->tcpStream, &err, false);
    if(!s)
        return err;
    /* Hand the receive area back to the app, as MacTCP documented. */
    pb->csParam.create.rcvBuff = s->rcvBuff;
    pb->csParam.create.rcvBuffLen = s->rcvBuffLen;
    release(cookie_of(pb->tcpStream), done);
    return noErr;
}

/* UDP */

OSErr do_udp_create(UDPiopb *pb)
{
    auto &create = pb->csParam.create;
    if(!create.rcvBuff || create.rcvBuffLen < 2048)
        return invalidBufPtr; /* MacTCP demanded >= 2K */

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd < 0)
        return insufficientResources;
    set_nonblocking(fd);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    struct sockaddr_in la = {};
    la.sin_family = AF_INET;
    la.sin_port = htons(create.localPort);
    la.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(fd, (struct sockaddr *)&la, sizeof(la)) < 0)
    {
        OSErr err = map_socket_errno(errno);
        close(fd);
        return err;
    }
    socklen_t alen = sizeof(la);
    if(getsockname(fd, (struct sockaddr *)&la, &alen) == 0)
        create.localPort = ntohs(la.sin_port);

    uint32_t cookie = STREAM_COOKIE_BASE + next_stream_id++;
    MacTCPStream &s = streams[cookie];
    s.udp = true;
    s.fd = fd;
    s.state = StreamState::established;
    s.rcvBuff = create.rcvBuff;
    s.rcvBuffLen = create.rcvBuffLen;
    s.notifyProc = guest_cast<uint32_t>(create.notifyProc);
    s.userDataPtr = guest_cast<uint32_t>(create.userDataPtr);
    pb->udpStream = guest_cast<StreamPtr>(cookie);
    return noErr;
}

/* The size of the next datagram, or -1 if there is none. */
ssize_t next_datagram_size(int fd)
{
#if defined(__APPLE__)
    if(!ready_now(fd, POLLIN))
        return -1;
    int n = 0;
    socklen_t len = sizeof n;
    return getsockopt(fd, SOL_SOCKET, SO_NREAD, &n, &len) == 0 ? n : -1;
#else
    return recv(fd, nullptr, 0, MSG_PEEK | MSG_TRUNC | MSG_DONTWAIT);
#endif
}

/* One UDPRead attempt: inProgress if no datagram (or no room) yet. */
OSErr try_udp_read(MacTCPStream &s, UDPiopb *pb)
{
    auto &rcv = pb->csParam.receive;
    ssize_t size = next_datagram_size(s.fd);
    if(size < 0)
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == 0
                   ? inProgress
                   : map_socket_errno(errno);
    /* One too big for the whole area is cut to fit. */
    uint32_t n = std::min<uint32_t>(size, s.rcvBuffLen);
    auto off = lend(s, n, false);
    if(!off)
    {
        s.areaFull = true;
        return inProgress;
    }
    struct sockaddr_in from = {};
    socklen_t flen = sizeof from;
    ssize_t r = recvfrom(s.fd, (void *)(s.rcvBuff + *off), n, MSG_DONTWAIT,
                         (struct sockaddr *)&from, &flen);
    if(r < 0)
    {
        give_back(s, *off);
        return errno == EAGAIN || errno == EWOULDBLOCK
                   ? inProgress
                   : map_socket_errno(errno);
    }
    rcv.remoteHost = ntohl(from.sin_addr.s_addr);
    rcv.remotePort = ntohs(from.sin_port);
    rcv.rcvBuff = s.rcvBuff + *off;
    rcv.rcvBuffLen = (uint16_t)r;
    return noErr;
}

OSErr do_udp_read(UDPiopb *pb)
{
    OSErr err = noErr;
    MacTCPStream *s = lookup(pb->udpStream, &err, true);
    if(!s)
        return err;
    s->arrivalNotified = false;
    auto &rcv = pb->csParam.receive;
    if(!s->reads.empty())
        err = inProgress;
    else
        err = try_udp_read(*s, pb);
    if(err != inProgress)
        return err;
    /* timeOut in seconds; 0 waits for as long as it takes. */
    auto deadline = deadline_after(rcv.timeOut);
    if(is_async(pb))
    {
        s->reads.push_back({ pb_addr(pb), deadline });
        return inProgress;
    }
    while((err = try_udp_read(*s, pb)) == inProgress)
    {
        int timeout = -1;
        if(deadline != never)
        {
            auto left = std::chrono::ceil<std::chrono::milliseconds>(
                deadline - Clock::now()).count();
            if(left <= 0)
                return commandTimeout;
            timeout = (int)left;
        }
        if(s->areaFull)
            return insufficientResources; /* nothing can give it back */
        struct pollfd p = { s->fd, POLLIN, 0 };
        poll(&p, 1, timeout);
    }
    return err;
}

OSErr do_udp_bfr_return(UDPiopb *pb)
{
    OSErr err = noErr;
    MacTCPStream *s = lookup(pb->udpStream, &err, true);
    if(!s)
        return err;
    Ptr p = pb->csParam.receive.rcvBuff;
    if(p < s->rcvBuff || !give_back(*s, p - s->rcvBuff))
        return invalidBufPtr;
    return noErr;
}

OSErr do_udp_write(UDPiopb *pb)
{
    OSErr err = noErr;
    MacTCPStream *s = lookup(pb->udpStream, &err, true);
    if(!s)
        return err;
    auto &send = pb->csParam.send;
    std::vector<struct iovec> iov;
    uint32_t total;
    if((err = gather(send.wdsPtr, iov, total)))
        return err;
    if(total > 65507)
        return invalidLength;

    struct sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(send.remotePort);
    to.sin_addr.s_addr = htonl(send.remoteHost);
    struct msghdr msg = {};
    msg.msg_name = &to;
    msg.msg_namelen = sizeof to;
    msg.msg_iov = iov.data();
    msg.msg_iovlen = iov.size();
    for(;;)
    {
        if(sendmsg(s->fd, &msg, 0) >= 0)
            break;
        if(errno == EAGAIN || errno == EWOULDBLOCK)
        {
            if(poll_one(s->fd, POLLOUT, 0, 10) <= 0)
                return commandTimeout;
            continue;
        }
        if(log_enabled())
            fprintf(stderr, "[mactcp] UDPWrite to %08x:%d: %s\n",
                    (uint32_t)send.remoteHost, (int)send.remotePort,
                    strerror(errno));
        return errno == EMSGSIZE ? invalidLength : map_socket_errno(errno);
    }
    return noErr;
}

OSErr do_udp_release(UDPiopb *pb, std::vector<std::pair<uint32_t, OSErr>> &done)
{
    OSErr err = noErr;
    MacTCPStream *s = lookup(pb->udpStream, &err, true);
    if(!s)
        return err;
    pb->csParam.create.rcvBuff = s->rcvBuff;
    pb->csParam.create.rcvBuffLen = s->rcvBuffLen;
    release(cookie_of(pb->udpStream), done);
    return noErr;
}

/* ---- interrupt-time service -----------------------------------------
 * Runs from the network interrupt: finishes the parked calls whose
 * sockets are ready or whose time ran out, and delivers ASRs.
 * Completion routines and ASRs are 68k code that may call the driver
 * again (and release the stream), so the stream is looked up afresh
 * after each one.
 */
void service(uint32_t cookie)
{
    MacTCPStream *s = find(cookie);
    if(!s)
        return;
    auto now = Clock::now();

    if(s->open)
    {
        Parked p = *s->open;
        TCPiopb *pb = pb_at(p.pb);
        OSErr err = inProgress;
        if(s->state == StreamState::connecting)
        {
            if(ready_now(s->fd, POLLOUT))
                err = finish_connect(*s, pb);
            else if(now >= p.deadline)
            {
                close(s->fd);
                s->fd = -1;
                s->state = StreamState::created;
                err = openFailed;
            }
        }
        else if(s->state == StreamState::listening)
        {
            err = try_accept(*s, pb);
            if(err == inProgress && now >= p.deadline)
            {
                close(s->listen_fd);
                s->listen_fd = -1;
                s->state = StreamState::created;
                err = commandTimeout;
            }
        }
        if(err != inProgress)
        {
            s->open.reset();
            complete(pb, err);
            if(!(s = find(cookie)))
                return;
        }
    }

    while(!s->reads.empty())
    {
        Parked p = s->reads.front();
        OSErr err = s->udp ? try_udp_read(*s, (UDPiopb *)pb_at(p.pb))
                           : try_tcp_rcv(*s, pb_at(p.pb));
        if(err == inProgress)
        {
            if(now < p.deadline)
                break;
            err = commandTimeout;
        }
        s->reads.pop_front();
        complete(pb_at(p.pb), err);
        if(!(s = find(cookie)))
            return;
    }

    /* ASRs, for data nobody has asked for yet. */
    if(s->reads.empty() && s->notifyProc && s->fd >= 0
       && !s->arrivalNotified && !s->closingNotified
       && (s->udp || s->state == StreamState::established
           || s->state == StreamState::closing)
       && ready_now(s->fd, POLLIN))
    {
        if(s->udp)
        {
            s->arrivalNotified = true;
            call_asr(cookie, *s, UDPDataArrival, 0);
            return;
        }
        char c;
        ssize_t r = recv(s->fd, &c, 1, MSG_PEEK | MSG_DONTWAIT);
        if(r > 0)
        {
            s->arrivalNotified = true;
            call_asr(cookie, *s, TCPDataArrival, 0);
        }
        else if(r == 0)
        {
            s->remoteClosed = true;
            s->closingNotified = true;
            call_asr(cookie, *s, TCPClosing, 0);
        }
        else if(errno != EAGAIN && errno != EWOULDBLOCK)
        {
            s->closingNotified = true;
            s->state = StreamState::terminated;
            call_asr(cookie, *s, TCPTerminate, TCPRemoteAbort);
        }
    }
}

bool pumping = false;
bool pump_again = false;

void pump()
{
    if(pumping)
    {
        pump_again = true;
        return;
    }
    pumping = true;
    do
    {
        pump_again = false;
        std::vector<uint32_t> cookies;
        for(auto &kv : streams)
            cookies.push_back(kv.first);
        for(uint32_t c : cookies)
            service(c);
        ROMlib_dnr_pump();
    } while(pump_again);
    pumping = false;
    rewatch();
}

const char *cs_name(int cs)
{
    switch(cs)
    {
        case ipctlGetAddr: return "ipctlGetAddr";
        case udpCreate: return "UDPCreate";
        case udpRead: return "UDPRead";
        case udpBfrReturn: return "UDPBfrReturn";
        case udpWrite: return "UDPWrite";
        case udpRelease: return "UDPRelease";
        case udpMaxMTUSize: return "UDPMaxMTUSize";
        case udpStatus: return "UDPStatus";
        case TCPCreate: return "TCPCreate";
        case TCPPassiveOpen: return "TCPPassiveOpen";
        case TCPActiveOpen: return "TCPActiveOpen";
        case TCPSend: return "TCPSend";
        case TCPNoCopyRcv: return "TCPNoCopyRcv";
        case TCPRcvBfrReturn: return "TCPRcvBfrReturn";
        case TCPRcv: return "TCPRcv";
        case TCPClose: return "TCPClose";
        case TCPAbort: return "TCPAbort";
        case TCPStatus: return "TCPStatus";
        case TCPRelease: return "TCPRelease";
        default: return "?";
    }
}

} /* anonymous namespace */

void Executor::ROMlib_mactcp_wake()
{
    net_interrupt.trigger();
}

/* ---- driver entry points ------------------------------------------
 * These live at file scope rather than in the anonymous namespace
 * above because REGISTER_FUNCTION_PTR expands to an explicit template
 * instantiation, which has to be at namespace scope.  Same shape as
 * serial.cpp: a C_-prefixed implementation plus a wrapper object of
 * the register-convention calling convention the Device Manager
 * dispatches through.
 */

static OSErr C_ROMlib_ippopen(ParmBlkPtr pbp, DCtlPtr dce);
REGISTER_FUNCTION_PTR(ROMlib_ippopen, D0(A0, A1));
static OSErr C_ROMlib_ippprime(ParmBlkPtr pbp, DCtlPtr dce);
REGISTER_FUNCTION_PTR(ROMlib_ippprime, D0(A0, A1));
static OSErr C_ROMlib_ippctl(ParmBlkPtr pbp, DCtlPtr dce);
REGISTER_FUNCTION_PTR(ROMlib_ippctl, D0(A0, A1));
static OSErr C_ROMlib_ippstatus(ParmBlkPtr pbp, DCtlPtr dce);
REGISTER_FUNCTION_PTR(ROMlib_ippstatus, D0(A0, A1));
static OSErr C_ROMlib_ippclose(ParmBlkPtr pbp, DCtlPtr dce);
REGISTER_FUNCTION_PTR(ROMlib_ippclose, D0(A0, A1));

static OSErr C_ROMlib_ippopen(ParmBlkPtr pbp, DCtlPtr dce)
{
    (void)pbp;
    (void)dce;
    /* No accRun: the watcher thread and the network interrupt do the
     * driver's background work. */
    return noErr;
}

static OSErr C_ROMlib_ippprime(ParmBlkPtr pbp, DCtlPtr dce)
{
    /* MacTCP has no Read/Write interface; everything is Control. */
    (void)pbp;
    (void)dce;
    return controlErr;
}

static OSErr C_ROMlib_ippctl(ParmBlkPtr pbp, DCtlPtr dce)
{
    (void)dce;
    TCPiopb *pb = (TCPiopb *)pbp;
    UDPiopb *upb = (UDPiopb *)pbp;
    OSErr err;
    /* Calls a teardown finished early, completed after this one. */
    std::vector<std::pair<uint32_t, OSErr>> done;

    switch(pb->csCode)
    {
        case accRun:
            return noErr; /* housekeeping: no completion semantics */

        case killCode:
            return complete(pb, noErr);

        case ipctlGetAddr:
            err = do_getaddr((GetAddrParamBlock *)pbp);
            break;

        case TCPCreate:
            err = do_tcp_create(pb);
            break;
        case TCPPassiveOpen:
            err = do_tcp_passive_open(pb);
            break;
        case TCPActiveOpen:
            err = do_tcp_active_open(pb);
            break;
        case TCPSend:
            err = do_tcp_send(pb);
            break;
        case TCPRcv:
        case TCPNoCopyRcv:
            err = do_tcp_rcv(pb);
            break;
        case TCPRcvBfrReturn:
            err = do_tcp_bfr_return(pb);
            break;
        case TCPClose:
            err = do_tcp_close(pb);
            break;
        case TCPAbort:
            err = do_tcp_abort(pb, done);
            break;
        case TCPStatus:
            err = do_tcp_status(pb);
            break;
        case TCPRelease:
            err = do_tcp_release(pb, done);
            break;

        case udpCreate:
            err = do_udp_create(upb);
            break;
        case udpRead:
            err = do_udp_read(upb);
            break;
        case udpBfrReturn:
            err = do_udp_bfr_return(upb);
            break;
        case udpWrite:
            err = do_udp_write(upb);
            break;
        case udpRelease:
            err = do_udp_release(upb, done);
            break;
        case udpMaxMTUSize:
            /* An Ethernet frame less the IP and UDP headers. */
            upb->csParam.mtu.mtuSize = 1472;
            err = noErr;
            break;
        case udpStatus:
        {
            OSErr e = noErr;
            err = lookup(upb->udpStream, &e, true) ? noErr : e;
            break;
        }

        case udpMultiCreate:
        case udpMultiSend:
        case udpMultiRead:
        case TCPExtendedStat:
        case TCPGlobalInfo:
            warning_unimplemented("MacTCP csCode %d", (int)pb->csCode);
            err = invalidLength; /* TODO: most fitting stub error? */
            break;

        default:
            warning_unexpected("MacTCP unknown csCode %d",
                               (int)pb->csCode);
            err = controlErr;
            break;
    }
    if(log_enabled())
        fprintf(stderr, "[mactcp] %s%s stream %08x -> %d\n",
                cs_name(pb->csCode), is_async(pb) ? " (async)" : "",
                cookie_of(pb->tcpStream), err);
    if(err == inProgress)
    {
        pb->ioResult = inProgress;
        rewatch();
        finish_all(done);
        return noErr;
    }
    rewatch();
    complete(pb, err);
    finish_all(done);
    return err;
}

static OSErr C_ROMlib_ippstatus(ParmBlkPtr pbp, DCtlPtr dce)
{
    /* MacTCP routes its status-ish calls (TCPStatus etc.) through
     * Control; a bare PBStatus on .IPP has nothing to report. */
    (void)dce;
    return complete((TCPiopb *)pbp, controlErr);
}

static OSErr C_ROMlib_ippclose(ParmBlkPtr pbp, DCtlPtr dce)
{
    (void)pbp;
    (void)dce;
    std::vector<std::pair<uint32_t, OSErr>> done;
    std::vector<uint32_t> cookies;
    for(auto &kv : streams)
        cookies.push_back(kv.first);
    for(uint32_t c : cookies)
        release(c, done);
    rewatch();
    finish_all(done);
    return noErr;
}

/* ---- registration --------------------------------------------------
 * Unit slot: 47 (refnum -48), the top of the 48-entry unit table —
 * far from .Sony (-5) and the serial units (-6..-9).  Real MacTCP
 * grabbed whatever free unit the Device Manager gave it, and no
 * application may depend on the number: they must PBOpen by name.
 */
void Executor::InitMacTCPDriver()
{
    RegisterDriver({
        &ROMlib_ippopen, &ROMlib_ippprime, &ROMlib_ippctl,
        &ROMlib_ippstatus, &ROMlib_ippclose,
        (StringPtr) "\04.IPP", -48,
    });
}
