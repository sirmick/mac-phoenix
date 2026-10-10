#include "gtest/gtest.h"

#include <base/mactype.h>
#include <ScriptMgr.h>
#include <sane/floatconv.h>

#include <cstring>
#include <string>

using namespace Executor;

/* Number formats as AppleScript uses them to write reals. The expected
   strings are what AppleScript 1.1 shows on a real 7.5.5 (UAE): plain
   "###0.0############" for moderate magnitudes, "0.0#############e+##0"
   otherwise (AppleScript picks the format). */
class NumFormat : public testing::Test
{
protected:
    std::string fmt(const char *format, long double v)
    {
        Str255 f;
        f[0] = strlen(format);
        memcpy(f + 1, format, f[0]);
        NumFormatStringRec rec;
        NumberParts parts = {};
        EXPECT_EQ(0, StringToFormatRec(f, &parts, &rec));
        Extended80 x;
        ieee_to_x80(v, (x80_t *)&x);
        Str255 out;
        ExtendedToString(&x, &rec, &parts, out);
        return std::string((const char *)out + 1, out[0]);
    }

    const char *plain = "###0.0############;-###0.0############;0.0";
    const char *expo = "0.0#############e+##0;-0.0#############e+##0;0.0";
};

TEST_F(NumFormat, Plain)
{
    EXPECT_EQ("1.0", fmt(plain, 1.0));
    EXPECT_EQ("2.5", fmt(plain, 2.5));
    EXPECT_EQ("100.0", fmt(plain, 100.0));
    EXPECT_EQ("0.001", fmt(plain, 0.001));
    EXPECT_EQ("0.3333333333333", fmt(plain, 1.0L / 3));
    EXPECT_EQ("0.6666666666667", fmt(plain, 2.0L / 3));
    EXPECT_EQ("-2.5", fmt(plain, -2.5));
    EXPECT_EQ("0.0", fmt(plain, 0.0));
}

TEST_F(NumFormat, Exponent)
{
    EXPECT_EQ("1.0E+20", fmt(expo, 1e20));
    EXPECT_EQ("1.2345675E+6", fmt(expo, 1234567.5));
    EXPECT_EQ("1.0E-5", fmt(expo, 1e-5));
    EXPECT_EQ("1.23456789E+8", fmt(expo, 123456789.0));
    EXPECT_EQ("1.23456789012345E+13", fmt(expo, 12345678901234.5));
    EXPECT_EQ("-1.0E-10", fmt(expo, -1e-10));
}

TEST_F(NumFormat, Integers)
{
    EXPECT_EQ("42", fmt("######0;-######0;0", 42));
    EXPECT_EQ("-7", fmt("######0;-######0;0", -7));
    EXPECT_EQ("0", fmt("######0;-######0;0", 0));
    EXPECT_EQ("+5", fmt("+################0;-################0", 5));
}
