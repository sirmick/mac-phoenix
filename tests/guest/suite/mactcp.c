/* mactcp.c — the MacTCP section: the .IPP driver's API beyond the network
 * section's synchronous echo, against the same echo service (net-bridge's
 * on the ROM machines, a host echo server on Executor; see net.c).
 * Asynchronous calls with completion routines, ASRs, TCPStatus,
 * TCPNoCopyRcv, a transfer bigger than the receive buffer, orderly close,
 * a passive open the section connects to itself, a refused and a
 * timed-out connect (interrupts run while a synchronous call waits),
 * UDP reads with completion and ASR, TCPGlobalInfo, and the resolver's
 * AddrToStr, AddrToName, HInfo and MXInfo.
 *
 * Completion routines and ASRs run at interrupt time with any A5, so they
 * keep what they see in the parameter block or the ASR's user data,
 * never in globals. */

#include "suite.h"
#include "tcpip.h"

#include <Devices.h>
#include <Events.h>
#include <Memory.h>
#include <OSUtils.h>
#include <stdio.h>
#include <string.h>

enum { kRcvArea = 8192, kBulk = 32768 };

static unsigned long echo_host;
static unsigned short echo_port;
static void (*miss)(const char *, const char *);

static void fail_err(const char *name, const char *what, OSErr err)
{
    char detail[64];

    sprintf(detail, "%s err=%d", what, err);
    report_fail(name, detail);
}

static void miss_err(const char *name, const char *what, OSErr err)
{
    char detail[64];

    sprintf(detail, "%s err=%d", what, err);
    miss(name, detail);
}

/* Wait for *flag, up to secs, staying responsive. */
static int wait_flag(volatile long *flag, int secs)
{
    unsigned long until = TickCount() + secs * 60L;

    while(!*flag && TickCount() < until)
        idle_ticks(1);
    return *flag != 0;
}

/* ---- parameter blocks with room for what the callbacks see */

typedef struct
{
    TCPiopb pb;
    volatile long done;
    volatile unsigned long tick;
} TCPCall;

typedef struct
{
    UDPiopb pb;
    volatile long done;
    volatile unsigned long tick;
} UDPCall;

/* ASR user data */
typedef struct
{
    volatile long events[8]; /* count by event code */
    volatile unsigned short reason;
} Notes;

static void tcp_completion(TCPiopb *pb)
{
    ((TCPCall *)pb)->tick = TickCount();
    ((TCPCall *)pb)->done = 1;
}

static void udp_completion(UDPiopb *pb)
{
    ((UDPCall *)pb)->tick = TickCount();
    ((UDPCall *)pb)->done = 1;
}

static pascal void tcp_asr(StreamPtr s, unsigned short event, Ptr user, unsigned short reason,
                           struct ICMPReport *icmp)
{
    Notes *n = (Notes *)user;

    (void)s;
    (void)icmp;
    if(event < 8)
        n->events[event]++;
    n->reason = reason;
}

static pascal void udp_asr(StreamPtr s, unsigned short event, Ptr user, struct ICMPReport *icmp)
{
    Notes *n = (Notes *)user;

    (void)s;
    (void)icmp;
    if(event < 8)
        n->events[event]++;
}

static TCPIOCompletionUPP tcp_done_upp;
static UDPIOCompletionUPP udp_done_upp;
static TCPNotifyUPP tcp_asr_upp;
static UDPNotifyUPP udp_asr_upp;

static void make_upps(void)
{
    if(tcp_done_upp)
        return;
    tcp_done_upp = NewTCPIOCompletionProc(tcp_completion);
    udp_done_upp = NewUDPIOCompletionProc(udp_completion);
    tcp_asr_upp = NewTCPNotifyProc(tcp_asr);
    udp_asr_upp = NewUDPNotifyProc(udp_asr);
}

static OSErr tcp_sync(TCPiopb *pb, short code, StreamPtr s)
{
    pb->ioCRefNum = ip_refnum();
    pb->csCode = code;
    pb->tcpStream = s;
    return PBControlSync((ParmBlkPtr)pb);
}

