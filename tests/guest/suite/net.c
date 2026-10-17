/* net.c — MacTCP, as MacPerl's GUSI used it: the resolver (the 'dnrp'
 * code resource MacTCP or Open Transport installs), UDP and TCP through
 * the .IPP driver, against an echo service. On the ROM machines that is
 * net-bridge's (10.0.2.1:7); on Executor, whose MacTCP is on host sockets,
 * a host echo server the harness names in Host:net_echo.txt ("address
 * port"). A named one must answer, so its silence fails rather than skips. */

#include "suite.h"

#include <Devices.h>
#include <Errors.h>
#include <Files.h>
#include <Folders.h>
#include <MacTCP.h>
#include <Memory.h>
#include <MixedMode.h>
#include <Resources.h>
#include <stdio.h>
#include <string.h>

/* ---- the resolver (what Apple's dnr.c glue does) */

enum { kOpenResolver = 1, kCloseResolver = 2, kStrToAddr = 3 };

#if PRAGMA_ALIGN_SUPPORTED
#pragma options align=mac68k
#endif
typedef struct
{
    long rtnCode;
    char cname[255];
    unsigned long addr[4];
} HostInfo;
#if PRAGMA_ALIGN_SUPPORTED
#pragma options align=reset
#endif

static Handle dnr_code;

#if GENERATINGCFM
enum
{
    kOpenProcInfo = kCStackBased | RESULT_SIZE(kTwoByteCode)
        | STACK_ROUTINE_PARAMETER(1, kFourByteCode) | STACK_ROUTINE_PARAMETER(2, kFourByteCode),
    kCloseProcInfo = kCStackBased | RESULT_SIZE(kTwoByteCode)
        | STACK_ROUTINE_PARAMETER(1, kFourByteCode),
    kStrToAddrProcInfo = kCStackBased | RESULT_SIZE(kTwoByteCode)
        | STACK_ROUTINE_PARAMETER(1, kFourByteCode) | STACK_ROUTINE_PARAMETER(2, kFourByteCode)
        | STACK_ROUTINE_PARAMETER(3, kFourByteCode) | STACK_ROUTINE_PARAMETER(4, kFourByteCode)
        | STACK_ROUTINE_PARAMETER(5, kFourByteCode),
    kResultProcInfo = kPascalStackBased
        | STACK_ROUTINE_PARAMETER(1, kFourByteCode) | STACK_ROUTINE_PARAMETER(2, kFourByteCode)
};
#else
typedef OSErr (*OpenFn)(long selector, char *file);
typedef OSErr (*CloseFn)(long selector);
typedef OSErr (*StrToAddrFn)(long selector, char *name, HostInfo *info, ProcPtr done, Ptr user);
#endif

/* The DNR calls this when a lookup it couldn't answer from its cache ends. */
static pascal void lookup_done(HostInfo *info, Ptr user)
{
    (void)info;
    *(volatile short *)user = 1;
}

/* A file in (vRefNum, dirID) of type 'cdev' (MacTCP) or named MacTCP DNR
 * (Open Transport) that has a 'dnrp' resource: open, or -1. */
static short open_dnr_file(short vRefNum, long dirID)
{
    HParamBlockRec pb;
    Str255 name;
    short ref;

    memset(&pb, 0, sizeof pb);
    pb.fileParam.ioNamePtr = name;
    pb.fileParam.ioVRefNum = vRefNum;
    for(pb.fileParam.ioFDirIndex = 1;; pb.fileParam.ioFDirIndex++)
    {
        pb.fileParam.ioDirID = dirID;
        if(PBHGetFInfoSync(&pb) != noErr)
            return -1;
        if(pb.fileParam.ioFlFndrInfo.fdType != 'cdev'
           && !(name[0] == 10 && memcmp(name + 1, "MacTCP DNR", 10) == 0))
            continue;
        ref = HOpenResFile(vRefNum, dirID, name, fsRdPerm);
        if(ref == -1)
            continue;
        if(Get1IndResource('dnrp', 1))
            return ref;
        CloseResFile(ref);
    }
}

static OSErr dnr_open(void)
{
    short vRefNum, ref = -1;
    long dirID;
    OSErr err;

    if(FindFolder(kOnSystemDisk, kControlPanelFolderType, kDontCreateFolder, &vRefNum, &dirID) == noErr)
        ref = open_dnr_file(vRefNum, dirID);
    if(ref == -1 && FindFolder(kOnSystemDisk, kSystemFolderType, kDontCreateFolder, &vRefNum, &dirID) == noErr)
        ref = open_dnr_file(vRefNum, dirID);
    dnr_code = GetIndResource('dnrp', 1);      /* or in the System file */
    if(!dnr_code)
        return ResError() ? ResError() : resNotFound;
    DetachResource(dnr_code);
    if(ref != -1)
        CloseResFile(ref);
    MoveHHi(dnr_code);
    HLock(dnr_code);
#if GENERATINGCFM
    err = CallUniversalProc((UniversalProcPtr)*dnr_code, kOpenProcInfo, (long)kOpenResolver, (char *)NULL);
#else
    err = ((OpenFn)*dnr_code)(kOpenResolver, NULL);
#endif
    if(err != noErr)
    {
        DisposeHandle(dnr_code);
        dnr_code = NULL;
    }
    return err;
}

