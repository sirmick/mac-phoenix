/*****************************************************************************
 *                                                                           *
 *                                 ScrapMgr.h                                *
 *                                 ==========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    LONGINT, Handle, INTEGER, StringPtr, ResType, Ptr       *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME ScrapMgr
#include <base/api-module.h>


namespace Executor {

enum {noScrapErr = -100,
noTypeErr = -102,
};

struct ScrapStuff{GUEST_STRUCT;GUEST<LONGINT> scrapSize;GUEST<Handle> scrapHandle;GUEST<INTEGER> scrapCount;GUEST<INTEGER> scrapState;GUEST<StringPtr> scrapName;};static_assert(sizeof(ScrapStuff) == 16);void describeStruct(const ScrapStuff&, std::ostream&);

typedef ScrapStuff *PScrapStuff;

// ScrapMgr IMI-457 (true);
const LowMemGlobal<LONGINT > ScrapSize{ 0x960 };
inline LONGINT  LMGetScrapSize() { return LM(ScrapSize); }
inline void LMSetScrapSize(LONGINT val) { LM(ScrapSize) = val; }
NOTRAP_FUNCTION2(LMGetScrapSize);
NOTRAP_FUNCTION2(LMSetScrapSize);


// ScrapMgr IMI-457 (true);
const LowMemGlobal<Handle > ScrapHandle{ 0x964 };
inline Handle  LMGetScrapHandle() { return LM(ScrapHandle); }
inline void LMSetScrapHandle(Handle val) { LM(ScrapHandle) = val; }
NOTRAP_FUNCTION2(LMGetScrapHandle);
NOTRAP_FUNCTION2(LMSetScrapHandle);


// ScrapMgr IMI-457 (true);
const LowMemGlobal<INTEGER > ScrapCount{ 0x968 };
inline INTEGER  LMGetScrapCount() { return LM(ScrapCount); }
inline void LMSetScrapCount(INTEGER val) { LM(ScrapCount) = val; }
NOTRAP_FUNCTION2(LMGetScrapCount);
NOTRAP_FUNCTION2(LMSetScrapCount);


// ScrapMgr IMI-457 (true);
const LowMemGlobal<INTEGER > ScrapState{ 0x96A };
inline INTEGER  LMGetScrapState() { return LM(ScrapState); }
inline void LMSetScrapState(INTEGER val) { LM(ScrapState) = val; }
NOTRAP_FUNCTION2(LMGetScrapState);
NOTRAP_FUNCTION2(LMSetScrapState);


// ScrapMgr IMI-457 (true);
const LowMemGlobal<StringPtr > ScrapName{ 0x96C };
inline StringPtr  LMGetScrapName() { return LM(ScrapName); }
inline void LMSetScrapName(StringPtr val) { LM(ScrapName) = val; }
NOTRAP_FUNCTION2(LMGetScrapName);
NOTRAP_FUNCTION2(LMSetScrapName);


PScrapStuff C_InfoScrap();PASCAL_TRAP(InfoScrap, 0xA9F9);


LONGINT C_UnloadScrap();PASCAL_TRAP(UnloadScrap, 0xA9FA);


LONGINT C_LoadScrap();PASCAL_TRAP(LoadScrap, 0xA9FB);


LONGINT C_ZeroScrap();PASCAL_TRAP(ZeroScrap, 0xA9FC);


LONGINT C_PutScrap(LONGINT len, ResType rest, Ptr p);PASCAL_TRAP(PutScrap, 0xA9FE);


LONGINT C_GetScrap(Handle h, ResType rest, GUEST<LONGINT> *off);PASCAL_TRAP(GetScrap, 0xA9FD);


} /* namespace Executor */