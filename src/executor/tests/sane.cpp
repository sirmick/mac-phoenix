#include "gtest/gtest.h"
#ifdef EXECUTOR
#include <SANE.h>
#else
#include <MacTypes.h>
#endif
#include "compat.h"

#if defined(EXECUTOR) || TARGET_CPU_68K

#ifdef EXECUTOR

using namespace Executor;

static void Ff2X(GUEST<float>* sp, extended80* dp)
{
    ROMlib_Fx2X(sp, dp, FS_OPERAND);
}
static void FX2f(extended80* sp, GUEST<float>* dp)
{
    ROMlib_FX2x(sp, dp, FS_OPERAND);
}

#else

enum
{
    FX_OPERAND = 0x0000,
    FD_OPERAND = 0x0800,
    FS_OPERAND = 0x1000,
    FC_OPERAND = 0x3000,
    FI_OPERAND = 0x2000,
    FL_OPERAND = 0x2800,
};


pascal void Ff2X(float *sp, extended80 *dp) = { 0x3F3C, 0x100E, 0xA9EB };
pascal void FX2f(extended80 *sp, float *dp) = { 0x3F3C, 0x1010, 0xA9EB };
#endif

TEST(SANE, convFloat)
{
    float a = 42.0f;
    extended80 ext{};
    float b = 23.0f;

    Ff2X(PTR(a), &ext);
    FX2f(&ext, OPTR(b));

    EXPECT_EQ(42.0f, b);
}


#endif
