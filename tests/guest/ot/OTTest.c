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
#include <OpenTransport.h>
#include <OpenTptInternet.h>
#include <stdio.h>
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