static void tcp_async(TCPCall *c, short code, StreamPtr s)
{
    c->done = 0;
    c->pb.ioCRefNum = ip_refnum();
    c->pb.csCode = code;
    c->pb.tcpStream = s;
    c->pb.ioCompletion = tcp_done_upp;
    PBControlAsync((ParmBlkPtr)&c->pb);
}

static OSErr udp_sync(UDPiopb *pb, short code, StreamPtr s)
{
    pb->ioCRefNum = ip_refnum();
    pb->csCode = code;
    pb->udpStream = s;
    return PBControlSync((ParmBlkPtr)pb);
}

/* A TCP stream with an ASR, its receive area from the heap. */
static OSErr tcp_create(StreamPtr *s, Notes *notes, unsigned long area)
{
    TCPiopb pb;
    OSErr err;

    memset(&pb, 0, sizeof pb);
    pb.csParam.create.rcvBuff = NewPtr(area);
    pb.csParam.create.rcvBuffLen = area;
    pb.csParam.create.notifyProc = notes ? tcp_asr_upp : NULL;
    pb.csParam.create.userDataPtr = (Ptr)notes;
    if((err = tcp_sync(&pb, TCPCreate, 0)) != noErr)
        DisposePtr(pb.csParam.create.rcvBuff);
    else
        *s = pb.tcpStream;
    return err;
}

static void tcp_release(StreamPtr s)
{
    TCPiopb pb;

    memset(&pb, 0, sizeof pb);
    tcp_sync(&pb, TCPAbort, s);
    memset(&pb, 0, sizeof pb);
    if(tcp_sync(&pb, TCPRelease, s) == noErr)
        DisposePtr(pb.csParam.create.rcvBuff);
}

static OSErr tcp_open_sync(StreamPtr s, unsigned long host, unsigned short port, int timeout)
{
    TCPiopb pb;

    memset(&pb, 0, sizeof pb);
    pb.csParam.open.ulpTimeoutValue = timeout;
    pb.csParam.open.ulpTimeoutAction = 1;
    pb.csParam.open.validityFlags = 0xC0;
    pb.csParam.open.commandTimeoutValue = timeout;
    pb.csParam.open.remoteHost = host;
    pb.csParam.open.remotePort = port;
    return tcp_sync(&pb, TCPActiveOpen, s);
}

static OSErr tcp_send(StreamPtr s, const void *data, unsigned short length)
{
    TCPiopb pb;
    wdsEntry wds[2];

    wds[0].length = length;
    wds[0].ptr = (Ptr)data;
    wds[1].length = 0;
    wds[1].ptr = NULL;
    memset(&pb, 0, sizeof pb);
    pb.csParam.send.pushFlag = true;
    pb.csParam.send.wdsPtr = (Ptr)wds;
    return tcp_sync(&pb, TCPSend, s);
}

/* Up to max bytes, waiting up to secs for each piece; the count. */
static long tcp_recv(StreamPtr s, char *buf, long max, int secs)
{
    TCPiopb pb;
    long got = 0, left;

    while(got < max)
    {
        left = max - got;
        memset(&pb, 0, sizeof pb);
        pb.csParam.receive.commandTimeoutValue = secs;
        pb.csParam.receive.rcvBuff = buf + got;
        pb.csParam.receive.rcvBuffLen = left > 32767 ? 32767 : left;
        if(tcp_sync(&pb, TCPRcv, s) != noErr)
            break;
        got += pb.csParam.receive.rcvBuffLen;
    }
    return got;
}

/* ---- the cases */

