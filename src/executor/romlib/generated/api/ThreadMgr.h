/*****************************************************************************
 *                                                                           *
 *                                ThreadMgr.h                                *
 *                                ===========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    OSErr, LONGINT, INTEGER, Boolean                        *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME ThreadMgr
#include <base/api-module.h>


namespace Executor {

// D0's low byte selects, its high byte is the parameter size in words
// (THREEWORDINLINE(0x303C, sel, 0xABF2)).
DISPATCHER_TRAP(ThreadDispatch, 0xABF2, D0<0xFF>);


enum {kReadyThreadState = 0,
kStoppedThreadState = 1,
kRunningThreadState = 2,
};

enum {kCooperativeThread = 1,
kPreemptiveThread = 2,
};

enum {kNoThreadID = 0,
kCurrentThreadID = 1,
kApplicationThreadID = 2,
};

enum {kNewSuspend = 1,
kUsePremadeThread = 2,
kCreateIfNeeded = 4,
kFPUNotNeeded = 8,
kExactMatchThread = 16,
};

enum {threadTooManyReqsErr = -617,
threadNotFoundErr = -618,
threadProtocolErr = -619,
};

OSErr C_CreateThreadPool(LONGINT threadStyle, INTEGER numToCreate, LONGINT stackSize);PASCAL_SUBTRAP(CreateThreadPool, 0xABF2, 0x0501, ThreadDispatch);


OSErr C_GetFreeThreadCount(LONGINT threadStyle, GUEST<INTEGER> *freeCount);PASCAL_SUBTRAP(GetFreeThreadCount, 0xABF2, 0x0402, ThreadDispatch);


OSErr C_NewThread(LONGINT threadStyle, ProcPtr threadEntry, LONGINT threadParam, LONGINT stackSize, LONGINT options, GUEST<LONGINT> *threadResult, GUEST<LONGINT> *threadMade);PASCAL_SUBTRAP(NewThread, 0xABF2, 0x0E03, ThreadDispatch);


OSErr C_DisposeThread(LONGINT threadToDump, LONGINT threadResult, Boolean recycleThread);PASCAL_SUBTRAP(DisposeThread, 0xABF2, 0x0504, ThreadDispatch);


// YieldToAnyThread is YieldToThread(kNoThreadID).
OSErr C_YieldToThread(LONGINT suggestedThread);PASCAL_SUBTRAP(YieldToThread, 0xABF2, 0x0205, ThreadDispatch);


OSErr C_GetCurrentThread(GUEST<LONGINT> *currentThreadID);PASCAL_SUBTRAP(GetCurrentThread, 0xABF2, 0x0206, ThreadDispatch);


OSErr C_GetThreadState(LONGINT threadToGet, GUEST<INTEGER> *threadState);PASCAL_SUBTRAP(GetThreadState, 0xABF2, 0x0407, ThreadDispatch);


OSErr C_SetThreadState(LONGINT threadToSet, INTEGER newState, LONGINT suggestedThread);PASCAL_SUBTRAP(SetThreadState, 0xABF2, 0x0508, ThreadDispatch);


OSErr C_SetThreadScheduler(ProcPtr threadScheduler);PASCAL_SUBTRAP(SetThreadScheduler, 0xABF2, 0x0209, ThreadDispatch);


OSErr C_SetThreadSwitcher(LONGINT thread, ProcPtr threadSwitcher, LONGINT switchProcParam, Boolean inOrOut);PASCAL_SUBTRAP(SetThreadSwitcher, 0xABF2, 0x070A, ThreadDispatch);


OSErr C_ThreadBeginCritical();PASCAL_SUBTRAP(ThreadBeginCritical, 0xABF2, 0x000B, ThreadDispatch);


OSErr C_ThreadEndCritical();PASCAL_SUBTRAP(ThreadEndCritical, 0xABF2, 0x000C, ThreadDispatch);


OSErr C_SetDebuggerNotificationProcs(ProcPtr notifyNewThread, ProcPtr notifyDisposeThread, ProcPtr notifyThreadScheduler);PASCAL_SUBTRAP(SetDebuggerNotificationProcs, 0xABF2, 0x060D, ThreadDispatch);


OSErr C_GetThreadCurrentTaskRef(GUEST<LONGINT> *threadTRef);PASCAL_SUBTRAP(GetThreadCurrentTaskRef, 0xABF2, 0x020E, ThreadDispatch);


OSErr C_GetThreadStateGivenTaskRef(LONGINT threadTRef, LONGINT threadToGet, GUEST<INTEGER> *threadState);PASCAL_SUBTRAP(GetThreadStateGivenTaskRef, 0xABF2, 0x060F, ThreadDispatch);


OSErr C_SetThreadReadyGivenTaskRef(LONGINT threadTRef, LONGINT threadToSet);PASCAL_SUBTRAP(SetThreadReadyGivenTaskRef, 0xABF2, 0x0410, ThreadDispatch);


OSErr C_SetThreadTerminator(LONGINT thread, ProcPtr threadTerminator, LONGINT terminationProcParam);PASCAL_SUBTRAP(SetThreadTerminator, 0xABF2, 0x0611, ThreadDispatch);


OSErr C_SetThreadStateEndCritical(LONGINT threadToSet, INTEGER newState, LONGINT suggestedThread);PASCAL_SUBTRAP(SetThreadStateEndCritical, 0xABF2, 0x0512, ThreadDispatch);


OSErr C_GetDefaultThreadStackSize(LONGINT threadStyle, GUEST<LONGINT> *stackSize);PASCAL_SUBTRAP(GetDefaultThreadStackSize, 0xABF2, 0x0413, ThreadDispatch);


OSErr C_ThreadCurrentStackSpace(LONGINT thread, GUEST<LONGINT> *freeStack);PASCAL_SUBTRAP(ThreadCurrentStackSpace, 0xABF2, 0x0414, ThreadDispatch);


OSErr C_GetSpecificFreeThreadCount(LONGINT threadStyle, LONGINT stackSize, GUEST<INTEGER> *freeCount);PASCAL_SUBTRAP(GetSpecificFreeThreadCount, 0xABF2, 0x0615, ThreadDispatch);


} /* namespace Executor */