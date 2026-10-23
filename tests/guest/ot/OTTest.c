/*
 * OTTest.c -- an MPW tool that exercises Open Transport's client API the
 * way 68k applications reach it: through Apple's static OT glue
 * (OpenTransport.o, OpenTransportApp.o, OpenTptInet.o), which binds to OT's
 * function sets through the Shared Library Manager.
 *
 *     OTTest [case ...]       no case: all of them
 *
 * The echo server (TCP and UDP on one port) is named in Host:net_echo.txt
 * as "address port", written by the test harness (tests/lib/echo_server.py).
 * One line per case: "PASS name" or "FAIL name: why"; then
 * "DONE <failures>".  Built by build.mpw.sh in MPW (tests/guest/ot/).
 */

#include <Types.h>
#include <Memory.h>
#include <Events.h>
#include <OSUtils.h>
#include <Gestalt.h>
#include <Timer.h>
#include <OpenTransport.h>
#include <OpenTptInternet.h>
#include <OpenTptLinks.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static InetHost echo_host = 0;
static InetPort echo_port = 0;

static void pass(const char *name)
{
    printf("PASS %s\n", name);
}

static void fail(const char *name, const char *why, long code)
{
    printf("FAIL %s: %s (%ld)\n", name, why, code);
    failures++;
}

/* Wait up to secs seconds for *flag to become nonzero. */
static Boolean wait_for(volatile long *flag, long secs)
{
    unsigned long until = TickCount() + secs * 60;
    while(!*flag)
    {
        if(TickCount() > until)
            return false;
    }
    return true;
}

static void fill_pattern(char *buf, long n, int seed)
{
    long i;
    for(i = 0; i < n; i++)
        buf[i] = (char)((i * 7 + seed) & 0xFF);
}

static void read_echo_config(void)
{
    FILE *f = fopen("Host:net_echo.txt", "r");
    char addr[64];
    unsigned int port;
    unsigned int a, b, c, d;

    if(!f)
        return;
    if(fscanf(f, "%63s %u", addr, &port) == 2
       && sscanf(addr, "%u.%u.%u.%u", &a, &b, &c, &d) == 4)
    {
        echo_host = ((InetHost)a << 24) | ((InetHost)b << 16) | (c << 8) | d;
        echo_port = (InetPort)port;
    }
    fclose(f);
}

/* ---- Gestalt and initialisation -------------------------------------- */

static void test_gestalt(void)
{
    long attr = 0, vers = 0;
    OSErr err = Gestalt(gestaltOpenTpt, &attr);

    if(err)
    {
        fail("gestalt", "Gestalt('otan')", err);
        return;
    }
    if(!(attr & gestaltOpenTptPresent) || !(attr & gestaltOpenTptTCPPresent))
    {
        fail("gestalt", "'otan' lacks present bits", attr);
        return;
    }
    err = Gestalt(gestaltOpenTptVersions, &vers);
    if(err || vers == 0)
    {
        fail("gestalt", "Gestalt('otvr')", err ? err : vers);
        return;
    }
    printf("# otan %08lx otvr %08lx\n", attr, vers);
    pass("gestalt");
}

static Boolean test_init(void)
{
    OSStatus err = InitOpenTransport();
    if(err)
    {
        fail("init", "InitOpenTransport", err);
        return false;
    }
    pass("init");
    return true;
}

/* ---- utilities ------------------------------------------------------- */

static void test_utils(void)
{
    char *p = (char *)OTAllocMem(1000);
    char src[32], dst[32];
    OTTimeStamp t0;
    UInt32 ms;
    long ticks;

    if(!p)
    {
        fail("utils", "OTAllocMem", 0);
        return;
    }
    OTMemzero(p, 1000);
    if(p[0] || p[999])
    {
        fail("utils", "OTMemzero", 0);
        return;
    }
    OTFreeMem(p);

    strcpy(src, "open transport");
    OTMemcpy(dst, src, 15);
    if(!OTMemcmp(dst, src, 15))
    {
        fail("utils", "OTMemcpy/OTMemcmp", 0);
        return;
    }
    if(OTStrLength(src) != 14)
    {
        fail("utils", "OTStrLength", OTStrLength(src));
        return;
    }

    OTGetTimeStamp(&t0);
    Delay(30, &ticks); /* half a second */
    ms = OTElapsedMilliseconds(&t0);
    if(ms < 300 || ms > 3000)
    {
        fail("utils", "OTElapsedMilliseconds after 30 ticks", ms);
        return;
    }
    pass("utils");
}

/* ---- name resolution ------------------------------------------------- */

static void test_dns_sync(void)
{
    OSStatus err;
    InetSvcRef svc = OTOpenInternetServices(kDefaultInternetServicesPath, 0, &err);
    InetHostInfo info;

    if(err || !svc)
    {
        fail("dns_sync", "OTOpenInternetServices", err);
        return;
    }
    err = OTInetStringToAddress(svc, "127.0.0.1", &info);
    if(err || info.addrs[0] != 0x7F000001)
    {
        fail("dns_sync", "dotted 127.0.0.1", err ? err : (long)info.addrs[0]);
        OTCloseProvider(svc);
        return;
    }
    err = OTInetStringToAddress(svc, "localhost", &info);
    if(err || info.addrs[0] == 0)
    {
        fail("dns_sync", "localhost", err);
        OTCloseProvider(svc);
        return;
    }
    printf("# localhost -> %s %08lx\n", info.name, (long)info.addrs[0]);
    err = OTInetStringToAddress(svc, "no-such-host.invalid", &info);
    if(err == noErr)
    {
        fail("dns_sync", "no-such-host.invalid resolved", 0);
        OTCloseProvider(svc);
        return;
    }
    printf("# no-such-host.invalid -> %ld\n", (long)err);
    OTCloseProvider(svc);
    pass("dns_sync");
}

static volatile long dns_done, dns_open;
static volatile OSStatus dns_result;

static pascal void dns_notifier(void *ctx, OTEventCode code, OTResult result, void *cookie)
{
    if(code == T_OPENCOMPLETE)
    {
        /* The new provider comes in the cookie. */
        *(InetSvcRef *)ctx = (InetSvcRef)cookie;
        dns_open = 1;
    }
    else if(code == T_DNRSTRINGTOADDRCOMPLETE)
    {
        dns_result = result;
        dns_done = 1;
    }
}

