/* MacPhoenix: QuickTime's $ABC2 math (qtmath.cpp), with the answers a
   Quadra's 7.6.1 with QuickTime 2.5 gives (tests/guest/qtmath has them
   all: run_qtmath.sh compares the whole run). */
#include "gtest/gtest.h"
#include "compat.h"

#include <ImageCompression.h>
#include <QuickDraw.h>

using namespace Executor;

namespace
{
wide W(int32_t hi, uint32_t lo)
{
    wide w;
    w.hi = hi;
    w.lo = lo;
    return w;
}

uint64_t V(const wide &w)
{
    return ((uint64_t)(uint32_t)(int32_t)w.hi << 32) | (uint32_t)w.lo;
}

MatrixRecord identity()
{
    MatrixRecord m;
    C_SetIdentityMatrix(&m);
    return m;
}

std::vector<uint32_t> cells(const MatrixRecord &m)
{
    std::vector<uint32_t> v;
    for(int i = 0; i < 3; i++)
        for(int j = 0; j < 3; j++)
            v.push_back((uint32_t)(int32_t)m.matrix[i][j]);
    return v;
}
}

TEST(QTMath, CompShiftRoundsRight)
{
    wide a = W(0x123, 0x456789AB), b = W(0xFFFFFEDC, 0xBA987655);
    C_CompShift(&a, 1);
    C_CompShift(&b, 1);
    EXPECT_EQ(0x00000091A2B3C4D6ull, V(a));
    EXPECT_EQ(0xFFFFFF6E5D4C3B2Bull, V(b));
    a = W(0x123, 0x456789AB);
    C_CompShift(&a, -4);
    EXPECT_EQ(0x0000123456789AB0ull, V(a));
}

TEST(QTMath, CompDivTruncatesAndSaturates)
{
    GUEST<LONGINT> r = 0x55555555;
    wide a = W(2, 7);
    EXPECT_EQ(0x7FFFFFFF, C_CompDiv(&a, 1, &r));
    EXPECT_EQ((int32_t)0x80000000, (int32_t)r);
    EXPECT_EQ(0x4924924A, C_CompDiv(&a, 7, &r));
    EXPECT_EQ(1, (int32_t)r);
    wide b = W(0xFFFFFFFD, 0xFFFFFFF9);
    EXPECT_EQ((int32_t)0xB6DB6DB6, C_CompDiv(&b, 7, &r));
    EXPECT_EQ(-1, (int32_t)r);
}

TEST(QTMath, CompFixMulFloors)
{
    wide a = W(3, 5), b = W(0xFFFFFFFC, 0xFFFFFFFB), out;
    C_CompFixMul(&a, (Fixed)0xFFFE8000, &out);
    EXPECT_EQ(0xFFFFFFFB7FFFFFF8ull, V(out));
    C_CompFixMul(&b, (Fixed)0xFFFE8000, &out);
    EXPECT_EQ(0x0000000480000007ull, V(out));
}

TEST(QTMath, CompMulDiv)
{
    wide a = W(0, 1);
    C_CompMulDiv(&a, 3, 2);
    EXPECT_EQ(2ull, V(a));
    a = W(0, 1);
    C_CompMulDiv(&a, -3, 2);
    EXPECT_EQ(0xFFFFFFFFFFFFFFFEull, V(a));
    GUEST<LONGINT> r = 0x55555555;
    a = W(0xFFFFFFFE, 0xFFFFFFFB);
    C_CompMulDivTrunc(&a, 7, 0x12345, &r);
    EXPECT_EQ(0xFFFFFFFFFFF9D8FEull, V(a));
    EXPECT_EQ(0x3699, (int32_t)r) << "the remainder's magnitude";
    r = 0x55555555;
    a = W(1, 5);
    C_CompMulDivTrunc(&a, 7, 7, &r);
    EXPECT_EQ(0x0000000100000005ull, V(a)) << "mul == divisor: left alone";
    EXPECT_EQ(0x55555555, (int32_t)r);
}

TEST(QTMath, CompSquareRoot)
{
    wide a = W(0, 6);
    EXPECT_EQ(2u, C_CompSquareRoot(&a));
    a = W(1, 0);
    EXPECT_EQ(0x10000u, C_CompSquareRoot(&a));
    a = W(0, 0);
    EXPECT_EQ(0u, C_CompSquareRoot(&a));
}

TEST(QTMath, FixMulDiv)
{
    EXPECT_EQ(0x00030000, C_FixMulDiv(0x10000, 0x30000, 0x10000));
    EXPECT_EQ((Fixed)0xFFFE0000, C_FixMulDiv(0x10000, 0x30000, (Fixed)0xFFFE8000));
    EXPECT_EQ(0x7AE, C_FixMulDiv(0x10000, 0x30000, 0x640000)) << "rounded";
    EXPECT_EQ((Fixed)0xFFFFFFFB, C_FixMulDiv(0x10000, 0x30000, (Fixed)0x80000000)) << "half taken off";
    EXPECT_EQ((Fixed)0x80000003, C_FixMulDiv(0x10000, 0x30000, 3)) << "overflow";
    EXPECT_EQ((Fixed)0x8000012B, C_FixMulDiv((Fixed)0xFF9C0000, 0x30000, (Fixed)0xFFFFFFFF));
    EXPECT_EQ((Fixed)0x80030000, C_FixMulDiv(0x10000, 0x30000, 0)) << "by zero";
    EXPECT_EQ((Fixed)0x8002FFFB, C_UnsignedFixMulDiv((Fixed)0xFFFE8000, 0x30000, 0x10000));
    EXPECT_EQ(0x00030000, C_UnsignedFixMulDiv((Fixed)0xFFFE8000, 0x30000, (Fixed)0xFFFE8000));
    EXPECT_EQ(0x0002FFFB, C_UnsignedFixMulDiv((Fixed)0xFFFE8000, 0x30000, (Fixed)0xFFFFFFFF));
    EXPECT_EQ(0x0005FFF6, C_UnsignedFixMulDiv((Fixed)0xFFFE8000, 0x30000, (Fixed)0x80000000));
}

