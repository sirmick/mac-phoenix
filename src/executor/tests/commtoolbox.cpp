#include "gtest/gtest.h"

#include "compat.h"

// The Communications Toolbox's dispatcher, as a 68k program sees it
// (the calls go through _CommToolboxDispatch with A0 pointing at the
// selector: MPW's Interface.o glue). A 7.6.1 Quadra (2026-11-03): the trap
// is there for every program, and each manager's Init answers noErr.
#ifndef EXECUTOR
#include <Traps.h>
#include <OSUtils.h>

static short ctbDispatch(short selector)
{
    short result;
    asm volatile("move.w %1,-(%%sp)\n\t"
                 "movea.l %%sp,%%a0\n\t"
                 ".short 0xA08B\n\t"
                 "addq.l #2,%%sp\n\t"
                 "move.w %%d0,%0"
                 : "=d"(result)
                 : "d"(selector)
                 : "d0", "d1", "d2", "a0", "a1", "cc", "memory");
    return result;
}

TEST(CommToolbox, TrapPresent)
{
    EXPECT_NE(NGetTrapAddress(_Unimplemented, ToolTrap), NGetTrapAddress(0xA08B, OSTrap));
}

TEST(CommToolbox, InitCalls)
{
    EXPECT_EQ(noErr, ctbDispatch(0x0401)); // InitCTBUtilities
    EXPECT_EQ(noErr, ctbDispatch(0x0501)); // InitCRM
    EXPECT_EQ(noErr, ctbDispatch(0x0101)); // InitCM
    EXPECT_EQ(noErr, ctbDispatch(0x0301)); // InitTM
    EXPECT_EQ(noErr, ctbDispatch(0x0201)); // InitFT
}
#endif