static void test_dns_async(void)
{
    OSStatus err;
    InetSvcRef svc = 0;
    InetHostInfo info;

    dns_open = dns_done = 0;
    err = OTAsyncOpenInternetServices(kDefaultInternetServicesPath, 0,
                                      dns_notifier, (void *)&svc);
    if(err)
    {
        fail("dns_async", "OTAsyncOpenInternetServices", err);
        return;
    }
    if(!wait_for(&dns_open, 10))
    {
        fail("dns_async", "no T_OPENCOMPLETE", 0);
        return;
    }
    if(!svc)
    {
        fail("dns_async", "provider not delivered", 0);
        return;
    }
    err = OTInetStringToAddress(svc, "localhost", &info);
    if(err)
    {
        fail("dns_async", "OTInetStringToAddress", err);
        OTCloseProvider(svc);
        return;
    }
    if(!wait_for(&dns_done, 20))
    {
        fail("dns_async", "no T_DNRSTRINGTOADDRCOMPLETE", 0);
        OTCloseProvider(svc);
        return;
    }
    if(dns_result || info.addrs[0] == 0)
    {
        fail("dns_async", "lookup result", dns_result);
        OTCloseProvider(svc);
        return;
    }
    OTCloseProvider(svc);
    pass("dns_async");
}

/* ---- interface info -------------------------------------------------- */

static void test_iface(void)
{
    InetInterfaceInfo info;
    OSStatus err = OTInetGetInterfaceInfo(&info, kDefaultInetInterface);

    if(err)
    {
        fail("iface", "OTInetGetInterfaceInfo", err);
        return;
    }
    if(info.fAddress == 0)
    {
        fail("iface", "no address", 0);
        return;
    }
    printf("# iface %08lx mask %08lx\n", (long)info.fAddress, (long)info.fNetmask);
    pass("iface");
}

/* ---- TCP ------------------------------------------------------------- */

#define XFER 3000

static EndpointRef open_tcp(OSStatus *err)
{
    return OTOpenEndpoint(OTCreateConfiguration(kTCPName), 0, nil, err);
}

static OSStatus connect_echo(EndpointRef ep)
{
    InetAddress addr;
    TCall call;

    OTInitInetAddress(&addr, echo_port, echo_host);
    OTMemzero(&call, sizeof call);
    call.addr.buf = (UInt8 *)&addr;
    call.addr.len = sizeof addr;
    return OTConnect(ep, &call, nil);
}

/* Send XFER bytes and read them back on a synchronous endpoint. */
static long echo_sync(EndpointRef ep, const char *name)
{
    static char out[XFER], in[XFER];
    long got = 0;
    OTResult r;
    OTFlags flags;
    unsigned long until;

    fill_pattern(out, XFER, 3);
    r = OTSnd(ep, out, XFER, 0);
    if(r != XFER)
    {
        fail(name, "OTSnd", r);
        return -1;
    }
    until = TickCount() + 600;
    while(got < XFER)
    {
        r = OTRcv(ep, in + got, XFER - got, &flags);
        if(r > 0)
            got += r;
        else if(r != kOTNoDataErr || TickCount() > until)
        {
            fail(name, "OTRcv", r);
            return -1;
        }
    }
    if(memcmp(in, out, XFER))
    {
        fail(name, "echoed data differs", 0);
        return -1;
    }
    return got;
}

static void close_orderly(EndpointRef ep)
{
    OTSndOrderlyDisconnect(ep);
    OTUnbind(ep);
    OTCloseProvider(ep);
}

static void test_tcp_blocking(void)
{
    OSStatus err;
    EndpointRef ep = open_tcp(&err);

    if(err)
    {
        fail("tcp_blocking", "OTOpenEndpoint", err);
        return;
    }
    OTSetSynchronous(ep);
    OTSetBlocking(ep);
    err = OTBind(ep, nil, nil);
    if(err)
    {
        fail("tcp_blocking", "OTBind", err);
        OTCloseProvider(ep);
        return;
    }
    err = connect_echo(ep);
    if(err)
    {
        fail("tcp_blocking", "OTConnect", err);
        OTUnbind(ep);
        OTCloseProvider(ep);
        return;
    }
    if(echo_sync(ep, "tcp_blocking") < 0)
    {
        OTSndDisconnect(ep, nil);
        OTUnbind(ep);
        OTCloseProvider(ep);
        return;
    }
    if(OTGetEndpointState(ep) != T_DATAXFER)
    {
        fail("tcp_blocking", "state after transfer", OTGetEndpointState(ep));
        close_orderly(ep);
        return;
    }
    close_orderly(ep);
    pass("tcp_blocking");
}

static void test_tcp_nonblocking(void)
{
    OSStatus err;
    OTResult r;
    OTFlags flags;
    char c;
    EndpointRef ep = open_tcp(&err);

    if(err)
    {
        fail("tcp_nonblocking", "OTOpenEndpoint", err);
        return;
    }
    OTSetBlocking(ep); /* connect blocking ... */
    err = OTBind(ep, nil, nil);
    if(!err)
        err = connect_echo(ep);
    if(err)
    {
        fail("tcp_nonblocking", "bind/connect", err);
        OTCloseProvider(ep);
        return;
    }
    OTSetNonBlocking(ep); /* ... then poll */
    r = OTRcv(ep, &c, 1, &flags);
    if(r != kOTNoDataErr)
    {
        fail("tcp_nonblocking", "OTRcv with nothing sent", r);
        close_orderly(ep);
        return;
    }
    if(echo_sync(ep, "tcp_nonblocking") < 0)
    {
        close_orderly(ep);
        return;
    }
    close_orderly(ep);
    pass("tcp_nonblocking");
}

/* Asynchronous, as REALbasic's sockets and most OT applications run. */
static struct
{
    EndpointRef ep;
    volatile long opened, bound, connected, data, ordrel, disconnected;
    volatile OSStatus open_result, bind_result;
} as;

static pascal void tcp_notifier(void *ctx, OTEventCode code, OTResult result, void *cookie)
{
    (void)ctx;
    switch(code)
    {
        case T_OPENCOMPLETE:
            as.open_result = result;
            as.ep = (EndpointRef)cookie;
            as.opened = 1;
            break;
        case T_BINDCOMPLETE:
            as.bind_result = result;
            as.bound = 1;
            break;
        case T_CONNECT:
            as.connected = 1;
            break;
        case T_DATA:
            as.data++;
            break;
        case T_ORDREL:
            as.ordrel = 1;
            break;
        case T_DISCONNECT:
            as.disconnected = 1;
            break;
    }
}

