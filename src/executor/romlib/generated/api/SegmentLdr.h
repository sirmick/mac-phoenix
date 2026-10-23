/*****************************************************************************
 *                                                                           *
 *                                SegmentLdr.h                               *
 *                                ============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    INTEGER, OSType, Str255, StringPtr, Handle              *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME SegmentLdr
#include <base/api-module.h>


namespace Executor {

void C_ExitToShell();PASCAL_TRAP(ExitToShell, 0xA9F4);


enum {appOpen = 0,
appPrint = 1,
};

struct AppFile{GUEST_STRUCT;GUEST<INTEGER> vRefNum;GUEST<OSType> fType;GUEST<INTEGER> versNum;GUEST<Str255> fName;};static_assert(sizeof(AppFile) == 264);void describeStruct(const AppFile&, std::ostream&);

// SegmentLdr SysEqu.a (true-b);
const LowMemGlobal<Byte > loadtrap{ 0x12D };
inline Byte  LMGetloadtrap() { return LM(loadtrap); }
inline void LMSetloadtrap(Byte val) { LM(loadtrap) = val; }
NOTRAP_FUNCTION2(LMGetloadtrap);
NOTRAP_FUNCTION2(LMSetloadtrap);


// SegmentLdr IMII-59 (true);
const LowMemGlobal<Byte [16]> FinderName{ 0x2E0 };


// SegmentLdr IMII-58 (true);
const LowMemGlobal<INTEGER > CurApRefNum{ 0x900 };
inline INTEGER  LMGetCurApRefNum() { return LM(CurApRefNum); }
inline void LMSetCurApRefNum(INTEGER val) { LM(CurApRefNum) = val; }
NOTRAP_FUNCTION2(LMGetCurApRefNum);
NOTRAP_FUNCTION2(LMSetCurApRefNum);


// * NOTE: IMIII says CurApName is 32 bytes long, but it looks to me like
//  * it is really 34 bytes long.
// SegmentLdr IMII-58 (true);
const LowMemGlobal<Byte [34]> CurApName{ 0x910 };


// SegmentLdr IMII-62 (true-b);
const LowMemGlobal<INTEGER > CurJTOffset{ 0x934 };
inline INTEGER  LMGetCurJTOffset() { return LM(CurJTOffset); }
inline void LMSetCurJTOffset(INTEGER val) { LM(CurJTOffset) = val; }
NOTRAP_FUNCTION2(LMGetCurJTOffset);
NOTRAP_FUNCTION2(LMSetCurJTOffset);


// SegmentLdr IMII-60 (true);
const LowMemGlobal<INTEGER > CurPageOption{ 0x936 };
inline INTEGER  LMGetCurPageOption() { return LM(CurPageOption); }
inline void LMSetCurPageOption(INTEGER val) { LM(CurPageOption) = val; }
NOTRAP_FUNCTION2(LMGetCurPageOption);
NOTRAP_FUNCTION2(LMSetCurPageOption);


// SegmentLdr IMII-57 (true);
const LowMemGlobal<Handle > AppParmHandle{ 0xAEC };
inline Handle  LMGetAppParmHandle() { return LM(AppParmHandle); }
inline void LMSetAppParmHandle(Handle val) { LM(AppParmHandle) = val; }
NOTRAP_FUNCTION2(LMGetAppParmHandle);
NOTRAP_FUNCTION2(LMSetAppParmHandle);


void CountAppFiles(GUEST<INTEGER> *messagep, GUEST<INTEGER> *countp);

void GetAppFiles(INTEGER index, AppFile *filep);

void ClrAppFiles(INTEGER index);

BEGIN_EXECUTOR_ONLY
extern void Launch(ConstStringPtr appl, INTEGER vrefnum);
extern void Chain(ConstStringPtr appl, INTEGER vrefnum);
END_EXECUTOR_ONLY


void C_GetAppParms(StringPtr namep, GUEST<INTEGER> *rnp, GUEST<Handle> *aphandp);PASCAL_TRAP(GetAppParms, 0xA9F5);


void C_UnloadSeg(void *addr);PASCAL_TRAP(UnloadSeg, 0xA9F1);


enum {_LoadSeg = 43504,
_UnLoadSeg = 43505,
_Launch = 43506,
_Chain = 43507,
};

} /* namespace Executor */