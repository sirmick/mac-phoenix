#include "gtest/gtest.h"

#include "compat.h"

#ifdef EXECUTOR
#include <SegmentLdr.h>
#include <base/common.h>
#else
#include <SegLoad.h>
#include <LowMem.h>
#endif

// GetAppParms stores its outputs where it is told, nil included: a 7.6.1
// Quadra (2026-11-03) puts the application parameter handle in the long at
// address 0 when the third argument is nil (Realmz 2.5 passes nil there).
TEST(SegmentLoader, GetAppParmsNilParamHandle)
{
    Str255 name;
    GUEST<int16_t> refNum = -1;
#ifndef EXECUTOR
    volatile long *zero;
    asm("" : "=r"(zero) : "0"(0L)); // address 0, hidden from the optimizer
    long saved = *zero;
    *zero = 0x5A5A5A5A;
#endif
    GetAppParms(name, &refNum, nullptr);
#ifdef EXECUTOR
    EXPECT_EQ(LM(CurApRefNum), refNum);
#else
    EXPECT_EQ(LMGetCurApRefNum(), refNum);
    long stored = *zero;
    *zero = saved;
    EXPECT_EQ((long)LMGetAppParmHandle(), stored);
#endif
    EXPECT_NE(0, name[0]);
}
