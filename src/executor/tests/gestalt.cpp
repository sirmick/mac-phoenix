/* MacPhoenix: Gestalt asked with no response pointer, as PowerPC callers
   may (REALbasic's runtime asks Gestalt('qtim', nil): GameBox Classic). A
   Mac writes the answer at 0, harmless there; Executor drops it. */
#include "gtest/gtest.h"

#include <Gestalt.h>
#include <OSUtil.h>

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
