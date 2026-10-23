/*****************************************************************************
 *                                                                           *
 *                                 Package.h                                 *
 *                                 =========                                 *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    INTEGER                                                 *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME Package
#include <base/api-module.h>


namespace Executor {

enum {dskInit = 2,
stdFile = 3,
flPoint = 4,
trFunc = 5,
intUtil = 6,
bdConv = 7,
};

// PackageMgr ThinkC (true-b);
const LowMemGlobal<GUEST<Handle> [8]> AppPacks{ 0xAB8 };


void C_InitPack(INTEGER packid);PASCAL_TRAP(InitPack, 0xA9E5);


void C_InitAllPacks();PASCAL_TRAP(InitAllPacks, 0xA9E6);


BEGIN_EXECUTOR_ONLY
RAW_68K_TRAP(Pack1, 0xA9E8);
END_EXECUTOR_ONLY


// MacPhoenix (name is a guess): 7.5.5's selector-table dispatcher, which
// its dispatcher traps and packages reach with `lea table,a0;
// move.l $7B0,-(sp); rts`. The table: kind.w (0: word offsets from the
// table, 1: word offsets indexed by selector, 2: long addresses),
// min.b, max.b, then the entries; D0's low byte selects, its high byte
// is the parameter size in words (popped, paramErr answered, when the
// selector is out of range). Executor implements it in sysroutines.cpp
// (clean room: docs/executor/cleanroom/system-routines.md).
const LowMemGlobal<ProcPtr > SelectorTableDispatch{ 0x7B0 };
inline ProcPtr  LMGetSelectorTableDispatch() { return LM(SelectorTableDispatch); }
inline void LMSetSelectorTableDispatch(ProcPtr val) { LM(SelectorTableDispatch) = val; }
NOTRAP_FUNCTION2(LMGetSelectorTableDispatch);
NOTRAP_FUNCTION2(LMSetSelectorTableDispatch);


// MacPhoenix (name is a guess): 7.5.5's call into a detached package
// handle (A0) with a lock count (A1 -> word) and the selector in D0:
// loads the handle if purged, locks it on the first call, re-stacks
// the Pascal parameters under a return into its own epilogue (which
// unlocks and returns the result word), and enters the package's
// selector table (10 bytes in) through SelectorTableDispatch. Color
// Picker 2.0's INIT calls its PACK 12 this way. Executor's copy is
// sysroutines.cpp.
const LowMemGlobal<ProcPtr > CallDetachedPackage{ 0x668 };
inline ProcPtr  LMGetCallDetachedPackage() { return LM(CallDetachedPackage); }
inline void LMSetCallDetachedPackage(ProcPtr val) { LM(CallDetachedPackage) = val; }
NOTRAP_FUNCTION2(LMGetCallDetachedPackage);
NOTRAP_FUNCTION2(LMSetCallDetachedPackage);


} /* namespace Executor */