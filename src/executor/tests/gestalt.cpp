/* MacPhoenix: Gestalt asked with no response pointer, as PowerPC callers
   may (REALbasic's runtime asks Gestalt('qtim', nil): GameBox Classic). A
   Mac writes the answer at 0, harmless there; Executor drops it. */
#include "gtest/gtest.h"

#include <Gestalt.h>
#include <OSUtil.h>
#include <rsys/gestalt.h>
#include <prefs/prefs.h>

using namespace Executor;

TEST(Gestalt, NilResponseIsAnswered)
{
    GUEST<LONGINT> sysv;
    ASSERT_EQ(noErr, C_Gestalt(gestaltSystemVersion, &sysv));
    EXPECT_EQ(noErr, C_Gestalt(gestaltSystemVersion, nullptr));
    OSErr qtim = C_Gestalt("qtim"_4, nullptr);
    EXPECT_TRUE(qtim == noErr || qtim == gestaltUndefSelectorErr) << qtim;
    EXPECT_EQ(gestaltUndefSelectorErr, C_Gestalt("~~~~"_4, nullptr));
}

/* A Quadra 650's answers (a probe on the ROM machine's 7.6.1): the
   68040's FPU, which the core emulates, and the Edition Manager, present
   and translation-aware. Photoshop 4.0 refuses to start ("lacks the
   required hardware or system software") without the Edition Manager. */
TEST(Gestalt, QuadraFPUAndEditionManager)
{
    GUEST<LONGINT> v;
    ASSERT_EQ(noErr, C_Gestalt(gestaltFPUType, &v));
    EXPECT_EQ(3, (LONGINT)v) << "gestalt68040FPU";
    GUEST<LONGINT> sysv;
    ASSERT_EQ(noErr, C_Gestalt(gestaltSystemVersion, &sysv));
    if(sysv >= 0x700)
    {
        ASSERT_EQ(noErr, C_Gestalt(gestaltEditionMgrAttr, &v));
        EXPECT_EQ(3, (LONGINT)v);
    }
    SysEnvRec env;
    ASSERT_EQ(noErr, SysEnvirons(2, &env));
    EXPECT_TRUE(env.hasFPU);
}

/* The addressing mode is the three flags' bits (7, as the Quadra and the
   G3 answer: 32-bit capable), the page size the Quadra's 4K. */
TEST(Gestalt, AddressingModeAndPageSize)
{
    GUEST<LONGINT> v;
    ASSERT_EQ(noErr, C_Gestalt(gestaltAddressingModeAttr, &v));
    EXPECT_EQ(7, (LONGINT)v);
    ASSERT_EQ(noErr, C_Gestalt(gestaltLogicalPageSize, &v));
    EXPECT_EQ(4096, (LONGINT)v);
}

/* Managers Executor has, answered as the Quadra answers them on 7.6.1
   (tests/guest/gestalt): the pop-up CDEF, the Icon Utilities, the
   Notification and Resource Managers, the machine icon, Gestalt 6; and the
   keyboard KbdType says, an ISO Apple Extended (9), in SysEnvirons too. */
TEST(Gestalt, QuadraManagersAndKeyboard)
{
    GUEST<LONGINT> sysv, v;
    ASSERT_EQ(noErr, C_Gestalt(gestaltSystemVersion, &sysv));
    ASSERT_EQ(noErr, C_Gestalt(gestaltPopupAttr, &v));
    EXPECT_EQ(1, (LONGINT)v);
    ASSERT_EQ(noErr, C_Gestalt(gestaltMachineIcon, &v));
    EXPECT_EQ(-16396, (LONGINT)v);
    ASSERT_EQ(noErr, C_Gestalt(gestaltKeyboardType, &v));
    EXPECT_EQ(9, (LONGINT)v);
    SysEnvRec env;
    ASSERT_EQ(noErr, SysEnvirons(2, &env));
    EXPECT_EQ(9, env.keyBoardType);
    if(sysv >= 0x700)
    {
        ASSERT_EQ(noErr, C_Gestalt("nmgr"_4, &v));
        EXPECT_EQ(1, (LONGINT)v);
        ASSERT_EQ(noErr, C_Gestalt("rsrc"_4, &v));
        EXPECT_EQ(3, (LONGINT)v);
        ASSERT_EQ(noErr, C_Gestalt("icon"_4, &v));
        EXPECT_EQ(sysv >= 0x850 ? 0x1F : 1, (LONGINT)v);
        ASSERT_EQ(noErr, C_Gestalt(gestaltVersion, &v));
        EXPECT_EQ(sysv >= 0x800 ? 7 : 6, (LONGINT)v);
    }
}

/* The System's version sets the machine both ways: an 8.x System makes it
   the G3, a 7.x one (no --executor-ppc) the Quadra again, nothing of the
   G3 left behind. */
TEST(Gestalt, SystemVersionSetsTheMachineBothWays)
{
    GUEST<LONGINT> sysv, v;
    ASSERT_EQ(noErr, C_Gestalt(gestaltSystemVersion, &sysv));
    bool prefer_ppc = ROMlib_prefer_ppc;
    ROMlib_prefer_ppc = false;
    auto get = [&](OSType sel) {
        GUEST<LONGINT> r;
        OSErr err = C_Gestalt(sel, &r);
        return err ? (LONGINT)err : (LONGINT)r;
    };
    gestalt_set_system_version(0x0860);
    EXPECT_EQ(67, get(gestaltMachineType));
    EXPECT_EQ(2, get(gestaltSysArchitecture));
    EXPECT_EQ(0, get(gestaltFPUType));
    EXPECT_EQ(100000000, get("pclk"_4));
    EXPECT_EQ(7, get(gestaltVersion));
    gestalt_set_system_version(0x0761);
    EXPECT_EQ(20, get(gestaltMachineType));
    EXPECT_EQ(1, get(gestaltSysArchitecture));
    EXPECT_EQ(5, get(gestaltProcessorType));
    EXPECT_EQ(3, get(gestaltFPUType));
    EXPECT_EQ(0, get(gestaltMMUType));
    EXPECT_EQ(3, get("thds"_4));
    EXPECT_EQ(gestaltUndefSelectorErr, get("pclk"_4));
    EXPECT_EQ(6, get(gestaltVersion));
    EXPECT_EQ(1, get(gestaltAppleEventsAttr));
    EXPECT_EQ(0x0750, get(gestaltScriptMgrVersion));
    EXPECT_EQ(0, get(gestaltFSAttr) & (1 << 8));
    ROMlib_prefer_ppc = prefer_ppc;
    gestalt_set_system_version((LONGINT)sysv);
}

/* One script system (Roman), as 7.6.1's Quadra and 8.6's G3 answer
   'scr#': Canvas 5 can't initialize without it (-5551) */
TEST(Gestalt, ScriptCount)
{
    GUEST<LONGINT> v;
    ASSERT_EQ(noErr, C_Gestalt(gestaltScriptCount, &v));
    EXPECT_EQ(1, (LONGINT)v);
}
