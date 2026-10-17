/* tcpip.c — see tcpip.h. */

#include "suite.h"
#include "tcpip.h"

#include <Devices.h>
#include <Errors.h>
#include <Events.h>
#include <Files.h>
#include <Folders.h>
#include <Memory.h>
#include <MixedMode.h>
#include <Resources.h>
#include <string.h>

static short ipp;

OSErr ip_open(void)
{
    static unsigned char name[] = "\004.IPP";

    return ipp ? noErr : OpenDriver(name, &ipp);
}

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

OSErr dns_open(void)
{
    short vRefNum, ref = -1;
    long dirID;
    OSErr err;

    if(dnr_code)
        return noErr;
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

void dns_close(void)
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

OSErr dns_resolve(char *name, unsigned long *addr)
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

/* ---- TCP */

enum { kTCPBuffer = 8192 };

static OSErr tcp_call(TCPiopb *pb, short code, StreamPtr stream)
{
    pb->ioCRefNum = ipp;
    pb->csCode = code;
    pb->tcpStream = stream;
    return PBControlSync((ParmBlkPtr)pb);
}

OSErr tcp_connect(unsigned long host, unsigned short port, int timeout, StreamPtr *stream)
{
    TCPiopb pb;
    OSErr err;

    memset(&pb, 0, sizeof pb);
    pb.csParam.create.rcvBuff = NewPtr(kTCPBuffer);
    pb.csParam.create.rcvBuffLen = kTCPBuffer;
    if((err = tcp_call(&pb, TCPCreate, 0)) != noErr)
    {
        DisposePtr(pb.csParam.create.rcvBuff);
        return err;
    }
    *stream = pb.tcpStream;
    memset(&pb.csParam, 0, sizeof pb.csParam);
    pb.csParam.open.ulpTimeoutValue = timeout;
    pb.csParam.open.ulpTimeoutAction = 1;   /* abort */
    pb.csParam.open.validityFlags = 0xC0;   /* both of those given */
    pb.csParam.open.commandTimeoutValue = timeout;
    pb.csParam.open.remoteHost = host;
    pb.csParam.open.remotePort = port;
    if((err = tcp_call(&pb, TCPActiveOpen, *stream)) != noErr)
    {
        tcp_close(*stream);
        *stream = 0;
    }
    return err;
}

OSErr tcp_write(StreamPtr stream, const void *data, unsigned short length)
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
    return tcp_call(&pb, TCPSend, stream);
}

long tcp_read(StreamPtr stream, char *buf, long max, int timeout)
{
    TCPiopb pb;
    unsigned long until = TickCount() + timeout * 60L;
    long got = 0;
    long left;
    OSErr err;

    while(got < max && TickCount() < until)
    {
        left = max - got;
        memset(&pb, 0, sizeof pb);
        pb.csParam.receive.commandTimeoutValue = 1;
        pb.csParam.receive.rcvBuff = buf + got;
        pb.csParam.receive.rcvBuffLen = left > 32767 ? 32767 : left;
        err = tcp_call(&pb, TCPRcv, stream);
        if(err == noErr)
            got += pb.csParam.receive.rcvBuffLen;
        else if(err != commandTimeout)
            break;                              /* closing, terminated: the end */
    }
    return got;
}

void tcp_close(StreamPtr stream)
{
    TCPiopb pb;

    memset(&pb, 0, sizeof pb);
    tcp_call(&pb, TCPAbort, stream);
    memset(&pb, 0, sizeof pb);
    if(tcp_call(&pb, TCPRelease, stream) == noErr)
        DisposePtr(pb.csParam.create.rcvBuff);
}

/* ---- UDP */

static OSErr udp_call(UDPiopb *pb, short code, StreamPtr stream)
{
    pb->ioCRefNum = ipp;
    pb->csCode = code;
    pb->udpStream = stream;
    return PBControlSync((ParmBlkPtr)pb);
}

OSErr udp_open(StreamPtr *stream)
{
    UDPiopb pb;
    OSErr err;

    memset(&pb, 0, sizeof pb);
    pb.csParam.create.rcvBuff = NewPtr(4096);
    pb.csParam.create.rcvBuffLen = 4096;
    if((err = udp_call(&pb, UDPCreate, 0)) != noErr)
        DisposePtr(pb.csParam.create.rcvBuff);
    else
        *stream = pb.udpStream;
    return err;
}

OSErr udp_send(StreamPtr stream, unsigned long host, unsigned short port, const void *data,
               unsigned short length)
{
    UDPiopb pb;
    wdsEntry wds[2];

    wds[0].length = length;
    wds[0].ptr = (Ptr)data;
    wds[1].length = 0;
    wds[1].ptr = NULL;
    memset(&pb, 0, sizeof pb);
    pb.csParam.send.remoteHost = host;
    pb.csParam.send.remotePort = port;
    pb.csParam.send.wdsPtr = (Ptr)wds;
    pb.csParam.send.checkSum = true;
    return udp_call(&pb, UDPWrite, stream);
}

OSErr udp_recv(StreamPtr stream, char *buf, int max, int timeout, int *length)
{
    UDPiopb pb;
    OSErr err;

    memset(&pb, 0, sizeof pb);
    pb.csParam.receive.timeOut = timeout;
    if((err = udp_call(&pb, UDPRead, stream)) != noErr)
        return err;
    *length = pb.csParam.receive.rcvBuffLen < max ? pb.csParam.receive.rcvBuffLen : max;
    BlockMoveData(pb.csParam.receive.rcvBuff, buf, *length);
    udp_call(&pb, UDPBfrReturn, stream);
    return noErr;
}

void udp_close(StreamPtr stream)
{
    UDPiopb pb;

    memset(&pb, 0, sizeof pb);
    if(udp_call(&pb, UDPRelease, stream) == noErr)
        DisposePtr(pb.csParam.create.rcvBuff);
}