static void dnr_close(void)
{
    if(!dnr_code)
        return;
#if GENERATINGCFM
    CallUniversalProc((UniversalProcPtr)*dnr_code, kCloseProcInfo, (long)kCloseResolver);
#else
    ((CloseFn)*dnr_code)(kCloseResolver);
#endif
    DisposeHandle(dnr_code);
    dnr_code = NULL;
}

/* name -> address (network order), or an error. */
static OSErr resolve(char *name, unsigned long *addr)
{
    static HostInfo info;
    volatile short done = 0;
    unsigned long until;
    OSErr err;
#if GENERATINGCFM
    static UniversalProcPtr done_upp;

    if(!done_upp)
        done_upp = NewRoutineDescriptor((ProcPtr)lookup_done, kResultProcInfo, GetCurrentArchitecture());
    err = CallUniversalProc((UniversalProcPtr)*dnr_code, kStrToAddrProcInfo, (long)kStrToAddr, name,
                            &info, done_upp, (Ptr)&done);
#else
    err = ((StrToAddrFn)*dnr_code)(kStrToAddr, name, &info, (ProcPtr)lookup_done, (Ptr)&done);
#endif
    if(err == cacheFault)                       /* asked a server: wait for its answer */
    {
        for(until = TickCount() + 600; !done && TickCount() < until;)
            idle_ticks(1);
        err = done ? (OSErr)info.rtnCode : commandTimeout;
    }
    if(err == noErr)
        *addr = info.addr[0];
    return err;
}

/* ---- UDP and TCP */

static short ipp;

static OSErr udp_echo(unsigned long host, unsigned short port, char *detail)
{
    UDPiopb pb;
    wdsEntry wds[2];
    StreamPtr stream;
    Ptr buf = NewPtr(4096);
    OSErr err;

    memset(&pb, 0, sizeof pb);
    pb.ioCRefNum = ipp;
    pb.csCode = UDPCreate;
    pb.csParam.create.rcvBuff = buf;
    pb.csParam.create.rcvBuffLen = 4096;
    if((err = PBControlSync((ParmBlkPtr)&pb)) != noErr)
    {
        sprintf(detail, "UDPCreate err=%d", err);
        DisposePtr(buf);
        return err;
    }
    report_pass("udp_socket");
    stream = pb.udpStream;

    wds[0].length = 4;
    wds[0].ptr = "ping";
    wds[1].length = 0;
    wds[1].ptr = NULL;
    memset(&pb.csParam, 0, sizeof pb.csParam);
    pb.csCode = UDPWrite;
    pb.udpStream = stream;
    pb.csParam.send.remoteHost = host;
    pb.csParam.send.remotePort = port;
    pb.csParam.send.wdsPtr = (Ptr)wds;
    pb.csParam.send.checkSum = true;
    if((err = PBControlSync((ParmBlkPtr)&pb)) != noErr)
    {
        sprintf(detail, "send err=%d", err);
        report_fail("udp_send", detail);
        detail[0] = 0;
    }
    else
    {
        report_pass("udp_send");
        memset(&pb.csParam, 0, sizeof pb.csParam);
        pb.csCode = UDPRead;
        pb.udpStream = stream;
        pb.csParam.receive.timeOut = 3;
        err = PBControlSync((ParmBlkPtr)&pb);
        if(err != noErr)
            sprintf(detail, err == commandTimeout ? "no response" : "recv err=%d", err);
        else
        {
            if(pb.csParam.receive.rcvBuffLen != 4 || memcmp(pb.csParam.receive.rcvBuff, "ping", 4) != 0)
            {
                sprintf(detail, "got %u bytes", pb.csParam.receive.rcvBuffLen);
                err = -1;
            }
            pb.csCode = UDPBfrReturn;
            PBControlSync((ParmBlkPtr)&pb);
        }
    }
    memset(&pb.csParam, 0, sizeof pb.csParam);
    pb.csCode = UDPRelease;
    pb.udpStream = stream;
    PBControlSync((ParmBlkPtr)&pb);
    DisposePtr(buf);
    return err;
}