static void test_tcp_async(void)
{
    static char out[XFER], in[XFER];
    OSStatus err;
    OTResult r;
    OTFlags flags;
    long got = 0;
    unsigned long until;
    InetAddress addr;
    TCall call;

    memset((void *)&as, 0, sizeof as);
    err = OTAsyncOpenEndpoint(OTCreateConfiguration(kTCPName), 0, nil,
                              tcp_notifier, nil);
    if(err)
    {
        fail("tcp_async", "OTAsyncOpenEndpoint", err);
        return;
    }
    if(!wait_for(&as.opened, 10) || as.open_result)
    {
        fail("tcp_async", "T_OPENCOMPLETE", as.opened ? as.open_result : -1);
        return;
    }
    err = OTBind(as.ep, nil, nil);
    if(err || !wait_for(&as.bound, 10) || as.bind_result)
    {
        fail("tcp_async", "OTBind / T_BINDCOMPLETE", err ? err : as.bind_result);
        OTCloseProvider(as.ep);
        return;
    }
    OTInitInetAddress(&addr, echo_port, echo_host);
    OTMemzero(&call, sizeof call);
    call.addr.buf = (UInt8 *)&addr;
    call.addr.len = sizeof addr;
    err = OTConnect(as.ep, &call, nil);
    if(err != kOTNoDataErr)
    {
        fail("tcp_async", "OTConnect should answer kOTNoDataErr", err);
        OTCloseProvider(as.ep);
        return;
    }
    if(!wait_for(&as.connected, 10))
    {
        fail("tcp_async", "no T_CONNECT", 0);
        OTCloseProvider(as.ep);
        return;
    }
    err = OTRcvConnect(as.ep, nil);
    if(err)
    {
        fail("tcp_async", "OTRcvConnect", err);
        OTCloseProvider(as.ep);
        return;
    }
    fill_pattern(out, XFER, 11);
    r = OTSnd(as.ep, out, XFER, 0);
    if(r != XFER)
    {
        fail("tcp_async", "OTSnd", r);
        OTCloseProvider(as.ep);
        return;
    }
    /* Read on T_DATA until kOTNoDataErr, then wait for the next T_DATA. */
    until = TickCount() + 600;
    while(got < XFER && TickCount() < until)
    {
        long seen = as.data;
        if(!seen)
            continue;
        for(;;)
        {
            r = OTRcv(as.ep, in + got, XFER - got, &flags);
            if(r > 0)
                got += r;
            else
                break;
        }
        if(r != kOTNoDataErr)
        {
            fail("tcp_async", "OTRcv", r);
            OTCloseProvider(as.ep);
            return;
        }
        as.data = 0;
    }
    if(got != XFER || memcmp(in, out, XFER))
    {
        fail("tcp_async", "echoed data", got);
        OTCloseProvider(as.ep);
        return;
    }
    /* The echo server closes after our FIN: T_ORDREL. */
    err = OTSndOrderlyDisconnect(as.ep);
    if(err)
    {
        fail("tcp_async", "OTSndOrderlyDisconnect", err);
        OTCloseProvider(as.ep);
        return;
    }
    if(!wait_for(&as.ordrel, 10))
    {
        fail("tcp_async", "no T_ORDREL", 0);
        OTCloseProvider(as.ep);
        return;
    }
    err = OTRcvOrderlyDisconnect(as.ep);
    if(err)
    {
        fail("tcp_async", "OTRcvOrderlyDisconnect", err);
        OTCloseProvider(as.ep);
        return;
    }
    OTCloseProvider(as.ep);
    pass("tcp_async");
}

/* A connect to a port nobody listens on: OTConnect fails with kOTLookErr
   and OTLook says T_DISCON. */
static void test_tcp_refused(void)
{
    OSStatus err;
    EndpointRef ep = open_tcp(&err);
    InetAddress addr;
    TCall call;
    OTResult look;

    if(err)
    {
        fail("tcp_refused", "OTOpenEndpoint", err);
        return;
    }
    OTSetBlocking(ep);
    OTBind(ep, nil, nil);
    OTInitInetAddress(&addr, 1, 0x7F000001); /* tcpmux: closed */
    OTMemzero(&call, sizeof call);
    call.addr.buf = (UInt8 *)&addr;
    call.addr.len = sizeof addr;
    err = OTConnect(ep, &call, nil);
    if(err != kOTLookErr)
    {
        fail("tcp_refused", "OTConnect should answer kOTLookErr", err);
        OTCloseProvider(ep);
        return;
    }
    look = OTLook(ep);
    if(look != T_DISCONNECT)
    {
        fail("tcp_refused", "OTLook", look);
        OTCloseProvider(ep);
        return;
    }
    OTRcvDisconnect(ep, nil);
    if(OTGetEndpointState(ep) != T_IDLE)
    {
        fail("tcp_refused", "state after disconnect", OTGetEndpointState(ep));
        OTCloseProvider(ep);
        return;
    }
    OTUnbind(ep);
    OTCloseProvider(ep);
    pass("tcp_refused");
}

