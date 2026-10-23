/*****************************************************************************
 *                                                                           *
 *                                  MacTCP.h                                 *
 *                                  ========                                 *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    ULONGINT, Ptr, INTEGER, Byte, LONGINT, QElemPtr,        *
 *                   OSErr, StringPtr                                        *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME MacTCP
#include <base/api-module.h>


namespace Executor {

// IPv4 address.  Numeric value; guest byte order (big-endian) is
// identical to network byte order.
typedef ULONGINT ip_addr;

typedef uint16_t ip_port;

typedef uint16_t tcp_port;

// Opaque stream cookie returned by TCPCreate/UDPCreate and passed
// back in every subsequent call.  Applications must not
// dereference it.
typedef Ptr StreamPtr;

typedef enum {ipctlGetAddr = 15,
udpCreate = 20,
udpRead = 21,
udpBfrReturn = 22,
udpWrite = 23,
udpRelease = 24,
udpMaxMTUSize = 25,
udpStatus = 26,
udpMultiCreate = 27,
udpMultiSend = 28,
udpMultiRead = 29,
TCPCreate = 30,
TCPPassiveOpen = 31,
TCPActiveOpen = 32,
TCPSend = 34,
TCPNoCopyRcv = 35,
TCPRcvBfrReturn = 36,
TCPRcv = 37,
TCPClose = 38,
TCPAbort = 39,
TCPStatus = 40,
TCPExtendedStat = 41,
TCPRelease = 42,
TCPGlobalInfo = 43,
}MacTCPCsCode;

// MacTCP OSErr codes, -23000 .. -23048.
typedef enum {inProgress = 1, // ioResult while an asynchronous call is pending.
ipBadLapErr = -23000,
ipBadCnfgErr = -23001,
ipNoCnfgErr = -23002,
ipLoadErr = -23003,
ipBadAddr = -23004,
connectionClosing = -23005,
invalidLength = -23006,
connectionExists = -23007,
connectionDoesntExist = -23008,
insufficientResources = -23009,
invalidStreamPtr = -23010,
streamAlreadyOpen = -23011,
connectionTerminated = -23012,
invalidBufPtr = -23013,
invalidRDS = -23014,
invalidWDS = -23014,
openFailed = -23015,
commandTimeout = -23016,
duplicateSocket = -23017,
nameSyntaxErr = -23041,
cacheFault = -23042,
noResultProc = -23043,
noNameServer = -23044,
authNameErr = -23045,
noAnsErr = -23046,
dnrErr = -23047,
outOfMemory = -23048, // Apple's spelling; nothing else in multiversal defines this
// name, so there is no collision to work around.
}MacTCPErr;

typedef enum {TCPClosing = 1,
TCPULPTimeout = 2,
TCPTerminate = 3,
TCPDataArrival = 4,
TCPUrgent = 5,
TCPICMPReceived = 6,
}TCPEventCode;

typedef enum {TCPSClosed = 0,
TCPSListen = 2,
TCPSSynReceived = 4,
TCPSSynSent = 6,
TCPSEstablished = 8,
TCPSFinWait1 = 10,
TCPSFinWait2 = 12,
TCPSCloseWait = 14,
TCPSClosing = 16,
TCPSLastAck = 18,
TCPSTimeWait = 20,
}TCPConnectionState;

typedef enum {timeoutValue = 128,
timeoutAction = 64,
typeOfService = 32,
precedence = 16,
}TCPValidityFlags;

struct ICMPReport{GUEST_STRUCT;GUEST<StreamPtr> streamPtr;GUEST<ip_addr> localHost;GUEST<ip_port> localPort;GUEST<ip_addr> remoteHost;GUEST<ip_port> remotePort;GUEST<INTEGER> reportType;GUEST<uint16_t> optionalAddlInfo;GUEST<Ptr> optionalAddlInfoPtr;};static_assert(sizeof(ICMPReport) == 24);void describeStruct(const ICMPReport&, std::ostream&);

// Asynchronous notification routine (ASR).  Pascal convention.
using TCPNotifyUPP = UPP<void(StreamPtr tcpStream, uint16_t eventCode, Ptr userDataPtr, uint16_t terminReason, ICMPReport *icmpMsg)>;


// TCPSend gather list entry.  The list is terminated by an entry
// whose length is zero (the terminator's ptr field is absent /
// ignored).
struct wdsEntry{GUEST_STRUCT;GUEST<uint16_t> length;GUEST<Ptr> ptr;};static_assert(sizeof(wdsEntry) == 6);void describeStruct(const wdsEntry&, std::ostream&);

// TCPNoCopyRcv scatter list entry, filled in by the driver;
// returned via TCPRcvBfrReturn.  Same layout as wdsEntry.
struct rdsEntry{GUEST_STRUCT;GUEST<uint16_t> length;GUEST<Ptr> ptr;};static_assert(sizeof(rdsEntry) == 6);void describeStruct(const rdsEntry&, std::ostream&);

struct TCPCreatePB{GUEST_STRUCT;GUEST<Ptr> rcvBuff;GUEST<ULONGINT> rcvBuffLen;GUEST<TCPNotifyUPP> notifyProc;GUEST<Ptr> userDataPtr;};static_assert(sizeof(TCPCreatePB) == 16);void describeStruct(const TCPCreatePB&, std::ostream&);

// Used by both TCPActiveOpen and TCPPassiveOpen.
struct TCPOpenPB{GUEST_STRUCT;Byte ulpTimeoutValue;Byte ulpTimeoutAction;Byte validityFlags;Byte commandTimeoutValue;GUEST<ip_addr> remoteHost;GUEST<tcp_port> remotePort;GUEST<ip_addr> localHost;GUEST<tcp_port> localPort;Byte tosFlags;Byte precedence;Byte dontFrag;Byte timeToLive;Byte security;Byte optionCnt;Byte options[40];GUEST<Ptr> userDataPtr;};static_assert(sizeof(TCPOpenPB) == 66);void describeStruct(const TCPOpenPB&, std::ostream&);

struct TCPSendPB{GUEST_STRUCT;Byte ulpTimeoutValue;Byte ulpTimeoutAction;Byte validityFlags;Byte pushFlag;Byte urgentFlag;GUEST<Ptr> wdsPtr;GUEST<ULONGINT> sendFree;GUEST<uint16_t> sendLength;GUEST<Ptr> userDataPtr;};static_assert(sizeof(TCPSendPB) == 20);void describeStruct(const TCPSendPB&, std::ostream&);

// Used by TCPRcv (rcvBuff/rcvBuffLen), TCPNoCopyRcv
// (rdsPtr/rdsLength), and TCPRcvBfrReturn (rdsPtr).
// 
// The MacTCP Programmer's Guide describes this block incorrectly,
// and Apple's own header says as much in a comment: markFlag and
// urgentFlag sit at the front, immediately after
// commandTimeoutValue, with the pad byte after them rather than
// before rcvBuff.  Verified against Universal Interfaces 3.4.2.
// The total is 22 bytes either way, which is exactly why a
// size-only assertion does not catch the difference.
struct TCPReceivePB{GUEST_STRUCT;Byte commandTimeoutValue;Byte markFlag;Byte urgentFlag;GUEST<Ptr> rcvBuff;GUEST<uint16_t> rcvBuffLen;GUEST<Ptr> rdsPtr;GUEST<uint16_t> rdsLength;GUEST<uint16_t> secondTimeStamp;GUEST<Ptr> userDataPtr;};static_assert(sizeof(TCPReceivePB) == 22);void describeStruct(const TCPReceivePB&, std::ostream&);

struct TCPClosePB{GUEST_STRUCT;Byte ulpTimeoutValue;Byte ulpTimeoutAction;Byte validityFlags;GUEST<Ptr> userDataPtr;};static_assert(sizeof(TCPClosePB) == 8);void describeStruct(const TCPClosePB&, std::ostream&);

struct TCPAbortPB{GUEST_STRUCT;GUEST<Ptr> userDataPtr;};static_assert(sizeof(TCPAbortPB) == 4);void describeStruct(const TCPAbortPB&, std::ostream&);

// Verified against Universal Interfaces 3.4.2.  Note connStatPtr
// between sendMaxSegSize and userDataPtr: it points at a
// TCPConnectionStats block, is not mentioned in the parameter
// table in the Programmer's Guide, and its omission is what makes
// this block 70 bytes rather than 66 -- and therefore what makes
// TCPiopb 102 bytes rather than 98.  Apple spells the round-trip
// field lastRTT, not lastRtt.
struct TCPStatusPB{GUEST_STRUCT;Byte ulpTimeoutValue;Byte ulpTimeoutAction;GUEST<LONGINT> unused;GUEST<ip_addr> remoteHost;GUEST<tcp_port> remotePort;GUEST<ip_addr> localHost;GUEST<tcp_port> localPort;Byte tosFlags;Byte precedence;Byte connectionState;GUEST<uint16_t> sendWindow;GUEST<uint16_t> rcvWindow;GUEST<uint16_t> amtUnackedData;GUEST<uint16_t> amtUnreadData;GUEST<Ptr> securityLevelPtr;GUEST<ULONGINT> sendUnacked;GUEST<ULONGINT> sendNext;GUEST<ULONGINT> congestionWindow;GUEST<ULONGINT> rcvNext;GUEST<ULONGINT> srtt;GUEST<ULONGINT> lastRTT;GUEST<ULONGINT> sendMaxSegSize;GUEST<Ptr> connStatPtr;GUEST<Ptr> userDataPtr;};static_assert(sizeof(TCPStatusPB) == 70);void describeStruct(const TCPStatusPB&, std::ostream&);

union TCPcsParam{GUEST_STRUCT;TCPCreatePB create;TCPOpenPB open;TCPSendPB send;TCPReceivePB receive;TCPClosePB close;TCPAbortPB abort;TCPStatusPB status;};static_assert(sizeof(TCPcsParam) == 70);void describeStruct(const TCPcsParam&, std::ostream&);

// The TCP I/O parameter block passed to PBControl on the .IPP
// driver.  Canonical offsets: ioCompletion 12, ioResult 16,
// ioNamePtr 18, ioVRefNum 22, ioCRefNum 24, csCode 26,
// tcpStream 28, csParam 32.  csParam is 70 bytes -- the size of
// its largest member, TCPStatusPB -- so the whole block is 102.
struct TCPiopb{GUEST_STRUCT;GUEST<QElemPtr> qLink;GUEST<INTEGER> qType;GUEST<INTEGER> ioTrap;GUEST<Ptr> ioCmdAddr;GUEST<ProcPtr> ioCompletion;GUEST<OSErr> ioResult;GUEST<StringPtr> ioNamePtr;GUEST<INTEGER> ioVRefNum;GUEST<INTEGER> ioCRefNum;GUEST<INTEGER> csCode;GUEST<StreamPtr> tcpStream;TCPcsParam csParam;};static_assert(sizeof(TCPiopb) == 102);void describeStruct(const TCPiopb&, std::ostream&);

typedef TCPiopb *TCPiopbPtr;

struct GetAddrParamBlock{GUEST_STRUCT;GUEST<QElemPtr> qLink;GUEST<INTEGER> qType;GUEST<INTEGER> ioTrap;GUEST<Ptr> ioCmdAddr;GUEST<ProcPtr> ioCompletion;GUEST<OSErr> ioResult;GUEST<StringPtr> ioNamePtr;GUEST<INTEGER> ioVRefNum;GUEST<INTEGER> ioCRefNum;GUEST<INTEGER> csCode;GUEST<ip_addr> ourAddress;GUEST<ip_addr> ourNetMask;};static_assert(sizeof(GetAddrParamBlock) == 36);void describeStruct(const GetAddrParamBlock&, std::ostream&);

typedef enum {UDPDataArrival = 1,
UDPICMPReceived = 2,
}UDPEventCode;

// UDP notification routine (ASR).  Pascal convention.
using UDPNotifyUPP = UPP<void(StreamPtr udpStream, uint16_t eventCode, Ptr userDataPtr, ICMPReport *icmpMsg)>;


// UDPCreate; localPort 0 asks for any free port and is answered in
// place.  endingPort belongs to udpMultiCreate.
struct UDPCreatePB{GUEST_STRUCT;GUEST<Ptr> rcvBuff;GUEST<ULONGINT> rcvBuffLen;GUEST<UDPNotifyUPP> notifyProc;GUEST<ip_port> localPort;GUEST<Ptr> userDataPtr;GUEST<ip_port> endingPort;};static_assert(sizeof(UDPCreatePB) == 20);void describeStruct(const UDPCreatePB&, std::ostream&);

struct UDPSendPB{GUEST_STRUCT;GUEST<uint16_t> reserved;GUEST<ip_addr> remoteHost;GUEST<ip_port> remotePort;GUEST<Ptr> wdsPtr;Byte checkSum;GUEST<INTEGER> sendLength;GUEST<Ptr> userDataPtr;GUEST<ip_port> localPort;};static_assert(sizeof(UDPSendPB) == 22);void describeStruct(const UDPSendPB&, std::ostream&);

// UDPRead hands back a datagram inside the stream's receive area
// (rcvBuff/rcvBuffLen out); UDPBfrReturn gives that area back.
struct UDPReceivePB{GUEST_STRUCT;GUEST<uint16_t> timeOut;GUEST<ip_addr> remoteHost;GUEST<ip_port> remotePort;GUEST<Ptr> rcvBuff;GUEST<uint16_t> rcvBuffLen;GUEST<uint16_t> secondTimeStamp;GUEST<Ptr> userDataPtr;GUEST<ip_addr> destHost;GUEST<ip_port> destPort;};static_assert(sizeof(UDPReceivePB) == 26);void describeStruct(const UDPReceivePB&, std::ostream&);

struct UDPMTUPB{GUEST_STRUCT;GUEST<uint16_t> mtuSize;GUEST<ip_addr> remoteHost;GUEST<Ptr> userDataPtr;};static_assert(sizeof(UDPMTUPB) == 10);void describeStruct(const UDPMTUPB&, std::ostream&);

union UDPcsParam{GUEST_STRUCT;UDPCreatePB create;UDPSendPB send;UDPReceivePB receive;UDPMTUPB mtu;};static_assert(sizeof(UDPcsParam) == 26);void describeStruct(const UDPcsParam&, std::ostream&);

// Same header as TCPiopb; udpStream at 28, csParam at 32.
struct UDPiopb{GUEST_STRUCT;GUEST<QElemPtr> qLink;GUEST<INTEGER> qType;GUEST<INTEGER> ioTrap;GUEST<Ptr> ioCmdAddr;GUEST<ProcPtr> ioCompletion;GUEST<OSErr> ioResult;GUEST<StringPtr> ioNamePtr;GUEST<INTEGER> ioVRefNum;GUEST<INTEGER> ioCRefNum;GUEST<INTEGER> csCode;GUEST<StreamPtr> udpStream;UDPcsParam csParam;};static_assert(sizeof(UDPiopb) == 58);void describeStruct(const UDPiopb&, std::ostream&);

// StrToAddr/AddrToName result; rtnCode is cacheFault while a lookup
// runs.
struct hostInfo{GUEST_STRUCT;GUEST<LONGINT> rtnCode;char cname[255];GUEST<ULONGINT> addr[4];};static_assert(sizeof(hostInfo) == 276);void describeStruct(const hostInfo&, std::ostream&);

} /* namespace Executor */