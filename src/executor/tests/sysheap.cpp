#include "gtest/gtest.h"

#include <MemoryMgr.h>
#include <mman/mman.h>

using namespace Executor;

/* The System heap grows on demand into the reserve above it: the zone's
   grow-zone procedure takes what an allocation lacks, and the extension
   loader takes an extension's 'sysz' the same way. */
TEST(SysHeap, GrowsOnDemand)
{
    int32_t before = FreeMemSys();
    ASSERT_GT(ROMlib_syszone_reserve_left(), 256 * 1024);
    Size big = before + 256 * 1024;
    Handle h = NewHandleSys(big);
    ASSERT_NE(nullptr, h);
    EXPECT_EQ(noErr, MemError());
    EXPECT_EQ(big, GetHandleSize(h));
    DisposeHandle(h);
    EXPECT_GE(FreeMemSys(), big); /* the heap kept its growth */
}

TEST(SysHeap, GrowthIsBounded)
{
    int32_t left = ROMlib_syszone_reserve_left();
    Handle h = NewHandleSys(FreeMemSys() + left + 1024 * 1024);
    EXPECT_EQ(nullptr, h);
    EXPECT_EQ(memFullErr, MemError());
    /* An explicit growth takes 64K steps and no more than the reserve. */
    int32_t got = ROMlib_grow_syszone(1);
    EXPECT_EQ(got, left >= 64 * 1024 ? 64 * 1024 : 0);
    EXPECT_EQ(ROMlib_syszone_reserve_left(), left - got);
}