/* A listener and a client in the same process. */
static void test_tcp_listen(void)
{
    OSStatus err;
    EndpointRef lis, cli, acc;
    InetAddress want, got, peer;
    TBind req, ret;
    TCall call, conn;
    char buf[16];
    OTFlags flags;
    OTResult r;
    unsigned long until;

    lis = open_tcp(&err);
    if(err)
    {
        fail("tcp_listen", "open listener", err);
        return;
    }
    OTSetBlocking(lis);
    OTInitInetAddress(&want, 0, 0); /* any port, any address */
    req.addr.buf = (UInt8 *)&want;
    req.addr.len = sizeof want;
    req.qlen = 1;
    ret.addr.buf = (UInt8 *)&got;
    ret.addr.maxlen = sizeof got;
    err = OTBind(lis, &req, &ret);
    if(err || got.fPort == 0)
    {
        fail("tcp_listen", "OTBind qlen 1", err);
        OTCloseProvider(lis);
        return;
    }
    printf("# listening on port %u\n", got.fPort);

    cli = open_tcp(&err);
    OTSetNonBlocking(cli);
    OTSetAsynchronous(cli);
    OTBind(cli, nil, nil); /* async bind; give it a moment */
    until = TickCount() + 60;
    while(OTGetEndpointState(cli) != T_IDLE && TickCount() < until)
        ;
    OTInitInetAddress(&peer, got.fPort, 0x7F000001);
    OTMemzero(&call, sizeof call);
    call.addr.buf = (UInt8 *)&peer;
    call.addr.len = sizeof peer;
    err = OTConnect(cli, &call, nil);
    if(err != kOTNoDataErr && err != noErr)
    {
        fail("tcp_listen", "client OTConnect", err);
        OTCloseProvider(cli);
        OTCloseProvider(lis);
        return;
    }

    OTMemzero(&conn, sizeof conn);
    conn.addr.buf = (UInt8 *)&peer;
    conn.addr.maxlen = sizeof peer;
    err = OTListen(lis, &conn); /* blocks until the client arrives */
    if(err)
    {
        fail("tcp_listen", "OTListen", err);
        OTCloseProvider(cli);
        OTCloseProvider(lis);
        return;
    }
    acc = open_tcp(&err);
    OTSetBlocking(acc);
    OTBind(acc, nil, nil);
    err = OTAccept(lis, acc, &conn);
    if(err)
    {
        fail("tcp_listen", "OTAccept", err);
        OTCloseProvider(acc);
        OTCloseProvider(cli);
        OTCloseProvider(lis);
        return;
    }
    /* The client connected asynchronously: T_CONNECT, then OTRcvConnect
       before it can receive. */
    until = TickCount() + 300;
    while(OTLook(cli) != T_CONNECT && TickCount() < until)
        ;
    err = OTRcvConnect(cli, nil);
    if(err)
    {
        fail("tcp_listen", "client OTRcvConnect", err);
        OTCloseProvider(acc);
        OTCloseProvider(cli);
        OTCloseProvider(lis);
        return;
    }
    r = OTSnd(acc, "accepted", 8, 0);
    until = TickCount() + 300;
    do
        r = OTRcv(cli, buf, sizeof buf, &flags);
    while(r == kOTNoDataErr && TickCount() < until);
    if(r != 8 || memcmp(buf, "accepted", 8))
    {
        fail("tcp_listen", "data from accepted endpoint", r);
    }
    else
        pass("tcp_listen");
    OTSndDisconnect(acc, nil);
    OTCloseProvider(acc);
    OTCloseProvider(cli);
    OTUnbind(lis);
    OTCloseProvider(lis);
}

/* ---- UDP ------------------------------------------------------------- */

static void test_udp(void)
{
    OSStatus err;
    EndpointRef ep = OTOpenEndpoint(OTCreateConfiguration(kUDPName), 0, nil, &err);
    InetAddress to, from;
    TUnitData ud;
    char out[64], in[64];
    OTFlags flags;
    unsigned long until;

    if(err)
    {
        fail("udp", "OTOpenEndpoint udp", err);
        return;
    }
    OTSetBlocking(ep);
    err = OTBind(ep, nil, nil);
    if(err)
    {
        fail("udp", "OTBind", err);
        OTCloseProvider(ep);
        return;
    }
    strcpy(out, "datagram via open transport");
    OTInitInetAddress(&to, echo_port, echo_host);
    OTMemzero(&ud, sizeof ud);
    ud.addr.buf = (UInt8 *)&to;
    ud.addr.len = sizeof to;
    ud.udata.buf = (UInt8 *)out;
    ud.udata.len = strlen(out);
    err = OTSndUData(ep, &ud);
    if(err)
    {
        fail("udp", "OTSndUData", err);
        OTCloseProvider(ep);
        return;
    }
    OTSetNonBlocking(ep);
    OTMemzero(&ud, sizeof ud);
    ud.addr.buf = (UInt8 *)&from;
    ud.addr.maxlen = sizeof from;
    ud.udata.buf = (UInt8 *)in;
    ud.udata.maxlen = sizeof in;
    until = TickCount() + 300;
    do
        err = OTRcvUData(ep, &ud, &flags);
    while(err == kOTNoDataErr && TickCount() < until);
    if(err || ud.udata.len != strlen(out) || memcmp(in, out, ud.udata.len))
    {
        fail("udp", "OTRcvUData", err);
        OTCloseProvider(ep);
        return;
    }
    if(from.fPort != echo_port)
    {
        fail("udp", "reply port", from.fPort);
        OTCloseProvider(ep);
        return;
    }
    OTUnbind(ep);
    OTCloseProvider(ep);
    pass("udp");
}

/* ---- atomics and queues ---------------------------------------------- */

typedef struct QItem
{
    long value;
    struct QItem *next; /* OTEnqueue's link, at offset 4 */
    OTLink link;        /* for the OTList calls */
} QItem;

static Boolean match_value(const void *ref, OTLink *link)
{
    QItem *q = (QItem *)((char *)link - offsetof(QItem, link));
    return q->value == *(const long *)ref;
}

static void test_atomics(void)
{
    UInt8 bits = 0;
    UInt32 word = 5;
    void *ptr = nil;
    SInt16 w16 = 10;
    SInt8 w8 = 100;
    SInt32 w32 = 1;
    QItem items[3];
    void *head = nil;
    QItem *q;
    OTList list;
    long want;
    int i;

    /* SC compiles these inline: BSET, BCLR, BTST, CAS. */
    if(OTAtomicSetBit(&bits, 3) != 0 || bits != 0x08 || OTAtomicSetBit(&bits, 3) != 1)
    {
        fail("atomics", "OTAtomicSetBit", bits);
        return;
    }
    if(!OTAtomicTestBit(&bits, 3) || OTAtomicTestBit(&bits, 2))
    {
        fail("atomics", "OTAtomicTestBit", bits);
        return;
    }
    if(OTAtomicClearBit(&bits, 3) != 1 || bits != 0)
    {
        fail("atomics", "OTAtomicClearBit", bits);
        return;
    }
    if(OTCompareAndSwap32(4, 9, &word) || word != 5 || !OTCompareAndSwap32(5, 9, &word)
       || word != 9)
    {
        fail("atomics", "OTCompareAndSwap32", word);
        return;
    }
    if(!OTCompareAndSwapPtr(nil, &word, &ptr) || ptr != &word)
    {
        fail("atomics", "OTCompareAndSwapPtr", 0);
        return;
    }
    if(OTAtomicAdd32(41, &w32) != 42 || w32 != 42)
    {
        fail("atomics", "OTAtomicAdd32", w32);
        return;
    }
    /* These two are calls (OTClnt$CmnShared). */
    if(OTAtomicAdd16(-3, &w16) != 7 || w16 != 7)
    {
        fail("atomics", "OTAtomicAdd16", w16);
        return;
    }
    if(OTAtomicAdd8(27, &w8) != 127 || w8 != 127)
    {
        fail("atomics", "OTAtomicAdd8", w8);
        return;
    }

    /* OTEnqueue / OTDequeue with the link at offset 4: last in, first out. */
    for(i = 0; i < 3; i++)
    {
        items[i].value = i + 1;
        OTEnqueue(&head, &items[i], offsetof(QItem, next));
    }
    q = (QItem *)OTDequeue(&head, offsetof(QItem, next));
    if(q != &items[2] || OTDequeue(&head, offsetof(QItem, next)) != &items[1])
    {
        fail("atomics", "OTEnqueue/OTDequeue order", q ? q->value : 0);
        return;
    }

    /* OTFindLink / OTFindAndRemoveLink with a search procedure. */
    list.fHead = nil;
    for(i = 0; i < 3; i++)
        OTAddLast(&list, &items[i].link);
    want = 2;
    if(OTFindLink(&list, match_value, &want) != &items[1].link)
    {
        fail("atomics", "OTFindLink", 0);
        return;
    }
    if(OTFindAndRemoveLink(&list, match_value, &want) != &items[1].link
       || OTIsInList(&list, &items[1].link) || OTGetIndexedLink(&list, 1) != &items[2].link)
    {
        fail("atomics", "OTFindAndRemoveLink", 0);
        return;
    }
    want = 7;
    if(OTFindLink(&list, match_value, &want) != nil)
    {
        fail("atomics", "OTFindLink found a missing value", 0);
        return;
    }
    pass("atomics");
}