/* 0: echoed; 1: no connection; 2: connected, no echo. */
static int tcp_echo(unsigned long host, unsigned short port, char *detail)
{
    TCPiopb pb;
    wdsEntry wds[2];
    StreamPtr stream;
    Ptr buf = NewPtr(8192);
    char in[64];
    OSErr err;
    int result = 1;

    memset(&pb, 0, sizeof pb);
    pb.ioCRefNum = ipp;
    pb.csCode = TCPCreate;
    pb.csParam.create.rcvBuff = buf;
    pb.csParam.create.rcvBuffLen = 8192;
    if((err = PBControlSync((ParmBlkPtr)&pb)) != noErr)
    {
        sprintf(detail, "err=%d", err);
        report_fail("tcp_socket", detail);
        DisposePtr(buf);
        return -1;
    }
    report_pass("tcp_socket");
    stream = pb.tcpStream;

    memset(&pb.csParam, 0, sizeof pb.csParam);
    pb.csCode = TCPActiveOpen;
    pb.tcpStream = stream;
    pb.csParam.open.ulpTimeoutValue = 10;
    pb.csParam.open.ulpTimeoutAction = 1;   /* abort */
    pb.csParam.open.validityFlags = 0xC0;   /* both of those given */
    pb.csParam.open.commandTimeoutValue = 10;
    pb.csParam.open.remoteHost = host;
    pb.csParam.open.remotePort = port;
    if((err = PBControlSync((ParmBlkPtr)&pb)) != noErr)
        sprintf(detail, "err=%d", err);
    else
    {
        report_pass("tcp_connect");
        result = 2;
        wds[0].length = 4;
        wds[0].ptr = "ping";
        wds[1].length = 0;
        wds[1].ptr = NULL;
        memset(&pb.csParam, 0, sizeof pb.csParam);
        pb.csCode = TCPSend;
        pb.tcpStream = stream;
        pb.csParam.send.pushFlag = true;
        pb.csParam.send.wdsPtr = (Ptr)wds;
        if((err = PBControlSync((ParmBlkPtr)&pb)) != noErr)
        {
            sprintf(detail, "err=%d", err);
            report_fail("tcp_send", detail);
            detail[0] = 0;
        }
        else
        {
            memset(&pb.csParam, 0, sizeof pb.csParam);
            pb.csCode = TCPRcv;
            pb.tcpStream = stream;
            pb.csParam.receive.commandTimeoutValue = 3;
            pb.csParam.receive.rcvBuff = in;
            pb.csParam.receive.rcvBuffLen = sizeof in;
            err = PBControlSync((ParmBlkPtr)&pb);
            if(err != noErr)
                sprintf(detail, err == commandTimeout ? "no response" : "read err=%d", err);
            else if(pb.csParam.receive.rcvBuffLen == 4 && memcmp(in, "ping", 4) == 0)
                result = 0;
            else
                sprintf(detail, "got %u bytes", pb.csParam.receive.rcvBuffLen);
        }
    }
    memset(&pb.csParam, 0, sizeof pb.csParam);
    pb.csCode = TCPAbort;
    pb.tcpStream = stream;
    PBControlSync((ParmBlkPtr)&pb);
    pb.csCode = TCPRelease;
    PBControlSync((ParmBlkPtr)&pb);
    DisposePtr(buf);
    return result;
}

void test_network(void)
{
    unsigned char ipp_name[] = "\004.IPP";
    unsigned int a, b, c, d, port;
    unsigned long echo = (10UL << 24) | 2 << 8 | 1, addr;
    unsigned short echo_port = 7;
    int named = 0, r;
    char detail[64], name[48];
    FILE *cfg;
    OSErr err;
    void (*miss)(const char *, const char *);

    if((err = OpenDriver(ipp_name, &ipp)) != noErr)
    {
        sprintf(detail, "OpenDriver .IPP err=%d", err);
        report_fail("net_mactcp", detail);
        return;
    }
    report_pass("net_mactcp");

    if((err = dnr_open()) != noErr)
    {
        sprintf(detail, "open resolver err=%d", err);
        report_fail("dns_resolve", detail);
    }
    else
    {
        err = resolve("example.com", &addr);
        if(err == noErr)
        {
            sprintf(name, "dns_resolve_%lu.%lu.%lu.%lu", addr >> 24, (addr >> 16) & 255,
                    (addr >> 8) & 255, addr & 255);
            report_pass(name);
        }
        else
        {
            sprintf(detail, "err=%d", err);
            report_fail("dns_resolve", detail);
        }
        dnr_close();
    }

    if((cfg = fopen("Host:net_echo.txt", "r")) != NULL)
    {
        if(fscanf(cfg, "%u.%u.%u.%u %u", &a, &b, &c, &d, &port) == 5)
        {
            echo = (unsigned long)a << 24 | (unsigned long)b << 16 | c << 8 | d;
            echo_port = port;
            named = 1;
        }
        fclose(cfg);
    }
    miss = named ? report_fail : report_skip;

    detail[0] = 0;
    err = udp_echo(echo, echo_port, detail);
    if(err == noErr)
        report_pass("udp_echo");
    else if(detail[0] && strncmp(detail, "UDPCreate", 9) == 0)
        report_fail("udp_socket", detail);
    else if(detail[0])
        miss("udp_echo", detail);

    detail[0] = 0;
    r = tcp_echo(echo, echo_port, detail);
    if(r == 0)
        report_pass("tcp_echo");
    else if(r == 1)
        miss("tcp_connect", detail);
    else if(r == 2 && detail[0])
        miss("tcp_echo", detail);
}
