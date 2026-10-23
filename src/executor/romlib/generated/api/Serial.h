/*****************************************************************************
 *                                                                           *
 *                                  Serial.h                                 *
 *                                  ========                                 *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    SignedByte, Byte, OSErr, INTEGER, Ptr, LONGINT          *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME Serial
#include <base/api-module.h>


namespace Executor {

enum {baud300 = 380,
baud600 = 189,
baud1200 = 94,
baud1800 = 62,
baud2400 = 46,
baud3600 = 30,
baud4800 = 22,
baud7200 = 14,
baud9600 = 10,
baud14400 = 6,
baud19200 = 4,
baud28800 = 2,
baud38400 = 1,
baud57600 = 0,
};

enum {stop10 = 16384,
stop15 = -32768,
stop20 = -16384,
};

enum {noParity = 0,
oddParity = 4096,
evenParity = 12288,
};

enum {data5 = 0,
data6 = 2048,
data7 = 1024,
data8 = 3072,
};

enum {swOverrunErr = 1,
parityErr = 16,
hwOverrunErr = 32,
framingErr = 64,
};

enum {ctsEvent = 32,
breakEvent = 128,
};

enum {xOffWasSent = 128,
};

typedef SignedByte SPortSel;

enum {sPortA = 0,
sPortB = 1,
};

// Serial driver control codes
enum {kSERDConfiguration = 8,
kSERDInputBuffer = 9,
kSERDSerHShake = 10,
kSERDClearBreak = 11,
kSERDSetBreak = 12,
kSERDBaudRate = 13,
kSERDHandshake = 14,
kSERDClockMIDI = 15,
kSERDMiscOptions = 16,
kSERDAssertDTR = 17,
kSERDNegateDTR = 18,
kSERDSetPEChar = 19,
kSERDSetPEAltChar = 20,
kSERDSetXOffFlag = 21,
kSERDClearXOffFlag = 22,
kSERDSendXOn = 23,
kSERDSendXOnOut = 24,
kSERDSendXOff = 25,
kSERDSendXOffOut = 26,
kSERDResetChannel = 27,
kSERDHandshakeRS232 = 28,
kSERDStickParity = 29,
kSERDAssertRTS = 30,
kSERDNegateRTS = 31,
kSERD115KBaud = 115,
kSERD230KBaud = 230,
};

// Serial driver status codes
enum {kSERDInputCount = 2,
kSERDStatus = 8,
kSERDVersion = 9,
kSERDGetDCD = 256,
};

struct SerShk{GUEST_STRUCT;Byte fXOn;Byte fCTS;Byte xOn;Byte xOff;Byte errs;Byte evts;Byte fInX;Byte null;};static_assert(sizeof(SerShk) == 8);void describeStruct(const SerShk&, std::ostream&);

struct SerStaRec{GUEST_STRUCT;Byte cumErrs;Byte xOffSent;Byte rdPend;Byte wrPend;Byte ctsHold;Byte xOffHold;Byte dsrHold; // unimplemented
Byte modemStatus; // unimplemented
};static_assert(sizeof(SerStaRec) == 8);void describeStruct(const SerStaRec&, std::ostream&);

BEGIN_EXECUTOR_ONLY
const char *const MODEMINAME = ".AIn";
const char *const MODEMONAME = ".AOut";
const char *const PRNTRINAME = ".AIn";
const char *const PRNTRONAME = ".AOut";
END_EXECUTOR_ONLY


enum {MODEMIRNUM = -6,
MODEMORNUM = -7,
PRNTRIRNUM = -8,
PRNTRORNUM = -9,
};

OSErr RAMSDOpen(SPortSel port);NOTRAP_FUNCTION2(RAMSDOpen);


void RAMSDClose(SPortSel port);NOTRAP_FUNCTION2(RAMSDClose);


OSErr SerReset(INTEGER rn, INTEGER config);NOTRAP_FUNCTION2(SerReset);


OSErr SerSetBuf(INTEGER rn, Ptr p, INTEGER len);NOTRAP_FUNCTION2(SerSetBuf);


OSErr SerHShake(INTEGER rn, const SerShk *flags);NOTRAP_FUNCTION2(SerHShake);


OSErr SerSetBrk(INTEGER rn);NOTRAP_FUNCTION2(SerSetBrk);


OSErr SerClrBrk(INTEGER rn);NOTRAP_FUNCTION2(SerClrBrk);


OSErr SerGetBuf(INTEGER rn, GUEST<LONGINT> *lp);NOTRAP_FUNCTION2(SerGetBuf);


OSErr SerStatus(INTEGER rn, SerStaRec *serstap);NOTRAP_FUNCTION2(SerStatus);


} /* namespace Executor */