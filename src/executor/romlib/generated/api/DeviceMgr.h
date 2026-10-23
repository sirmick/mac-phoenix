/*****************************************************************************
 *                                                                           *
 *                                DeviceMgr.h                                *
 *                                ===========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h     OSErr, INTEGER, Str255, QHdr, LONGINT, Handle,         *
 *                    Boolean, ConstStringPtr, Ptr                           *
 *     FileMgr.h      ParmBlkPtr                                             *
 *     WindowMgr.h    WindowPtr                                              *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "FileMgr.h"
#include "WindowMgr.h"

#define MODULE_NAME DeviceMgr
#include <base/api-module.h>


namespace Executor {

struct DCtlEntry;

typedef DCtlEntry *DCtlPtr;

typedef GUEST<DCtlPtr> *DCtlHandle;

typedef GUEST<DCtlHandle> *DCtlHandlePtr;

using DriverUPP = UPP<OSErr(ParmBlkPtr , DCtlPtr ), Register<D0(A0, A1)>>;


// Executor's native (C++) driver. MacPhoenix: starts like a real 'DRVR'
// header (the routine offsets are 0), so the name is where code walking
// the unit table expects it; the entry points follow the name.
struct umacdriver{GUEST_STRUCT;GUEST<INTEGER> udrvrFlags;GUEST<INTEGER> udrvrDelay;GUEST<INTEGER> udrvrEMask;GUEST<INTEGER> udrvrMenu;GUEST<INTEGER> udrvrOffsets[5];GUEST<Str255> udrvrName;GUEST<DriverUPP> udrvrOpen;GUEST<DriverUPP> udrvrPrime; // read and write
GUEST<DriverUPP> udrvrCtl; // control and killio
GUEST<DriverUPP> udrvrStatus;GUEST<DriverUPP> udrvrClose;};void describeStruct(const umacdriver&, std::ostream&);

typedef umacdriver *umacdriverptr;

struct ramdriver{GUEST_STRUCT;GUEST<INTEGER> drvrFlags;GUEST<INTEGER> drvrDelay;GUEST<INTEGER> drvrEMask;GUEST<INTEGER> drvrMenu;GUEST<INTEGER> drvrOpen;GUEST<INTEGER> drvrPrime;GUEST<INTEGER> drvrCtl;GUEST<INTEGER> drvrStatus;GUEST<INTEGER> drvrClose;char drvrName;};static_assert(sizeof(ramdriver) == 20);void describeStruct(const ramdriver&, std::ostream&);

typedef ramdriver *ramdriverptr;

typedef GUEST<ramdriverptr> *ramdriverhand;

typedef enum {Open,
Prime,
Ctl,
Stat,
Close,
}DriverRoutineType;

struct DCtlEntry{GUEST_STRUCT;GUEST<umacdriverptr> dCtlDriver; // not just Ptr
GUEST<INTEGER> dCtlFlags;QHdr dCtlQHdr;GUEST<LONGINT> dCtlPosition;GUEST<Handle> dCtlStorage;GUEST<INTEGER> dCtlRefNum;GUEST<LONGINT> dCtlCurTicks;GUEST<WindowPtr> dCtlWindow;GUEST<INTEGER> dCtlDelay;GUEST<INTEGER> dCtlEMask;GUEST<INTEGER> dCtlMenu;};static_assert(sizeof(DCtlEntry) == 40);void describeStruct(const DCtlEntry&, std::ostream&);

enum {asyncTrpBit = 1 << 10,
noQueueBit = 1 << 9,
};

enum {NEEDTIMEBIT = 1 << 13,
};

enum {aRdCmd = 2,
aWrCmd = 3,
};

enum {killCode = 1,
};

enum {NDEVICES = 96,
};

enum {abortErr = -27,
badUnitErr = -21,
controlErr = -17,
dInstErr = -26,
dRemovErr = -25,
notOpenErr = -28,
openErr = -23,
readErr = -19,
statusErr = -18,
unitEmptyErr = -22,
writErr = -20,
};

// DeviceMgr IMII-192 (false);
const LowMemGlobal<DCtlHandlePtr > UTableBase{ 0x11C };
inline DCtlHandlePtr  LMGetUTableBase() { return LM(UTableBase); }
inline void LMSetUTableBase(DCtlHandlePtr val) { LM(UTableBase) = val; }
NOTRAP_FUNCTION2(LMGetUTableBase);
NOTRAP_FUNCTION2(LMSetUTableBase);


// DeviceMgr IMII-197 (false);
const LowMemGlobal<GUEST<ProcPtr> [8]> Lvl1DT{ 0x192 };


// DeviceMgr IMII-198 (false);
const LowMemGlobal<GUEST<ProcPtr> [8]> Lvl2DT{ 0x1B2 };


// DeviceMgr ThinkC (true-b);
const LowMemGlobal<INTEGER > UnitNtryCnt{ 0x1D2 };
inline INTEGER  LMGetUnitNtryCnt() { return LM(UnitNtryCnt); }
inline void LMSetUnitNtryCnt(INTEGER val) { LM(UnitNtryCnt) = val; }
NOTRAP_FUNCTION2(LMGetUnitNtryCnt);
NOTRAP_FUNCTION2(LMSetUnitNtryCnt);


// DeviceMgr IMIII-39 (true-b);
const LowMemGlobal<Ptr > VIA{ 0x1D4 };
inline Ptr  LMGetVIA() { return LM(VIA); }
inline void LMSetVIA(Ptr val) { LM(VIA) = val; }
NOTRAP_FUNCTION2(LMGetVIA);
NOTRAP_FUNCTION2(LMSetVIA);


// DeviceMgr IMII-199 (false);
const LowMemGlobal<Ptr > SCCRd{ 0x1D8 };
inline Ptr  LMGetSCCRd() { return LM(SCCRd); }
inline void LMSetSCCRd(Ptr val) { LM(SCCRd) = val; }
NOTRAP_FUNCTION2(LMGetSCCRd);
NOTRAP_FUNCTION2(LMSetSCCRd);


// DeviceMgr IMII-199 (false);
const LowMemGlobal<Ptr > SCCWr{ 0x1DC };
inline Ptr  LMGetSCCWr() { return LM(SCCWr); }
inline void LMSetSCCWr(Ptr val) { LM(SCCWr) = val; }
NOTRAP_FUNCTION2(LMGetSCCWr);
NOTRAP_FUNCTION2(LMSetSCCWr);


// DeviceMgr ThinkC (false);
const LowMemGlobal<Ptr > IWM{ 0x1E0 };
inline Ptr  LMGetIWM() { return LM(IWM); }
inline void LMSetIWM(Ptr val) { LM(IWM) = val; }
NOTRAP_FUNCTION2(LMGetIWM);
NOTRAP_FUNCTION2(LMSetIWM);


// DeviceMgr IMII-199 (false);
const LowMemGlobal<GUEST<ProcPtr> [4]> ExtStsDT{ 0x2BE };


// DeviceMgr IMII-194 (false);
const LowMemGlobal<Ptr > JFetch{ 0x8F4 };
inline Ptr  LMGetJFetch() { return LM(JFetch); }
inline void LMSetJFetch(Ptr val) { LM(JFetch) = val; }
NOTRAP_FUNCTION2(LMGetJFetch);
NOTRAP_FUNCTION2(LMSetJFetch);


// DeviceMgr IMII-195 (false);
const LowMemGlobal<Ptr > JStash{ 0x8F8 };
inline Ptr  LMGetJStash() { return LM(JStash); }
inline void LMSetJStash(Ptr val) { LM(JStash) = val; }
NOTRAP_FUNCTION2(LMGetJStash);
NOTRAP_FUNCTION2(LMSetJStash);


// DeviceMgr IMII-195 (false);
const LowMemGlobal<Ptr > JIODone{ 0x8FC };
inline Ptr  LMGetJIODone() { return LM(JIODone); }
inline void LMSetJIODone(Ptr val) { LM(JIODone) = val; }
NOTRAP_FUNCTION2(LMGetJIODone);
NOTRAP_FUNCTION2(LMSetJIODone);


OSErr PBControl(ParmBlkPtr pbp, Boolean a);FILE_TRAP(PBControl, ParmBlkPtr, 0xA004);

OSErr PBStatus(ParmBlkPtr pbp, Boolean a);FILE_TRAP(PBStatus, ParmBlkPtr, 0xA005);

OSErr PBKillIO(ParmBlkPtr pbp, Boolean a);FILE_TRAP(PBKillIO, ParmBlkPtr, 0xA006);

OSErr OpenDriver(ConstStringPtr name, GUEST<INTEGER> *rnp);NOTRAP_FUNCTION2(OpenDriver);


OSErr CloseDriver(INTEGER rn);NOTRAP_FUNCTION2(CloseDriver);


OSErr Control(INTEGER rn, INTEGER code, const void *param);NOTRAP_FUNCTION2(Control);


OSErr Status(INTEGER rn, INTEGER code, Ptr param);NOTRAP_FUNCTION2(Status);


OSErr KillIO(INTEGER rn);NOTRAP_FUNCTION2(KillIO);


DCtlHandle GetDCtlEntry(INTEGER rn);NOTRAP_FUNCTION2(GetDCtlEntry);


} /* namespace Executor */