/* ---- deferred and system tasks ---------------------------------------- */

static volatile long dt_runs, dt_could_sync, st_runs;
static long dt_cookie;

static pascal void deferred_proc(void *arg)
{
    dt_runs += (long)arg;
    dt_could_sync = OTCanMakeSyncCall();
}

static pascal void system_proc(void *arg)
{
    st_runs += (long)arg;
}

static void test_tasks(void)
{
    long st;
    unsigned long until;

    dt_runs = st_runs = 0;
    dt_could_sync = -1;
    if(!OTCanMakeSyncCall())
    {
        fail("tasks", "OTCanMakeSyncCall at application time", 0);
        return;
    }
    dt_cookie = OTCreateDeferredTask(deferred_proc, (void *)5);
    if(!dt_cookie)
    {
        fail("tasks", "OTCreateDeferredTask", 0);
        return;
    }
    if(!OTScheduleDeferredTask(dt_cookie))
    {
        fail("tasks", "OTScheduleDeferredTask", 0);
        OTDestroyDeferredTask(dt_cookie);
        return;
    }
    if(!wait_for(&dt_runs, 5) || dt_runs != 5)
    {
        fail("tasks", "deferred task did not run once", dt_runs);
        OTDestroyDeferredTask(dt_cookie);
        return;
    }
    if(dt_could_sync != 0)
    {
        fail("tasks", "OTCanMakeSyncCall in a deferred task", dt_could_sync);
        OTDestroyDeferredTask(dt_cookie);
        return;
    }
    OTScheduleDeferredTask(dt_cookie);
    until = TickCount() + 300;
    while(dt_runs != 10 && TickCount() < until)
        ;
    if(dt_runs != 10)
    {
        fail("tasks", "deferred task scheduled again", dt_runs);
        OTDestroyDeferredTask(dt_cookie);
        return;
    }
    if(OTDestroyDeferredTask(dt_cookie))
    {
        fail("tasks", "OTDestroyDeferredTask", 0);
        return;
    }

    st = OTCreateSystemTask(system_proc, (void *)3);
    if(!st || !OTScheduleSystemTask(st))
    {
        fail("tasks", "OTCreateSystemTask/OTScheduleSystemTask", st);
        return;
    }
    if(OTScheduleSystemTask(st))
    {
        fail("tasks", "scheduling a scheduled system task", 0);
        OTDestroySystemTask(st);
        return;
    }
    /* System task time: SystemTask, as WaitNextEvent calls it. */
    until = TickCount() + 300;
    while(!st_runs && TickCount() < until)
        SystemTask();
    if(st_runs != 3)
    {
        fail("tasks", "system task did not run once", st_runs);
        OTDestroySystemTask(st);
        return;
    }
    OTScheduleSystemTask(st);
    if(!OTCancelSystemTask(st))
    {
        fail("tasks", "OTCancelSystemTask", 0);
        OTDestroySystemTask(st);
        return;
    }
    SystemTask();
    if(st_runs != 3)
    {
        fail("tasks", "a cancelled system task ran", st_runs);
        OTDestroySystemTask(st);
        return;
    }
    OTDestroySystemTask(st);
    pass("tasks");
}

/* ---- options ----------------------------------------------------------- */

typedef struct
{
    UInt32 len;
    OTXTILevel level;
    OTXTIName name;
    UInt32 status;
    UInt32 value;
} Opt4;

/* One 4-byte option, negotiated or asked for; its status and value. */
static OSStatus one_option(EndpointRef ep, OTFlags flags, OTXTILevel level, OTXTIName name,
                           UInt32 *value, UInt32 *status, OTFlags *result)
{
    Opt4 in, out;
    TOptMgmt req, ret;
    OSStatus err;

    in.len = kOTFourByteOptionSize;
    in.level = level;
    in.name = name;
    in.status = 0;
    in.value = *value;
    req.opt.buf = (UInt8 *)&in;
    req.opt.len = sizeof in;
    req.opt.maxlen = sizeof in;
    req.flags = flags;
    OTMemzero(&out, sizeof out);
    ret.opt.buf = (UInt8 *)&out;
    ret.opt.maxlen = sizeof out;
    ret.opt.len = 0;
    ret.flags = 0;
    err = OTOptionManagement(ep, &req, &ret);
    *value = out.value;
    *status = out.status;
    if(result)
        *result = ret.flags;
    return err;
}