static void test_getaddr(unsigned long *ours)
{
    GetAddrParamBlock pb;
    OSErr err;
    char name[48];

    memset(&pb, 0, sizeof pb);
    pb.ioCRefNum = ip_refnum();
    pb.csCode = ipctlGetAddr;
    if((err = PBControlSync((ParmBlkPtr)&pb)) != noErr || pb.ourAddress == 0)
    {
        fail_err("mactcp_getaddr", "ipctlGetAddr", err);
        *ours = 0;
        return;
    }
    *ours = pb.ourAddress;
    sprintf(name, "mactcp_getaddr_%lu.%lu.%lu.%lu", *ours >> 24, (*ours >> 16) & 255,
            (*ours >> 8) & 255, *ours & 255);
    report_pass(name);
}

/* An asynchronous connection with an ASR: open, status, data arrival,
   no-copy receive, a receive posted before the data, orderly close. */
static void test_async_connection(void)
{
    static TCPCall open, rcv;
    static Notes notes;
    static char buf[64];
    TCPiopb pb;
    rdsEntry rds[4];
    StreamPtr s;
    OSErr err;
    long n;

    memset(&notes, 0, sizeof notes);
    if((err = tcp_create(&s, &notes, kRcvArea)) != noErr)
    {
        fail_err("mactcp_async_open", "TCPCreate", err);
        return;
    }
    memset(&open, 0, sizeof open);
    open.pb.csParam.open.ulpTimeoutValue = 10;
    open.pb.csParam.open.ulpTimeoutAction = 1;
    open.pb.csParam.open.validityFlags = 0xC0;
    open.pb.csParam.open.remoteHost = echo_host;
    open.pb.csParam.open.remotePort = echo_port;
    tcp_async(&open, TCPActiveOpen, s);
    if(!wait_flag(&open.done, 15) || open.pb.ioResult != noErr)
    {
        miss_err("mactcp_async_open", open.done ? "completion" : "no completion",
                 open.done ? open.pb.ioResult : 0);
        tcp_release(s);
        return;
    }
    report_pass("mactcp_async_open");

    memset(&pb, 0, sizeof pb);
    err = tcp_sync(&pb, TCPStatus, s);
    if(err || pb.csParam.status.connectionState != 8 || pb.csParam.status.remotePort != echo_port)
        fail_err("mactcp_status", "TCPStatus (state 8, the echo port)",
                 err ? err : pb.csParam.status.connectionState);
    else
        report_pass("mactcp_status");

    /* Nothing asked for: the data-arrival ASR. */
    if((err = tcp_send(s, "arrival", 7)) != noErr)
        fail_err("mactcp_asr_data", "TCPSend", err);
    else if(!wait_flag(&notes.events[TCPDataArrival], 5))
        report_fail("mactcp_asr_data", "no TCPDataArrival");
    else
        report_pass("mactcp_asr_data");

    /* TCPNoCopyRcv lends pieces of the receive area; give them back. */
    memset(&pb, 0, sizeof pb);
    pb.csParam.receive.commandTimeoutValue = 5;
    pb.csParam.receive.rdsPtr = (Ptr)rds;
    pb.csParam.receive.rdsLength = 3;
    err = tcp_sync(&pb, TCPNoCopyRcv, s);
    n = err ? 0 : rds[0].length;
    if(err || n < 1 || n > 7 || memcmp(rds[0].ptr, "arrival", n))
        fail_err("mactcp_nocopy", "TCPNoCopyRcv", err ? err : (OSErr)n);
    else
    {
        memset(&pb, 0, sizeof pb);
        pb.csParam.receive.rdsPtr = (Ptr)rds;
        err = tcp_sync(&pb, TCPRcvBfrReturn, s);
        if(err)
            fail_err("mactcp_nocopy", "TCPRcvBfrReturn", err);
        else
        {
            if(n < 7)                           /* the rest came separately */
                tcp_recv(s, buf, 7 - n, 3);
            report_pass("mactcp_nocopy");
        }
    }

    /* A receive waiting before the data is sent. */
    memset(&rcv, 0, sizeof rcv);
    rcv.pb.csParam.receive.commandTimeoutValue = 10;
    rcv.pb.csParam.receive.rcvBuff = buf;
    rcv.pb.csParam.receive.rcvBuffLen = 4;
    tcp_async(&rcv, TCPRcv, s);
    if(rcv.pb.ioResult != inProgress && rcv.pb.ioResult != noErr)
        fail_err("mactcp_async_rcv", "TCPRcv", rcv.pb.ioResult);
    else if((err = tcp_send(s, "wait", 4)) != noErr)
        fail_err("mactcp_async_rcv", "TCPSend", err);
    else if(!wait_flag(&rcv.done, 5) || rcv.pb.ioResult != noErr)
        fail_err("mactcp_async_rcv", rcv.done ? "completion" : "no completion",
                 rcv.done ? rcv.pb.ioResult : 0);
    else if(rcv.pb.csParam.receive.rcvBuffLen < 1 || memcmp(buf, "wait", rcv.pb.csParam.receive.rcvBuffLen))
        report_fail("mactcp_async_rcv", "wrong data");
    else
    {
        n = rcv.pb.csParam.receive.rcvBuffLen;
        if(n < 4)
            tcp_recv(s, buf, 4 - n, 3);
        report_pass("mactcp_async_rcv");
    }

    /* Our FIN: the echo server closes too, TCPClosing reaches the ASR, and
       with both sides closed a receive says connectionDoesntExist. */
    memset(&pb, 0, sizeof pb);
    pb.csParam.close.ulpTimeoutValue = 10;
    pb.csParam.close.ulpTimeoutAction = 1;
    pb.csParam.close.validityFlags = 0xC0;
    if((err = tcp_sync(&pb, TCPClose, s)) != noErr)
        fail_err("mactcp_close", "TCPClose", err);
    else if(!wait_flag(&notes.events[TCPClosing], 5))
        report_fail("mactcp_close", "no TCPClosing");
    else
    {
        memset(&pb, 0, sizeof pb);
        pb.csParam.receive.commandTimeoutValue = 3;
        pb.csParam.receive.rcvBuff = buf;
        pb.csParam.receive.rcvBuffLen = sizeof buf;
        err = tcp_sync(&pb, TCPRcv, s);
        if(err != connectionDoesntExist)
            fail_err("mactcp_close", "TCPRcv with both sides closed", err);
        else
            report_pass("mactcp_close");
    }
    tcp_release(s);
}

