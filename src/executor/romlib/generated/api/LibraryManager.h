/*****************************************************************************
 *                                                                           *
 *                              LibraryManager.h                             *
 *                              ================                             *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    LONGINT, OSErr, Boolean                                 *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME LibraryManager
#include <base/api-module.h>


namespace Executor {

#define kASLMNotFoundErr -3120

#define kASLMOutOfMemoryErr -3126

#define kASLMLibraryManagerNotLoadedErr -3132

typedef void *GlobalWorld;

// A client's manager (LibraryManager.h's class); opaque here.
struct TLibraryManager;

// An exception handler on the process's stack (PushException, TRY).
// PowerPC's: fBuffer is SetupException's save of r0-r31 and LR (68k
// code has 12 longs there).
struct TException{GUEST_STRUCT;GUEST<TException*> fPrev;GUEST<uint32_t> fReserved;GUEST<LONGINT> fBuffer[33];GUEST<char*> fMessage;GUEST<void*> fPtr;GUEST<OSErr> fError;};static_assert(sizeof(TException) == 150);void describeStruct(const TException&, std::ostream&);

void PushException(TException *e);ASLM_FUNCTION("!$fset", 19, PushException, PushException);


TException* PopException(TException *e);ASLM_FUNCTION("!$fset", 20, PopException, PopException);


ASLM_RAW("!$fset", 21, Fail);


Boolean MatchException(TException *e, LONGINT err);ASLM_FUNCTION("!$fset", 22, MatchException, MatchException);


TLibraryManager* GetCurrentClient();ASLM_FUNCTION("!$fset", 29, GetCurrentClient, GetCurrentClient);


GlobalWorld ASLMGetCurrentGlobalWorld();ASLM_FUNCTION("!$fset", 49, ASLMGetCurrentGlobalWorld, ASLMGetCurrentGlobalWorld);


GlobalWorld ASLMSetCurrentGlobalWorld(GlobalWorld world);ASLM_FUNCTION("!$fset", 50, ASLMSetCurrentGlobalWorld, ASLMSetCurrentGlobalWorld);


} /* namespace Executor */