static void test_options(void)
{
    OSStatus err;
    EndpointRef ep = open_tcp(&err);
    UInt32 v, st;
    OTFlags flags;
    UInt8 buf[64];
    TOption *opt;
    TOption *first, *second;
    Opt4 *o;

    if(err)
    {
        fail("options", "OTOpenEndpoint", err);
        return;
    }
    OTSetSynchronous(ep);
    OTSetBlocking(ep);
    /* Before there is a connection: kept, then applied. */
    v = T_NO;
    err = one_option(ep, T_NEGOTIATE, INET_TCP, TCP_NODELAY, &v, &st, &flags);
    if(err || st != T_SUCCESS || v != T_NO)
    {
        fail("options", "TCP_NODELAY T_NO before bind", err ? err : st);
        OTCloseProvider(ep);
        return;
    }
    v = 16384;
    err = one_option(ep, T_NEGOTIATE, XTI_GENERIC, XTI_SNDBUF, &v, &st, nil);
    if(err || st != T_SUCCESS)
    {
        fail("options", "XTI_SNDBUF", err ? err : st);
        OTCloseProvider(ep);
        return;
    }
    if(OTBind(ep, nil, nil) || connect_echo(ep))
    {
        fail("options", "bind/connect", 0);
        OTCloseProvider(ep);
        return;
    }
    v = 0;
    err = one_option(ep, T_CURRENT, INET_TCP, TCP_NODELAY, &v, &st, nil);
    if(err || st != T_SUCCESS || v != T_NO)
    {
        fail("options", "TCP_NODELAY current after connect", err ? err : (long)v);
        close_orderly(ep);
        return;
    }
    v = 0;
    err = one_option(ep, T_CURRENT, XTI_GENERIC, XTI_SNDBUF, &v, &st, nil);
    if(err || v < 16384)
    {
        fail("options", "XTI_SNDBUF current", err ? err : (long)v);
        close_orderly(ep);
        return;
    }
    v = T_YES;
    err = one_option(ep, T_NEGOTIATE, INET_TCP, TCP_NODELAY, &v, &st, nil);
    if(err || st != T_SUCCESS || v != T_YES)
    {
        fail("options", "TCP_NODELAY T_YES on a connection", err ? err : st);
        close_orderly(ep);
        return;
    }
    v = 1;
    err = one_option(ep, T_NEGOTIATE, 0x4242, 0x4242, &v, &st, &flags);
    if(err || st != T_NOTSUPPORT || flags != T_NOTSUPPORT)
    {
        fail("options", "an unknown option should be T_NOTSUPPORT", err ? err : st);
        close_orderly(ep);
        return;
    }
    v = 0;
    err = one_option(ep, T_DEFAULT, INET_IP, IP_TTL, &v, &st, nil);
    if(err || st != T_SUCCESS || v == 0)
    {
        fail("options", "IP_TTL default", err ? err : st);
        close_orderly(ep);
        return;
    }
    if(echo_sync(ep, "options") < 0)
    {
        close_orderly(ep);
        return;
    }
    close_orderly(ep);

    /* OTNextOption / OTFindOption over a buffer of two options. */
    OTMemzero(buf, sizeof buf);
    o = (Opt4 *)buf;
    o->len = kOTFourByteOptionSize;
    o->level = INET_IP;
    o->name = IP_TTL;
    o->value = 32;
    o = (Opt4 *)(buf + 20);
    o->len = kOTFourByteOptionSize;
    o->level = INET_TCP;
    o->name = TCP_NODELAY;
    o->value = T_YES;
    first = nil;
    if(OTNextOption(buf, 40, &first) || first != (TOption *)buf)
    {
        fail("options", "OTNextOption first", 0);
        return;
    }
    second = first;
    if(OTNextOption(buf, 40, &second) || second != (TOption *)(buf + 20))
    {
        fail("options", "OTNextOption second", 0);
        return;
    }
    if(OTNextOption(buf, 40, &second) || second != nil)
    {
        fail("options", "OTNextOption after the last", 0);
        return;
    }
    opt = OTFindOption(buf, 40, INET_TCP, TCP_NODELAY);
    if(opt != (TOption *)(buf + 20) || OTFindOption(buf, 40, INET_TCP, TCP_MAXSEG) != nil)
    {
        fail("options", "OTFindOption", 0);
        return;
    }
    pass("options");
}

/* ---- a datagram bigger than the buffer ----------------------------------- */

static void test_udp_more(void)
{
    OSStatus err;
    EndpointRef ep = OTOpenEndpoint(OTCreateConfiguration(kUDPName), 0, nil, &err);
    InetAddress to, from;
    TUnitData ud;
    static char out[200], in[200];
    char piece[64];
    OTFlags flags;
    unsigned long until;
    long got = 0, pieces = 0, mores = 0;
    UInt32 v, st;

    if(err)
    {
        fail("udp_more", "OTOpenEndpoint udp", err);
        return;
    }
    /* An option on a UDP endpoint before it is bound. */
    v = T_YES;
    if(one_option(ep, T_NEGOTIATE, INET_IP, IP_REUSEADDR, &v, &st, nil) || st != T_SUCCESS)
    {
        fail("udp_more", "IP_REUSEADDR", st);
        OTCloseProvider(ep);
        return;
    }
    OTSetBlocking(ep);
    if((err = OTBind(ep, nil, nil)) != noErr)
    {
        fail("udp_more", "OTBind", err);
        OTCloseProvider(ep);
        return;
    }
    fill_pattern(out, sizeof out, 5);
    OTInitInetAddress(&to, echo_port, echo_host);
    OTMemzero(&ud, sizeof ud);
    ud.addr.buf = (UInt8 *)&to;
    ud.addr.len = sizeof to;
    ud.udata.buf = (UInt8 *)out;
    ud.udata.len = sizeof out;
    if((err = OTSndUData(ep, &ud)) != noErr)
    {
        fail("udp_more", "OTSndUData", err);
        OTCloseProvider(ep);
        return;
    }
    OTSetNonBlocking(ep);
    until = TickCount() + 300;
    while(got < (long)sizeof out && TickCount() < until)
    {
        OTMemzero(&ud, sizeof ud);
        ud.addr.buf = (UInt8 *)&from;
        ud.addr.maxlen = sizeof from;
        ud.udata.buf = (UInt8 *)piece;
        ud.udata.maxlen = sizeof piece;
        flags = 0;
        err = OTRcvUData(ep, &ud, &flags);
        if(err == kOTNoDataErr)
            continue;
        if(err)
            break;
        memcpy(in + got, piece, ud.udata.len);
        got += ud.udata.len;
        pieces++;
        if(flags & T_MORE)
            mores++;
        else
            break;
    }
    if(err || got != sizeof out || memcmp(in, out, sizeof out))
    {
        fail("udp_more", "datagram in pieces", err ? err : got);
        OTCloseProvider(ep);
        return;
    }
    if(pieces != 4 || mores != 3)
    {
        fail("udp_more", "pieces / T_MORE flags", pieces * 10 + mores);
        OTCloseProvider(ep);
        return;
    }
    OTUnbind(ep);
    OTCloseProvider(ep);
    pass("udp_more");
}