/* More than the receive area holds, both ways. */
static void test_bulk(void)
{
    char *out, *in;                             /* 64 KB: too much for near data */
    StreamPtr s;
    OSErr err;
    long i, got, sent;
    char detail[48];

    out = NewPtr(kBulk);
    in = NewPtr(kBulk);
    if(!out || !in)
    {
        report_fail("mactcp_bulk", "no memory");
        if(out)
            DisposePtr(out);
        if(in)
            DisposePtr(in);
        return;
    }
    for(i = 0; i < kBulk; i++)
        out[i] = (char)(i * 13 + 7);
    if((err = tcp_create(&s, NULL, 4096)) != noErr)
    {
        fail_err("mactcp_bulk", "TCPCreate", err);
        DisposePtr(out);
        DisposePtr(in);
        return;
    }
    if((err = tcp_open_sync(s, echo_host, echo_port, 10)) != noErr)
    {
        miss_err("mactcp_bulk", "TCPActiveOpen", err);
        tcp_release(s);
        DisposePtr(out);
        DisposePtr(in);
        return;
    }
    /* In pieces the echo can return while we write. */
    got = 0;
    for(sent = 0; sent < kBulk; sent += 4096)
    {
        if((err = tcp_send(s, out + sent, 4096)) != noErr)
            break;
        got += tcp_recv(s, in + got, sent + 4096 - got, 5);
    }
    if(err)
        fail_err("mactcp_bulk", "TCPSend", err);
    else if(got != kBulk || memcmp(in, out, kBulk))
    {
        sprintf(detail, "got %ld of %d intact", got, kBulk);
        report_fail("mactcp_bulk", detail);
    }
    else
        report_pass("mactcp_bulk_32768");
    tcp_release(s);
    DisposePtr(out);
    DisposePtr(in);
}

