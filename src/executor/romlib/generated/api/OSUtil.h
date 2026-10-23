/*****************************************************************************
 *                                                                           *
 *                                  OSUtil.h                                 *
 *                                  ========                                 *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h       Byte, INTEGER, LONGINT, Boolean, SignedByte,         *
 *                      QElemPtr, OSErr, Handle, ConstStringPtr,             *
 *                      StringPtr, ULONGINT, QHdrPtr, Str255,                *
 *                      ConstStr255Param                                     *
 *     VRetraceMgr.h    VBLTask                                              *
 *     FileMgr.h        ParamBlockRec, DrvQEl, VCB                           *
 *     OSEvent.h        EvQEl                                                *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "VRetraceMgr.h"
#include "FileMgr.h"
#include "OSEvent.h"

#define MODULE_NAME OSUtil
#include <base/api-module.h>


namespace Executor {

enum {macXLMachine = 0,
macMachine = 1,
UNIXMachine = 1127,
};

enum {clkRdErr = -85,
clkWrErr = -86,
prInitErr = -88,
prWrErr = -87,
hwParamErr = -502,
};

struct SysParmType{GUEST_STRUCT;Byte valid;Byte aTalkA;Byte aTalkB;Byte config;GUEST<INTEGER> portA;GUEST<INTEGER> portB;GUEST<LONGINT> alarm;GUEST<INTEGER> font;GUEST<INTEGER> kbdPrint;GUEST<INTEGER> volClik;GUEST<INTEGER> misc;};static_assert(sizeof(SysParmType) == 20);void describeStruct(const SysParmType&, std::ostream&);

typedef SysParmType *SysPPtr;

typedef enum {dummyType,
vType,
ioQType,
drvQType,
evType,
fsQType,
}QTypes;

union QElem{GUEST_STRUCT;VBLTask vblQElem;ParamBlockRec ioQElem;DrvQEl drvQElem;EvQEl evQElem;VCB vcbQElem;};static_assert(sizeof(QElem) == 178);void describeStruct(const QElem&, std::ostream&);

struct DateTimeRec{GUEST_STRUCT;GUEST<INTEGER> year;GUEST<INTEGER> month;GUEST<INTEGER> day;GUEST<INTEGER> hour;GUEST<INTEGER> minute;GUEST<INTEGER> second;GUEST<INTEGER> dayOfWeek;};static_assert(sizeof(DateTimeRec) == 14);void describeStruct(const DateTimeRec&, std::ostream&);

struct SysEnvRec{GUEST_STRUCT;GUEST<INTEGER> environsVersion;GUEST<INTEGER> machineType;GUEST<INTEGER> systemVersion;GUEST<INTEGER> processor;Boolean hasFPU;Boolean hasColorQD;GUEST<INTEGER> keyBoardType;GUEST<INTEGER> atDrvrVersNum;GUEST<INTEGER> sysVRefNum;};static_assert(sizeof(SysEnvRec) == 16);void describeStruct(const SysEnvRec&, std::ostream&);

typedef SysEnvRec *SysEnvRecPtr;

enum {curSysEnvVers = 2,
};

// sysEnv machine types
enum {envMachUnknown = 0,
env512KE = 1,
envMacPlus = 2,
envSE = 3,
envMacII = 4,
envMac = -1,
envXL = -2,
};

enum {envCPUUnknown = 0,
env68000 = 1,
env68020 = 3,
env68030 = 4,
env68040 = 5,
};

enum {envUnknownKbd = 0,
envMacKbd = 1,
envMacAndPad = 2,
envMacPlusKbd = 3,
envAExtendKbd = 4,
envStandADBKbd = 5,
};

enum {envBadVers = -5501,
envVersTooBig = -5502,
};

typedef SignedByte TrapType;

enum {kOSTrapType,
kToolboxTrapType,
};

// OSUtil ThinkC (true);
const LowMemGlobal<INTEGER > SysVersion{ 0x15A };
inline INTEGER  LMGetSysVersion() { return LM(SysVersion); }
inline void LMSetSysVersion(INTEGER val) { LM(SysVersion) = val; }
NOTRAP_FUNCTION2(LMGetSysVersion);
NOTRAP_FUNCTION2(LMSetSysVersion);


// OSUtil IMII-392 (true);
const LowMemGlobal<Byte > SPValid{ 0x1F8 };
inline Byte  LMGetSPValid() { return LM(SPValid); }
inline void LMSetSPValid(Byte val) { LM(SPValid) = val; }
NOTRAP_FUNCTION2(LMGetSPValid);
NOTRAP_FUNCTION2(LMSetSPValid);


// OSUtil IMII-392 (true);
const LowMemGlobal<Byte > SPATalkA{ 0x1F9 };
inline Byte  LMGetSPATalkA() { return LM(SPATalkA); }
inline void LMSetSPATalkA(Byte val) { LM(SPATalkA) = val; }
NOTRAP_FUNCTION2(LMGetSPATalkA);
NOTRAP_FUNCTION2(LMSetSPATalkA);


// OSUtil IMII-392 (true);
const LowMemGlobal<Byte > SPATalkB{ 0x1FA };
inline Byte  LMGetSPATalkB() { return LM(SPATalkB); }
inline void LMSetSPATalkB(Byte val) { LM(SPATalkB) = val; }
NOTRAP_FUNCTION2(LMGetSPATalkB);
NOTRAP_FUNCTION2(LMSetSPATalkB);


// OSUtil IMII-392 (true);
const LowMemGlobal<Byte > SPConfig{ 0x1FB };
inline Byte  LMGetSPConfig() { return LM(SPConfig); }
inline void LMSetSPConfig(Byte val) { LM(SPConfig) = val; }
NOTRAP_FUNCTION2(LMGetSPConfig);
NOTRAP_FUNCTION2(LMSetSPConfig);


// OSUtil IMII-392 (true);
const LowMemGlobal<INTEGER > SPPortA{ 0x1FC };
inline INTEGER  LMGetSPPortA() { return LM(SPPortA); }
inline void LMSetSPPortA(INTEGER val) { LM(SPPortA) = val; }
NOTRAP_FUNCTION2(LMGetSPPortA);
NOTRAP_FUNCTION2(LMSetSPPortA);


// OSUtil IMII-392 (true);
const LowMemGlobal<INTEGER > SPPortB{ 0x1FE };
inline INTEGER  LMGetSPPortB() { return LM(SPPortB); }
inline void LMSetSPPortB(INTEGER val) { LM(SPPortB) = val; }
NOTRAP_FUNCTION2(LMGetSPPortB);
NOTRAP_FUNCTION2(LMSetSPPortB);


// OSUtil IMII-392 (true);
const LowMemGlobal<LONGINT > SPAlarm{ 0x200 };
inline LONGINT  LMGetSPAlarm() { return LM(SPAlarm); }
inline void LMSetSPAlarm(LONGINT val) { LM(SPAlarm) = val; }
NOTRAP_FUNCTION2(LMGetSPAlarm);
NOTRAP_FUNCTION2(LMSetSPAlarm);


// OSUtil IMII-392 (true);
const LowMemGlobal<INTEGER > SPFont{ 0x204 };
inline INTEGER  LMGetSPFont() { return LM(SPFont); }
inline void LMSetSPFont(INTEGER val) { LM(SPFont) = val; }
NOTRAP_FUNCTION2(LMGetSPFont);
NOTRAP_FUNCTION2(LMSetSPFont);


// OSUtil IMII-369 (true);
const LowMemGlobal<Byte > SPKbd{ 0x206 };
inline Byte  LMGetSPKbd() { return LM(SPKbd); }
inline void LMSetSPKbd(Byte val) { LM(SPKbd) = val; }
NOTRAP_FUNCTION2(LMGetSPKbd);
NOTRAP_FUNCTION2(LMSetSPKbd);


// OSUtil IMII-392 (true);
const LowMemGlobal<Byte > SPPrint{ 0x207 };
inline Byte  LMGetSPPrint() { return LM(SPPrint); }
inline void LMSetSPPrint(Byte val) { LM(SPPrint) = val; }
NOTRAP_FUNCTION2(LMGetSPPrint);
NOTRAP_FUNCTION2(LMSetSPPrint);


// OSUtil IMII-392 (true);
const LowMemGlobal<Byte > SPVolCtl{ 0x208 };
inline Byte  LMGetSPVolCtl() { return LM(SPVolCtl); }
inline void LMSetSPVolCtl(Byte val) { LM(SPVolCtl) = val; }
NOTRAP_FUNCTION2(LMGetSPVolCtl);
NOTRAP_FUNCTION2(LMSetSPVolCtl);


// OSUtil IMII-392 (true);
const LowMemGlobal<Byte > SPClikCaret{ 0x209 };
inline Byte  LMGetSPClikCaret() { return LM(SPClikCaret); }
inline void LMSetSPClikCaret(Byte val) { LM(SPClikCaret) = val; }
NOTRAP_FUNCTION2(LMGetSPClikCaret);
NOTRAP_FUNCTION2(LMSetSPClikCaret);


// OSUtil IMII-392 (true);
const LowMemGlobal<Byte > SPMisc2{ 0x20B };
inline Byte  LMGetSPMisc2() { return LM(SPMisc2); }
inline void LMSetSPMisc2(Byte val) { LM(SPMisc2) = val; }
NOTRAP_FUNCTION2(LMGetSPMisc2);
NOTRAP_FUNCTION2(LMSetSPMisc2);


// OSUtil IMI-260 (true);
const LowMemGlobal<ULONGINT > Time{ 0x20C };
inline ULONGINT  LMGetTime() { return LM(Time); }
inline void LMSetTime(ULONGINT val) { LM(Time) = val; }
NOTRAP_FUNCTION2(LMGetTime);
NOTRAP_FUNCTION2(LMSetTime);


// OSUtil IMII-372 (false);
const LowMemGlobal<INTEGER > CrsrThresh{ 0x8EC };
inline INTEGER  LMGetCrsrThresh() { return LM(CrsrThresh); }
inline void LMSetCrsrThresh(INTEGER val) { LM(CrsrThresh) = val; }
NOTRAP_FUNCTION2(LMGetCrsrThresh);
NOTRAP_FUNCTION2(LMSetCrsrThresh);


// OSUtil MPW (false);
const LowMemGlobal<Byte > MMUType{ 0xCB1 };
inline Byte  LMGetMMUType() { return LM(MMUType); }
inline void LMSetMMUType(Byte val) { LM(MMUType) = val; }
NOTRAP_FUNCTION2(LMGetMMUType);
NOTRAP_FUNCTION2(LMSetMMUType);


// OSUtil IMV-592 (true-b);
const LowMemGlobal<Byte > MMU32Bit{ 0xCB2 };
inline Byte  LMGetMMU32Bit() { return LM(MMU32Bit); }
inline void LMSetMMU32Bit(Byte val) { LM(MMU32Bit) = val; }
NOTRAP_FUNCTION2(LMGetMMU32Bit);
NOTRAP_FUNCTION2(LMSetMMU32Bit);


// OSUtil IMV-466 (false);
const LowMemGlobal<QHdr > DTQueue{ 0xD92 };
inline void LMGetDTQueue(QHdr *val) { *val = LM(DTQueue); }
inline void LMSetDTQueue(const QHdr *val) { LM(DTQueue) = *val; }
NOTRAP_FUNCTION2(LMGetDTQueue);
NOTRAP_FUNCTION2(LMSetDTQueue);


// OSUtil IMV (false);
const LowMemGlobal<ProcPtr > JDTInstall{ 0xD9C };
inline ProcPtr  LMGetJDTInstall() { return LM(JDTInstall); }
inline void LMSetJDTInstall(ProcPtr val) { LM(JDTInstall) = val; }
NOTRAP_FUNCTION2(LMGetJDTInstall);
NOTRAP_FUNCTION2(LMSetJDTInstall);


struct DeferredTask{GUEST_STRUCT;GUEST<QElemPtr> qLink;GUEST<INTEGER> qType;GUEST<INTEGER> dtFlags;GUEST<ProcPtr> dtAddr;GUEST<LONGINT> dtParam;GUEST<LONGINT> dtReserved;};static_assert(sizeof(DeferredTask) == 20);void describeStruct(const DeferredTask&, std::ostream&);

typedef DeferredTask *DeferredTaskPtr;

// MacPhoenix: the Deferred Task Manager (IM V-466). A task queued from
// an interrupt runs when the interrupt is over, with A1 = dtParam;
// here at the next tick, after the VBL tasks (time/vbl.cpp). Apple's
// Speech Manager queues one from its sound channel callback.
OSErr C_DTInstall(DeferredTaskPtr dtTaskPtr);REGISTER_TRAP(DTInstall, 0xA082, D0(A0));


OSErr HandToHand(GUEST<Handle> *h);REGISTER_TRAP2(HandToHand, 0xA9E1, D0(InOut<Handle, A0>), SaveA1D1D2, CCFromD0);


OSErr PtrToHand(const void *p, GUEST<Handle> *h, LONGINT s);REGISTER_TRAP2(PtrToHand, 0xA9E3, D0(A0, Out<Handle, A0>, D0), SaveA1D1D2, CCFromD0);


OSErr PtrToXHand(const void *p, Handle h, LONGINT s);REGISTER_TRAP2(PtrToXHand, 0xA9E2, D0(A0, A1, D0), MoveA1ToA0, SaveA1D1D2, CCFromD0);


OSErr HandAndHand(Handle h1, Handle h2);REGISTER_TRAP2(HandAndHand, 0xA9E4, D0(A0, A1), MoveA1ToA0, SaveA1D1D2, CCFromD0);


OSErr PtrAndHand(const void *p, Handle h, LONGINT s1);REGISTER_TRAP2(PtrAndHand, 0xA9EF, D0(A0, A1, D0), MoveA1ToA0, SaveA1D1D2, CCFromD0);


LONGINT ROMlib_RelString(const uint8_t *s1, const uint8_t *s2, Boolean casesig, Boolean diacsig, LONGINT d0);

INTEGER RelString(ConstStringPtr s1, ConstStringPtr s2, Boolean casesig, Boolean diacsig);

Boolean EqualString(ConstStringPtr s1, ConstStringPtr s2, Boolean casesig, Boolean diacsig);

void ROMlib_UprString(StringPtr s, Boolean diac, INTEGER len);

void UpperString(StringPtr s, Boolean diac);

void GetDateTime(GUEST<ULONGINT> *mactimepointer);

OSErr ReadDateTime(GUEST<ULONGINT> *secs);REGISTER_TRAP2(ReadDateTime, 0xA039, D0(A0));


OSErr SetDateTime(ULONGINT mactime);REGISTER_TRAP2(SetDateTime, 0xA03A, D0(D0));


void DateToSeconds(DateTimeRec *d, GUEST<ULONGINT> *s);REGISTER_TRAP2(DateToSeconds, 0xA9C7, void(A0, Out<ULONGINT, D0>), SaveA1D1D2);


void SecondsToDate(ULONGINT mactime, DateTimeRec *d);REGISTER_TRAP2(SecondsToDate, 0xA9C6, void(D0, A0), SaveA1D1D2);


void GetTime(DateTimeRec *d);

void SetTime(DateTimeRec *d);

OSErr InitUtil();REGISTER_TRAP2(InitUtil, 0xA03F, D0());


SysPPtr GetSysPPtr();

OSErr WriteParam();REGISTER_TRAP2(WriteParam, 0xA038, D0());


void Enqueue(QElemPtr e, QHdrPtr h);REGISTER_TRAP2(Enqueue, 0xA96F, void(A0, A1), SaveA1D1D2);


OSErr Dequeue(QElemPtr e, QHdrPtr h);REGISTER_TRAP2(Dequeue, 0xA96E, D0(A0, A1), SaveA1D1D2);


// extern LONGINT GetTrapAddress(INTEGER n); // 68K in emustubs, not supported on ppc
// extern void SetTrapAddress(LONGINT addr,
// INTEGER n);
ProcPtr C_NGetTrapAddress(uint16_t n, TrapType ttype);NOTRAP_FUNCTION(NGetTrapAddress);


void C_NSetTrapAddress(ProcPtr addr, uint16_t n, TrapType ttype);NOTRAP_FUNCTION(NSetTrapAddress);


ProcPtr _GetTrapAddress_flags(uint16_t n, bool newTraps, bool tool);REGISTER_2FLAG_TRAP(_GetTrapAddress_flags, GetTrapAddress, INVALID_GetTrapAddress, GetOSTrapAddress, GetToolTrapAddress, 0xA146, ProcPtr(uint16_t ), A0(D0, TrapBit<0x200>, TrapBit<0x400>), ClearD0);


void _SetTrapAddress_flags(ProcPtr addr, uint16_t n, bool newTraps, bool tool);REGISTER_2FLAG_TRAP(_SetTrapAddress_flags, SetTrapAddress, INVALID_SetTrapAddress, SetOSTrapAddress, SetToolTrapAddress, 0xA047, void(ProcPtr , uint16_t ), void(A0, D0, TrapBit<0x200>, TrapBit<0x400>), ClearD0);


ProcPtr C_GetToolboxTrapAddress(uint16_t n);NOTRAP_FUNCTION(GetToolboxTrapAddress);


void C_SetToolboxTrapAddress(ProcPtr addr, uint16_t n);NOTRAP_FUNCTION(SetToolboxTrapAddress);


enum {_Unimplemented = 43167,
};

void Delay(LONGINT n, GUEST<LONGINT> *ftp);REGISTER_TRAP2(Delay, 0xA03B, void(A0, Out<LONGINT, D0>));


void C_SysBeep(INTEGER i);PASCAL_TRAP(SysBeep, 0xA9C8);


void Environs(GUEST<INTEGER> *rom, GUEST<INTEGER> *machine);

OSErr SysEnvirons(INTEGER vers, SysEnvRecPtr p);REGISTER_TRAP2(SysEnvirons, 0xA090, D0(D0, A0));


void Restart();

void SetUpA5();

void RestoreA5();

Byte GetMMUMode();NOTRAP_FUNCTION2(GetMMUMode);


void SwapMMUMode(Byte *bp);REGISTER_TRAP2(SwapMMUMode, 0xA05D, void(InOut<Byte, D0>));


uint32_t StripAddress(uint32_t l);REGISTER_TRAP2(StripAddress, 0xA055, D0(D0));


void C_DebugStr(ConstStringPtr p);PASCAL_TRAP(DebugStr, 0xABFF);


void C_Debugger();PASCAL_TRAP(Debugger, 0xA9FF);


void C_MakeDataExecutable(void *ptr, uint32_t sz);NOTRAP_FUNCTION(MakeDataExecutable);


void C_FlushCodeCache();PASCAL_TRAP(FlushCodeCache, 0xA0BD);


void HWPriv(LONGINT d0, LONGINT a0);REGISTER_TRAP2(HWPriv, 0xA198, void(D0, A0));


LONGINT SetCurrentA5();

LONGINT SetA5(LONGINT a5);

void c2pstrcpy(Str255 dst, const char *src);

void p2cstrcpy(char *dst, ConstStr255Param src);

void CopyCStringToPascal(const char *src, Str255 dst);

void CopyPascalStringToC(ConstStr255Param src, char *dst);

} /* namespace Executor */