/* MacPhoenix: InterfaceLib's 64-bit arithmetic (FixMath.h's wide routines),
   with the answers Mac OS 8.6 gives (tests/guest/difftest/wide.c has them
   all, against the G3): rounding shifts and divides, overflow. */
#include "gtest/gtest.h"
#include "compat.h"

#include <ToolboxUtil.h>

using namespace Executor;

static wide W(int32_t hi, uint32_t lo)
{
    wide w;
    w.hi = hi;
    w.lo = lo;
    return w;
}

static int64_t V(const wide &w)
{
    return (int64_t)(((uint64_t)(uint32_t)(int32_t)w.hi << 32) | (uint32_t)w.lo);
}

TEST(WideMath, AddSubtractCompareNegateMultiply)
{
    wide a = W(0, 0xFFFFFFFF), b = W(0, 1);
    EXPECT_EQ(&a, C_WideAdd(&a, &b));
    EXPECT_EQ(0x100000000ll, V(a));
    C_WideSubtract(&a, &b);
    C_WideSubtract(&a, &b);
    EXPECT_EQ(0xFFFFFFFEll, V(a));
    EXPECT_EQ(1, C_WideCompare(&a, &b));
    EXPECT_EQ(-1, C_WideCompare(&b, &a));
    C_WideNegate(&a);
    EXPECT_EQ(-0xFFFFFFFEll, V(a));
    EXPECT_EQ(-1, C_WideCompare(&a, &b));
    C_WideMultiply(0x7FFFFFFF, (LONGINT)0x80000000, &a);
    EXPECT_EQ((int64_t)0x7FFFFFFF * -(int64_t)0x80000000, V(a));
}

TEST(WideMath, Shifts)
{
    wide a = W(0x12345678, 0x9ABCDEF0);
    C_WideShift(&a, 32); /* rounds */
    EXPECT_EQ(0x12345679ll, V(a));
    a = W(0x12345678, 0x9ABCDEF0);
    C_WideBitShift(&a, 32); /* doesn't */
    EXPECT_EQ(0x12345678ll, V(a));
    a = W(-1, 0xFFFFFFFF);
    C_WideShift(&a, 1);
    EXPECT_EQ(0, V(a));
    a = W(-1, 0xFFFFFFFF);
    C_WideShift(&a, 65); /* past 64: no rounding */
    EXPECT_EQ(-1, V(a));
    a = W(0x7FFFFFFF, 0xFFFFFFFF);
    C_WideShift(&a, 63); /* the rounding doesn't overflow */
    EXPECT_EQ(1, V(a));
    a = W(0, 0x64);
    C_WideBitShift(&a, -32);
    EXPECT_EQ(0x6400000000ll, V(a));
}

TEST(WideMath, SquareRoot)
{
    wide a = W(0, 0xFFFFFFFF);
    EXPECT_EQ(0x10000u, C_WideSquareRoot(&a)); /* rounded */
    a = W(0x3FFFFFFF, 0);
    EXPECT_EQ(0x7FFFFFFFu, C_WideSquareRoot(&a));
    a = W(-1, 0xFFFFFFFF); /* unsigned, at most 0xFFFFFFFF */
    EXPECT_EQ(0xFFFFFFFFu, C_WideSquareRoot(&a));
}

TEST(WideMath, Divide)
{
    GUEST<LONGINT> rem;
    wide a = W(1, 0);
    EXPECT_EQ(0x24924924, C_WideDivide(&a, 7, &rem)); /* truncated */
    EXPECT_EQ(4, (LONGINT)rem);
    EXPECT_EQ(0x24924925, C_WideDivide(&a, 7, nullptr)); /* rounded */
    EXPECT_EQ(0x7FFFFFFF, C_WideDivide(&a, 1, &rem)); /* overflow */
    EXPECT_EQ((LONGINT)0x80000000, (LONGINT)rem);
    a = W(-1, 0x80000000);
    EXPECT_EQ((LONGINT)0x80000000, C_WideDivide(&a, 1, &rem)); /* -2^31 overflows too */
    EXPECT_EQ((LONGINT)0x80000000, C_WideDivide(&a, 0, &rem));
}

TEST(WideMath, WideWideDivide)
{
    GUEST<LONGINT> rem;
    wide a = W(0x7FFFFFFF, 0xFFFFFFFF);
    EXPECT_EQ(&a, C_WideWideDivide(&a, 7, &rem));
    EXPECT_EQ(0x1249249249249249ll, V(a));
    EXPECT_EQ(0, (LONGINT)rem);
    a = W(0x7FFFFFFF, 0xFFFFFFFF);
    C_WideWideDivide(&a, 7, nullptr); /* rounding overflows */
    EXPECT_EQ(0x80000000ll, V(a));
    a = W(0, 5);
    C_WideWideDivide(&a, 0, &rem);
    EXPECT_EQ(0x7FFFFFFF80000000ll, V(a));
    a = W((int32_t)0x80000000, 0);
    C_WideWideDivide(&a, 3, &rem);
    EXPECT_EQ((int64_t)0xFFFFFFFF80000000ull, V(a));
}
