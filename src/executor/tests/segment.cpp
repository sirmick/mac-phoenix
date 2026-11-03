#include "gtest/gtest.h"

#include "compat.h"

#ifdef EXECUTOR
#include <SegmentLdr.h>
#include <base/common.h>
#include <LowMem.h>
#include <MemoryMgr.h>
#include <OSUtil.h>
#include <base/cpu.h>
#else
#include <SegLoad.h>
#include <LowMem.h>
#include <Memory.h>
#include <OSUtils.h>
#include <Traps.h>
#endif

// GetAppParms with a nil output carries on: a 7.6.1 Quadra (2026-11-03)
// puts the application parameter handle in the long at address 0 when the
// third argument is nil (Realmz 2.5 passes nil there); Executor skips the
// store, its address 0 being nilhandle, kept 0.
TEST(SegmentLoader, GetAppParmsNilParamHandle)
{
    Str255 name;
    GUEST<int16_t> refNum = -1;
#ifndef EXECUTOR
    volatile long *zero;
    asm("" : "=r"(zero) : "0"(0L)); // address 0, hidden from the optimizer
    long saved = *zero;
    *zero = 0x5A5A5A5A;
#else
    /* A non-nil handle, so a store at 0 would show */
    Handle savedParms = LM(AppParmHandle);
    Handle h = NewHandle(4);
    LM(AppParmHandle) = h;
#endif
    GetAppParms(name, &refNum, nullptr);
#ifdef EXECUTOR
    LM(AppParmHandle) = savedParms;
    DisposeHandle(h);
    EXPECT_EQ(LM(CurApRefNum), refNum);
    EXPECT_EQ(nullptr, LM(nilhandle)); /* address 0 stays nil */
#else
    EXPECT_EQ(LMGetCurApRefNum(), refNum);
    long stored = *zero;
    *zero = saved;
    /* The Mac stores the handle at 0; Executor leaves the long there
       alone (it is nilhandle, kept 0). */
    EXPECT_TRUE(stored == (long)LMGetAppParmHandle() || stored == 0x5A5A5A5A) << stored;
#endif
    EXPECT_NE(0, name[0]);
}

#ifdef EXECUTOR
#include <ProcessMgr.h>
#else
#include <Processes.h>
#endif

/* The partition an application gets is its SIZE -1's (the user's, as Get
   Info sets it) when it has one, not its SIZE 0's: this app's SIZE -1 asks
   for 4 MB, its SIZE 0 for 3 MB (size.r). A 7.6.1 Quadra: 3 MB, SIZE 0's,
   when launched by LaunchApplication (Speedometer 4.0 gets its SIZE 0's
   1.5 MB, not its SIZE -1's 2 MB). */
TEST(Process, PartitionFromSizeZero)
{
#ifdef EXECUTOR
    GTEST_SKIP() << "the native gtest process is not a launched application";
#endif
    ProcessSerialNumber psn = { 0, kCurrentProcess };
    ProcessInfoRec info;
    memset(&info, 0, sizeof info);
    info.processInfoLength = sizeof info;
    ASSERT_EQ(noErr, GetProcessInformation(&psn, &info));
    EXPECT_GE((long)info.processSize, 3072L * 1024) << (long)info.processSize;
    EXPECT_LT((long)info.processSize, 3072L * 1024 + 256 * 1024) << (long)info.processSize;
}

// _DeferUserFn ($A08F) is there on a 7.x Quadra (7.5.5's trap table: not
// _Unimplemented, with virtual memory off). MPW's far-model runtime
// (Microsoft Works 4.0b) reads an unimplemented $A08F as "no Process
// Manager" and takes its segment loader out before a _Launch that, with
// launchContinue, comes back: its far jump table entries then reached the
// System's _LoadSeg.
TEST(SegmentLoader, DeferUserFnPresent)
{
#ifdef EXECUTOR
    EXPECT_NE(NGetTrapAddress(_Unimplemented, kToolboxTrapType), NGetTrapAddress(0xA08F, kOSTrapType));
#else
    EXPECT_NE(NGetTrapAddress(_Unimplemented, ToolTrap), NGetTrapAddress(0xA08F, OSTrap));
#endif
}

// Without virtual memory DeferUserFn calls the function at once, its
// argument (here the routine's own address) in A0: MOVE.L A0,(abs).L; RTS.
TEST(SegmentLoader, DeferUserFnCallsAtOnce)
{
    Ptr code = NewPtr(16);
    Ptr seen = NewPtrClear(4);
    ASSERT_NE(nullptr, code);
    ASSERT_NE(nullptr, seen);
#ifdef EXECUTOR
    uint32_t target = US_TO_SYN68K(seen);
#else
    uint32_t target = (uint32_t)seen;
#endif
    uint8_t routine[] = { 0x23, 0xC8, uint8_t(target >> 24), uint8_t(target >> 16),
                          uint8_t(target >> 8), uint8_t(target), 0x4E, 0x75 };
    BlockMoveData(routine, code, sizeof routine);
#ifdef EXECUTOR
    EXPECT_EQ(noErr, DeferUserFn(code, code));
    EXPECT_EQ(US_TO_SYN68K(code), (uint32_t) * (GUEST<uint32_t> *)seen);
#else
    FlushInstructionCache();
    EXPECT_EQ(noErr, DeferUserFn((UserFnUPP)code, code));
    EXPECT_EQ((uint32_t)code, *(uint32_t *)seen);
#endif
    DisposePtr(seen);
    DisposePtr(code);
}