/* ---- a blocking call cancelled from a Time Manager task ----------------- */

typedef struct
{
    TMTask tm;
    long a5;
    EndpointRef ep;
    long fired;
} CancelTask;

#pragma parameter __D0 GetA1
long GetA1(void) = 0x2009; /* MOVE.L A1,D0 */

static pascal void cancel_proc(void)
{
    CancelTask *t = (CancelTask *)GetA1();
    long old = SetA5(t->a5);
    OTCancelSynchronousCalls(t->ep, kOTCanceledErr);
    t->fired = 1;
    SetA5(old);
}

static void test_cancel(void)
{
    OSStatus err;
    EndpointRef ep = open_tcp(&err);
    CancelTask t;
    OTResult r;
    OTFlags flags;
    char c;
    unsigned long start;

    if(err)
    {
        fail("cancel", "OTOpenEndpoint", err);
        return;
    }
    OTSetSynchronous(ep);
    OTSetBlocking(ep);
    if(OTBind(ep, nil, nil) || connect_echo(ep))
    {
        fail("cancel", "bind/connect", 0);
        OTCloseProvider(ep);
        return;
    }
    OTMemzero(&t, sizeof t);
    t.tm.tmAddr = (TimerUPP)cancel_proc;
    t.a5 = SetCurrentA5();
    t.ep = ep;
    InsTime((QElemPtr)&t.tm);
    PrimeTime((QElemPtr)&t.tm, 300); /* 0.3 s */
    start = TickCount();
    r = OTRcv(ep, &c, 1, &flags); /* the echo server sends nothing */
    RmvTime((QElemPtr)&t.tm);
    if(r != kOTCanceledErr || !t.fired)
    {
        fail("cancel", "blocking OTRcv should return kOTCanceledErr", r);
        close_orderly(ep);
        return;
    }
    if(TickCount() - start > 5 * 60)
    {
        fail("cancel", "cancelled late (ticks)", TickCount() - start);
        close_orderly(ep);
        return;
    }
    /* The endpoint still works. */
    if(echo_sync(ep, "cancel") < 0)
    {
        close_orderly(ep);
        return;
    }
    close_orderly(ep);
    pass("cancel");
}

/* ---- DNSAddress: connect and resolve by name ----------------------------- */

static void test_dns_addr(void)
{
    OSStatus err;
    EndpointRef ep;
    DNSAddress dns;
    InetAddress got;
    TCall call;
    TBind req, ret;
    char name[64];
    unsigned long until;

    sprintf(name, "localhost:%u", echo_port);
    /* OTResolveAddress: DNSAddress in, InetAddress out. */
    ep = open_tcp(&err);
    if(err)
    {
        fail("dns_addr", "OTOpenEndpoint", err);
        return;
    }
    OTSetSynchronous(ep);
    OTSetBlocking(ep);
    OTMemzero(&req, sizeof req);
    req.addr.buf = (UInt8 *)&dns;
    req.addr.len = OTInitDNSAddress(&dns, name);
    OTMemzero(&ret, sizeof ret);
    ret.addr.buf = (UInt8 *)&got;
    ret.addr.maxlen = sizeof got;
    err = OTResolveAddress(ep, &req, &ret, 10000);
    if(err || got.fAddressType != AF_INET || got.fPort != echo_port || got.fHost == 0)
    {
        fail("dns_addr", "OTResolveAddress", err ? err : (long)got.fPort);
        OTCloseProvider(ep);
        return;
    }
    /* A blocking connect to a DNSAddress. */
    OTBind(ep, nil, nil);
    OTMemzero(&call, sizeof call);
    call.addr.buf = (UInt8 *)&dns;
    call.addr.len = OTInitDNSAddress(&dns, name);
    err = OTConnect(ep, &call, nil);
    if(err)
    {
        fail("dns_addr", "blocking OTConnect to localhost:port", err);
        OTCloseProvider(ep);
        return;
    }
    if(echo_sync(ep, "dns_addr") < 0)
    {
        close_orderly(ep);
        return;
    }
    close_orderly(ep);

    /* Asynchronous: the name looked up, then T_CONNECT. */
    memset((void *)&as, 0, sizeof as);
    err = OTAsyncOpenEndpoint(OTCreateConfiguration(kTCPName), 0, nil, tcp_notifier, nil);
    if(err || !wait_for(&as.opened, 10))
    {
        fail("dns_addr", "OTAsyncOpenEndpoint", err);
        return;
    }
    OTBind(as.ep, nil, nil);
    if(!wait_for(&as.bound, 10))
    {
        fail("dns_addr", "T_BINDCOMPLETE", 0);
        OTCloseProvider(as.ep);
        return;
    }
    OTMemzero(&call, sizeof call);
    call.addr.buf = (UInt8 *)&dns;
    call.addr.len = OTInitDNSAddress(&dns, name);
    err = OTConnect(as.ep, &call, nil);
    if(err != kOTNoDataErr)
    {
        fail("dns_addr", "async OTConnect should answer kOTNoDataErr", err);
        OTCloseProvider(as.ep);
        return;
    }
    until = TickCount() + 600;
    while(!as.connected && !as.disconnected && TickCount() < until)
        ;
    if(!as.connected || OTRcvConnect(as.ep, nil))
    {
        fail("dns_addr", "async connect to a name: no T_CONNECT", as.disconnected);
        OTCloseProvider(as.ep);
        return;
    }
    OTSndDisconnect(as.ep, nil);
    OTCloseProvider(as.ep);
    pass("dns_addr");
}

/* ---- odds and ends: ports, OTSync, OTRcvUDErr, OTIoctl ------------------- */