/* A passive open the section connects to itself. */
static void test_passive(unsigned long ours)
{
    static TCPCall lis;
    static char buf[16];
    StreamPtr l, c;
    OSErr err;
    unsigned short port;

    if(!ours)
    {
        report_skip("mactcp_passive", "no address");
        return;
    }
    if((err = tcp_create(&l, NULL, kRcvArea)) != noErr || (err = tcp_create(&c, NULL, kRcvArea)) != noErr)
    {
        fail_err("mactcp_passive", "TCPCreate", err);
        return;
    }
    memset(&lis, 0, sizeof lis);
    lis.pb.csParam.open.commandTimeoutValue = 10;
    lis.pb.csParam.open.ulpTimeoutValue = 10;
    lis.pb.csParam.open.ulpTimeoutAction = 1;
    lis.pb.csParam.open.validityFlags = 0xC0;
    lis.pb.csParam.open.localPort = 0;          /* MacTCP picks one */
    tcp_async(&lis, TCPPassiveOpen, l);
    port = lis.pb.csParam.open.localPort;
    if(!port)
    {
        miss_err("mactcp_passive", "no local port", lis.pb.ioResult);
        tcp_release(c);
        tcp_release(l);
        return;
    }
    if((err = tcp_open_sync(c, ours, port, 10)) != noErr)
        miss_err("mactcp_passive", "connect to ourselves", err);
    else if(!wait_flag(&lis.done, 10) || lis.pb.ioResult != noErr)
        fail_err("mactcp_passive", "TCPPassiveOpen completion", lis.done ? lis.pb.ioResult : 0);
    else if((err = tcp_send(c, "loop", 4)) != noErr)
        fail_err("mactcp_passive", "TCPSend", err);
    else if(tcp_recv(l, buf, 4, 5) != 4 || memcmp(buf, "loop", 4))
        report_fail("mactcp_passive", "data on the accepted stream");
    else
        report_pass("mactcp_passive");
    tcp_release(c);
    tcp_release(l);
}

/* A port nobody listens on: the open fails. */
static void test_refused(void)
{
    StreamPtr s;
    OSErr err;
    unsigned long start = TickCount();

    if((err = tcp_create(&s, NULL, kRcvArea)) != noErr)
    {
        fail_err("mactcp_refused", "TCPCreate", err);
        return;
    }
    err = tcp_open_sync(s, echo_host, 1, 10);
    if(err == noErr)
        report_fail("mactcp_refused", "connected to port 1");
    else if(TickCount() - start > 12 * 60)
        fail_err("mactcp_refused", "slow", err);
    else
        report_pass("mactcp_refused");
    tcp_release(s);
}

/* A host that never answers (TEST-NET-1): the open gives up after its
   ULP timeout. Meanwhile an asynchronous UDP read with a 1-second timeout
   and nothing to read times out: its completion runs in the middle of the
   synchronous call, as interrupts did on a Mac. */
