#include "gtest/gtest.h"

#include "compat.h"

#ifdef EXECUTOR
#include <SegmentLdr.h>
#include <base/common.h>
#include <LowMem.h>
#else
#include <SegLoad.h>
#include <LowMem.h>
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