static void test_misc(void)
{
    OTPortRecord port, byname, byref;
    OSStatus err;
    EndpointRef ep;
    UInt16 other = 99;
    OTPortRef ref;
    TUDErr uderr;

    if(!OTGetIndexedPort(&port, 0) || OTGetIndexedPort(&byname, 1))
    {
        fail("misc", "OTGetIndexedPort", 0);
        return;
    }
    printf("# port 0: %s ref %08lx\n", port.fPortName, (long)port.fRef);
    if(OTGetDeviceTypeFromPortRef(port.fRef) != kOTEthernetDevice)
    {
        fail("misc", "port 0 is not Ethernet", OTGetDeviceTypeFromPortRef(port.fRef));
        return;
    }
    if(!OTFindPort(&byname, port.fPortName) || byname.fRef != port.fRef
       || !OTFindPortByRef(&byref, port.fRef) || strcmp(byref.fPortName, port.fPortName)
       || OTFindPort(&byname, "nosuchport"))
    {
        fail("misc", "OTFindPort / OTFindPortByRef", 0);
        return;
    }
    ref = OTCreatePortRef(kOTPCIBus, kOTSerialDevice, 3, 7);
    if(OTGetBusTypeFromPortRef(ref) != kOTPCIBus || OTGetDeviceTypeFromPortRef(ref) != kOTSerialDevice
       || OTGetSlotFromPortRef(ref, &other) != 3 || other != 7)
    {
        fail("misc", "OTCreatePortRef round trip", ref);
        return;
    }

    ep = OTOpenEndpoint(OTCreateConfiguration(kUDPName), 0, nil, &err);
    if(err)
    {
        fail("misc", "OTOpenEndpoint udp", err);
        return;
    }
    if(OTGetProviderPortRef(ep) != port.fRef)
    {
        fail("misc", "OTGetProviderPortRef", OTGetProviderPortRef(ep));
        OTCloseProvider(ep);
        return;
    }
    if(OTSync(ep) != T_UNBND)
    {
        fail("misc", "OTSync before bind", OTSync(ep));
        OTCloseProvider(ep);
        return;
    }
    OTBind(ep, nil, nil);
    if(OTSync(ep) != T_IDLE)
    {
        fail("misc", "OTSync after bind", OTSync(ep));
        OTCloseProvider(ep);
        return;
    }
    OTMemzero(&uderr, sizeof uderr);
    if(OTRcvUDErr(ep, &uderr) != kOTNoUDErrErr)
    {
        fail("misc", "OTRcvUDErr with no error", 0);
        OTCloseProvider(ep);
        return;
    }
    if(OTIoctl(ep, I_OTGetMiscellaneousEvents, nil) != noErr)
    {
        fail("misc", "OTIoctl I_OTGetMiscellaneousEvents", 0);
        OTCloseProvider(ep);
        return;
    }
    OTUnbind(ep);
    OTCloseProvider(ep);
    pass("misc");
}

/* ---- the rest of the Internet services ------------------------------------ */

static void test_inet_misc(void)
{
    OSStatus err;
    InetSvcRef svc = OTOpenInternetServices(kDefaultInternetServicesPath, 0, &err);
    InetDomainName name;
    InetMailExchange mx[4];
    InetSysInfo sys;
    UInt16 num = 4;
    static char buf[2048];
    void *argv[8];

    if(err)
    {
        fail("inet_misc", "OTOpenInternetServices", err);
        return;
    }
    err = OTInetAddressToName(svc, 0x7F000001, name);
    if(err || !name[0])
    {
        fail("inet_misc", "OTInetAddressToName 127.0.0.1", err);
        OTCloseProvider(svc);
        return;
    }
    printf("# 127.0.0.1 -> %s\n", name);
    /* These go to the host's name servers; whatever they answer, the calls
       come back with a result and leave the buffers sane. */
    err = OTInetMailExchange(svc, "no-such-host.invalid", &num, mx);
    printf("# MX no-such-host.invalid: %ld (%u)\n", (long)err, num);
    if(err == noErr && num > 4)
    {
        fail("inet_misc", "OTInetMailExchange count", num);
        OTCloseProvider(svc);
        return;
    }
    err = OTInetSysInfo(svc, "no-such-host.invalid", &sys);
    printf("# HINFO no-such-host.invalid: %ld\n", (long)err);
    if(err == noErr)
    {
        fail("inet_misc", "OTInetSysInfo answered for a name that isn't there", 0);
        OTCloseProvider(svc);
        return;
    }
    err = OTInetQuery(svc, "no-such-host.invalid", 1, 1, buf, sizeof buf, argv, 8, 0);
    printf("# query no-such-host.invalid A: %ld\n", (long)err);
    if(err == noErr)
    {
        fail("inet_misc", "OTInetQuery answered for a name that isn't there", 0);
        OTCloseProvider(svc);
        return;
    }
    OTCloseProvider(svc);
    pass("inet_misc");
}

/* ---- main ------------------------------------------------------------ */

typedef void (*TestFn)(void);
static const struct
{
    const char *name;
    TestFn fn;
    Boolean needs_echo;
} tests[] = {
    { "utils", test_utils, false },
    { "dns_sync", test_dns_sync, false },
    { "dns_async", test_dns_async, false },
    { "iface", test_iface, false },
    { "tcp_blocking", test_tcp_blocking, true },
    { "tcp_nonblocking", test_tcp_nonblocking, true },
    { "tcp_async", test_tcp_async, true },
    { "tcp_refused", test_tcp_refused, false },
    { "tcp_listen", test_tcp_listen, false },
    { "udp", test_udp, true },
    { "atomics", test_atomics, false },
    { "tasks", test_tasks, false },
    { "options", test_options, true },
    { "udp_more", test_udp_more, true },
    { "cancel", test_cancel, true },
    { "dns_addr", test_dns_addr, true },
    { "misc", test_misc, false },
    { "inet_misc", test_inet_misc, false },
};

static Boolean wanted(int argc, char **argv, const char *name)
{
    int i;
    if(argc < 2)
        return true;
    for(i = 1; i < argc; i++)
        if(!strcmp(argv[i], name))
            return true;
    return false;
}

int main(int argc, char **argv)
{
    int i;

    read_echo_config();
    if(echo_port)
        printf("# echo %08lx port %u\n", (long)echo_host, echo_port);
    if(wanted(argc, argv, "gestalt"))
        test_gestalt();
    if(test_init())
    {
        for(i = 0; i < sizeof tests / sizeof tests[0]; i++)
        {
            if(!wanted(argc, argv, tests[i].name))
                continue;
            if(tests[i].needs_echo && !echo_port)
            {
                fail(tests[i].name, "no echo server in Host:net_echo.txt", 0);
                continue;
            }
            tests[i].fn();
            fflush(stdout);
        }
        CloseOpenTransport();
    }
    printf("DONE %d\n", failures);
    return failures ? 1 : 0;
}