static void test_timeout(void)
{
    static UDPCall rd;
    UDPiopb pb;
    StreamPtr s, u = 0;
    OSErr err;
    unsigned long start, end;
    char detail[64];

    memset(&pb, 0, sizeof pb);
    pb.csParam.create.rcvBuff = NewPtr(4096);
    pb.csParam.create.rcvBuffLen = 4096;
    if(udp_sync(&pb, UDPCreate, 0) == noErr)
        u = pb.udpStream;
    if((err = tcp_create(&s, NULL, kRcvArea)) != noErr)
    {
        fail_err("mactcp_timeout", "TCPCreate", err);
        return;
    }
    start = TickCount();
    if(u)
    {
        memset(&rd, 0, sizeof rd);
        rd.pb.ioCRefNum = ip_refnum();
        rd.pb.csCode = UDPRead;
        rd.pb.udpStream = u;
        rd.pb.ioCompletion = udp_done_upp;
        rd.pb.csParam.receive.timeOut = 1;
        PBControlAsync((ParmBlkPtr)&rd.pb);
    }
    err = tcp_open_sync(s, 192UL << 24 | 2 << 8 | 1, 80, 3); /* 192.0.2.1 */
    end = TickCount();
    sprintf(detail, "err=%d after %lu ticks", err, end - start);
    if(err == noErr)
        report_fail("mactcp_timeout", "connected to TEST-NET");
    else if(end - start > 10 * 60)
        report_fail("mactcp_timeout", detail);
    else
        report_pass("mactcp_timeout");
    if(u)
    {
        wait_flag(&rd.done, 3);
        if(!rd.done)
            report_fail("mactcp_interrupts_in_sync_call", "the read never timed out");
        else if(rd.pb.ioResult != commandTimeout)
            fail_err("mactcp_interrupts_in_sync_call", "the read", rd.pb.ioResult);
        else if(end - start < 90 || rd.tick + 30 >= end)
        {
            sprintf(detail, "read done at +%lu, call ended at +%lu", rd.tick - start, end - start);
            report_fail("mactcp_interrupts_in_sync_call", detail);
        }
        else
            report_pass("mactcp_interrupts_in_sync_call");
        memset(&pb, 0, sizeof pb);
        if(udp_sync(&pb, UDPRelease, u) == noErr)
            DisposePtr(pb.csParam.create.rcvBuff);
    }
    tcp_release(s);
}

/* UDP: the data-arrival ASR, an asynchronous read, the MTU. */
static void test_udp(void)
{
    static UDPCall rd;
    static Notes notes;
    UDPiopb pb;
    wdsEntry wds[2];
    StreamPtr u;
    OSErr err;

    memset(&notes, 0, sizeof notes);
    memset(&pb, 0, sizeof pb);
    pb.csParam.create.rcvBuff = NewPtr(4096);
    pb.csParam.create.rcvBuffLen = 4096;
    pb.csParam.create.notifyProc = udp_asr_upp;
    pb.csParam.create.userDataPtr = (Ptr)&notes;
    if((err = udp_sync(&pb, UDPCreate, 0)) != noErr)
    {
        fail_err("mactcp_udp_asr", "UDPCreate", err);
        return;
    }
    u = pb.udpStream;

    memset(&pb, 0, sizeof pb);
    err = udp_sync(&pb, UDPMaxMTUSize, u);
    if(err || pb.csParam.mtu.mtuSize < 512)
        fail_err("mactcp_udp_mtu", "UDPMaxMTUSize", err ? err : (OSErr)pb.csParam.mtu.mtuSize);
    else
        report_pass("mactcp_udp_mtu");

    wds[0].length = 5;
    wds[0].ptr = (Ptr) "notes";
    wds[1].length = 0;
    memset(&pb, 0, sizeof pb);
    pb.csParam.send.remoteHost = echo_host;
    pb.csParam.send.remotePort = echo_port;
    pb.csParam.send.wdsPtr = (Ptr)wds;
    if((err = udp_sync(&pb, UDPWrite, u)) != noErr)
        fail_err("mactcp_udp_asr", "UDPWrite", err);
    else if(!wait_flag(&notes.events[UDPDataArrival], 5))
        miss("mactcp_udp_asr", "no UDPDataArrival");
    else
        report_pass("mactcp_udp_asr");

    /* The datagram the ASR announced, by an asynchronous read. */
    memset(&rd, 0, sizeof rd);
    rd.pb.ioCRefNum = ip_refnum();
    rd.pb.csCode = UDPRead;
    rd.pb.udpStream = u;
    rd.pb.ioCompletion = udp_done_upp;
    rd.pb.csParam.receive.timeOut = 5;
    PBControlAsync((ParmBlkPtr)&rd.pb);
    if(!wait_flag(&rd.done, 6) || rd.pb.ioResult != noErr)
        miss_err("mactcp_udp_async_read", rd.done ? "completion" : "no completion",
                 rd.done ? rd.pb.ioResult : 0);
    else if(rd.pb.csParam.receive.rcvBuffLen != 5 || memcmp(rd.pb.csParam.receive.rcvBuff, "notes", 5)
            || rd.pb.csParam.receive.remotePort != echo_port)
        report_fail("mactcp_udp_async_read", "wrong datagram");
    else
    {
        memset(&pb, 0, sizeof pb);
        pb.csParam.receive.rcvBuff = rd.pb.csParam.receive.rcvBuff;
        if((err = udp_sync(&pb, UDPBfrReturn, u)) != noErr)
            fail_err("mactcp_udp_async_read", "UDPBfrReturn", err);
        else
            report_pass("mactcp_udp_async_read");
    }
    memset(&pb, 0, sizeof pb);
    if(udp_sync(&pb, UDPRelease, u) == noErr)
        DisposePtr(pb.csParam.create.rcvBuff);
}

