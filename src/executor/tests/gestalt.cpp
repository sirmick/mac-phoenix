/* MacPhoenix: Gestalt asked with no response pointer, as PowerPC callers
   may (REALbasic's runtime asks Gestalt('qtim', nil): GameBox Classic). A
   Mac writes the answer at 0, harmless there; Executor drops it. */
#include "gtest/gtest.h"

#include <Gestalt.h>

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