TEST(QTMath, Matrices)
{
    MatrixRecord m = identity();
    C_TranslateMatrix(&m, 0x58000, (Fixed)0xFFFD0000);
    C_ScaleMatrix(&m, 0x20000, 0x8000, 0x10000, 0x30000);
    EXPECT_EQ((std::vector<uint32_t>{ 0x20000, 0, 0, 0, 0x8000, 0, 0xA0000, 0, 0x40000000 }), cells(m));
    MatrixRecord inv;
    EXPECT_TRUE(C_InverseMatrix(&m, &inv));
    EXPECT_EQ((std::vector<uint32_t>{ 0x8000, 0, 0, 0, 0x20000, 0, 0xFFFB0000, 0, 0x40000000 }), cells(inv));

    m = identity();
    C_RotateMatrix(&m, 0x1E0000, 0, 0);
    EXPECT_EQ((std::vector<uint32_t>{ 0xDDB4, 0x8000, 0, 0xFFFF8000, 0xDDB4, 0, 0, 0, 0x40000000 }), cells(m));
    m = identity();
    C_SkewMatrix(&m, 0x8000, 0x4000, 0x20000, 0x10000);
    EXPECT_EQ((std::vector<uint32_t>{ 0x10000, 0x4000, 0, 0x8000, 0x10000, 0, 0xFFFF8000, 0xFFFF8000, 0x40000000 }),
              cells(m));

    Rect r1 = { 0, 0, 120, 160 }, r2 = { 20, 10, 260, 90 }; /* top, left, bottom, right */
    C_RectMatrix(&m, &r1, &r2);
    EXPECT_EQ((std::vector<uint32_t>{ 0x8000, 0, 0, 0, 0x20000, 0, 0xA0000, 0x140000, 0x40000000 }), cells(m));
    m = identity();
    C_TranslateMatrix(&m, 0x30000, 0x40000);
    C_MapMatrix(&m, &r1, &r2);
    EXPECT_EQ((std::vector<uint32_t>{ 0x8000, 0, 0, 0, 0x20000, 0, 0xB8000, 0x1C0000, 0x40000000 }), cells(m));

    /* b, then a */
    MatrixRecord a = identity(), b = identity();
    C_ScaleMatrix(&b, 0x20000, 0x20000, 0, 0);
    C_CopyMatrix(&b, &a);
    C_TranslateMatrix(&a, 0x10000, 0x10000);
    C_ConcatMatrix(&a, &b);
    EXPECT_EQ((std::vector<uint32_t>{ 0x40000, 0, 0, 0, 0x40000, 0, 0x10000, 0x10000, 0x40000000 }), cells(b));
    EXPECT_TRUE(C_EqualMatrix(&a, &a));
    EXPECT_FALSE(C_EqualMatrix(&a, &b));
}

TEST(QTMath, Transforms)
{
    MatrixRecord m = identity();
    C_TranslateMatrix(&m, 0x58000, (Fixed)0xFFFD0000);
    GUEST<Point> p[3];
    p[0] = Point{ 5, 3 };
    p[1] = Point{ 1, -8 };
    p[2] = Point{ -7, 100 };
    EXPECT_EQ(noErr, C_TransformPoints(&m, p, 3));
    EXPECT_EQ(9, p[0].get().h);
    EXPECT_EQ(2, p[0].get().v);
    EXPECT_EQ(-3, p[1].get().h) << "halves away from zero";
    EXPECT_EQ(106, p[2].get().h);

    /* a quarter turn: corners mapped, but not a rectangle any more */
    m = identity();
    C_RotateMatrix(&m, 0x5A0000, 0xA0000, 0x140000);
    FixedRect fr;
    fr.left = 0x10000;
    fr.top = 0x20000;
    fr.right = 0xA8000;
    fr.bottom = 0x140000;
    FixedPoint c[4];
    EXPECT_FALSE(C_TransformFixedRect(&m, &fr, c));
    EXPECT_EQ(0xA0000, (int32_t)fr.left);
    EXPECT_EQ(0xB0000, (int32_t)fr.top);
    EXPECT_EQ(0x1C0000, (int32_t)fr.right);
    EXPECT_EQ(0x148000, (int32_t)fr.bottom);
    EXPECT_EQ(0x1C0000, (int32_t)c[0].x);
    EXPECT_EQ(0xB0000, (int32_t)c[0].y);

    /* perspective: refused, points left alone */
    m = identity();
    m.matrix[0][2] = 0x400000;
    FixedPoint fp;
    fp.x = 0x30000;
    fp.y = 0x50000;
    EXPECT_EQ(-1, C_TransformFixedPoints(&m, &fp, 1));
    EXPECT_EQ(0x30000, (int32_t)fp.x);
    EXPECT_EQ(perspectiveMatrixType, C_GetMatrixType(&m));

    /* singular scale: saturated */
    MatrixRecord z = {}, inv;
    z.matrix[2][2] = 0x40000000;
    EXPECT_TRUE(C_InverseMatrix(&z, &inv));
    EXPECT_EQ((std::vector<uint32_t>{ 0x7FFFFFFF, 0, 0, 0, 0x7FFFFFFF, 0, 0, 0, 0x40000000 }), cells(inv));
}