static void test_global_info(void)
{
    TCPiopb pb;
    OSErr err;
    TCPParam *param;
    TCPStats *st;
    char detail[80];

    memset(&pb, 0, sizeof pb);
    err = tcp_sync(&pb, TCPGlobalInfo, 0);
    if(err)
    {
        miss_err("mactcp_global_info", "TCPGlobalInfo", err);
        return;
    }
    param = pb.csParam.globalInfo.tcpParamPtr;
    st = pb.csParam.globalInfo.tcpStatsPtr;
    if(!param && !st)                           /* OT's MacTCP: noErr, no records */
    {
        miss("mactcp_global_info", "no records");
        return;
    }
    if(!param || !st || pb.csParam.globalInfo.maxTCPConnections == 0 || param->tcpMaxSegSize == 0)
    {
        sprintf(detail, "param %08lx stats %08lx max %u mss %lu", (long)param, (long)st,
                pb.csParam.globalInfo.maxTCPConnections, param ? param->tcpMaxSegSize : 0);
        report_fail("mactcp_global_info", detail);
    }
    else if(st->tcpConnOpened < 1)
        report_fail("mactcp_global_info", "no connection counted");
    else
        report_pass("mactcp_global_info");
}

static void test_resolver(void)
{
    char out[256];
    OSErr err;

    if((err = dns_addr_to_str(0xC0A80102, out)) != noErr || strcmp(out, "192.168.1.2"))
        fail_err("dnr_addr_to_str", "AddrToStr", err);
    else
        report_pass("dnr_addr_to_str");

    if((err = dns_addr_to_name(0x7F000001, out, 10)) != noErr || !out[0])
        miss_err("dnr_addr_to_name", "AddrToName 127.0.0.1", err);
    else
        report_pass("dnr_addr_to_name");

    /* Records of a name that cannot exist: an error, back in time. */
    err = dns_hinfo("no-such-host.invalid", out, 15);
    if(err == noErr)
        report_fail("dnr_hinfo", "answered for no-such-host.invalid");
    else if(err == commandTimeout)
        miss("dnr_hinfo", "no answer");
    else
        report_pass("dnr_hinfo");
    err = dns_mx("no-such-host.invalid", out, 15);
    if(err == noErr)
        report_fail("dnr_mxinfo", "answered for no-such-host.invalid");
    else if(err == commandTimeout)
        miss("dnr_mxinfo", "no answer");
    else
        report_pass("dnr_mxinfo");
}

void test_mactcp(void)
{
    unsigned long ours;
    int named;
    OSErr err;

    if((err = ip_open()) != noErr)
    {
        fail_err("mactcp_driver", "open .IPP", err);
        return;
    }
    make_upps();
    echo_service(&echo_host, &echo_port, &named);
    miss = named ? report_fail : report_skip;

    test_getaddr(&ours);
    test_async_connection();
    test_bulk();
    test_passive(ours);
    test_refused();
    test_timeout();
    test_udp();
    test_global_info();
    test_resolver();
}
