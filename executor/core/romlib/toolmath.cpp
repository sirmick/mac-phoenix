/* Copyright 1986, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ToolboxUtil.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <ToolboxUtil.h>
#include <float.h>
#include <math.h>

using namespace Executor;

Fract Executor::C_FracSqrt(Fract x) /* IMIV-64 */
{
    Extended z;

    z = sqrt(Frac2X(x));
    return X2Frac(&z);
}

#define POVER4 (ULONGINT)0x0000C910 /* IMIV-64 */

Fract Executor::C_FracSin(Fixed x) /* IMIV-64 */
{
    Extended z;
    LONGINT oct;

    if(x < 0)
        x += (-(x + 1) / (POVER4 * 8) + 1) * POVER4 * 8;
    oct = x / POVER4 % 8;
    x %= POVER4 * 4;
    switch(oct)
    {
        case 0:
            z = sin(Fix2X(x));
            break;
        case 1:
            z = cos(Fix2X(POVER4 * 2 - x));
            break;
        case 2:
            z = cos(Fix2X(x - POVER4 * 2));
            break;
        case 3:
            z = sin(Fix2X(POVER4 * 4 - x));
            break;
        case 4:
            z = -sin(Fix2X(x));
            break;
        case 5:
            z = -cos(Fix2X(POVER4 * 2 - x));
            break;
        case 6:
            z = -cos(Fix2X(x - POVER4 * 2));
            break;
        case 7:
            z = -sin(Fix2X(POVER4 * 4 - x));
            break;
    }
    return X2Frac(&z);
}

Fixed Executor::C_FixATan2(LONGINT x, LONGINT y) /* IMIV-65 */
{
    Extended z;

    if(y == 0)
    {
        if(x & 0x80000000)
            /*-->*/ return x & 0x7FFFFFFF ? 205888 : 234019; /* trial and error */
        /*-->*/ return 0;
    }
    z = atan2((double)y, (double)x);
    return X2Fix(&z);
}

Fract Executor::C_FracCos(Fixed x) /* IMIV-64 */
{
    return FracSin(x + POVER4 * 2); /* could produce overflow ! */
}

/* MacPhoenix: InterfaceLib's 64-bit arithmetic (FixMath.h), with 8.6's
   answers (tests/guest/difftest/wide.c). Add, subtract, compare, negate and
   multiply are exact (modulo 2^64). */
namespace
{
int64_t wide_get(const wide *w)
{
    return (int64_t)(((uint64_t)(uint32_t)(int32_t)w->hi << 32) | (uint32_t)w->lo);
}

void wide_set(wide *w, int64_t v)
{
    w->hi = (int32_t)((uint64_t)v >> 32);
    w->lo = (uint32_t)v;
}

/* Left by -shift (0 from 64 on), or arithmetically right by shift (the
   sign from 64 on). */
int64_t bit_shift(int64_t v, int32_t shift)
{
    if(shift <= 0)
        return shift <= -64 ? 0 : (int64_t)((uint64_t)v << -shift);
    return shift >= 64 ? (v < 0 ? -1 : 0) : v >> shift;
}

/* The quotient's sign as 8.6's divides decide it on overflow. */
bool negative_quotient(int64_t dividend, int32_t divisor)
{
    return divisor ? ((dividend < 0) != (divisor < 0)) : dividend < 0;
}

/* |dividend| / |divisor|, signed back, rounded to nearest (halves away from
   0) when round; false when |dividend| (plus half the divisor, rounding)
   doesn't fit 63 bits, as 8.6 overflows then. */
bool divide(int64_t dividend, int32_t divisor, bool round, int64_t &q, int64_t &r)
{
    if(dividend == INT64_MIN)
        return false;
    uint64_t a = dividend < 0 ? -(uint64_t)dividend : (uint64_t)dividend;
    uint64_t d = divisor < 0 ? -(uint64_t)(int64_t)divisor : (uint64_t)divisor;
    if(round)
        a += d / 2;
    if(a > (uint64_t)INT64_MAX)
        return false;
    uint64_t uq = a / d, ur = a % d;
    q = negative_quotient(dividend, divisor) ? -(int64_t)uq : (int64_t)uq;
    r = dividend < 0 ? -(int64_t)ur : (int64_t)ur;
    return true;
}
}

wide *Executor::C_WideAdd(wide *target, const wide *source)
{
    wide_set(target, (int64_t)((uint64_t)wide_get(target) + (uint64_t)wide_get(source)));
    return target;
}

wide *Executor::C_WideSubtract(wide *target, const wide *source)
{
    wide_set(target, (int64_t)((uint64_t)wide_get(target) - (uint64_t)wide_get(source)));
    return target;
}

short Executor::C_WideCompare(const wide *target, const wide *source)
{
    int64_t a = wide_get(target), b = wide_get(source);
    return a < b ? -1 : a > b;
}

wide *Executor::C_WideNegate(wide *target)
{
    wide_set(target, (int64_t)-(uint64_t)wide_get(target));
    return target;
}

/* As WideBitShift, but a right shift rounds (half a unit added, without
   overflow, up to 64 places); from 65 places on it doesn't. */
wide *Executor::C_WideShift(wide *target, LONGINT shift)
{
    int64_t v = wide_get(target);
    if(shift <= 0 || shift > 64)
        v = bit_shift(v, shift);
    else
    {
        __int128 sum = (__int128)v + ((__int128)1 << (shift - 1));
        v = (int64_t)(sum >> shift);
    }
    wide_set(target, v);
    return target;
}

wide *Executor::C_WideBitShift(wide *src, LONGINT shift)
{
    wide_set(src, bit_shift(wide_get(src), shift));
    return src;
}

/* The source unsigned, the root rounded to nearest, 0xFFFFFFFF at most. */
ULONGINT Executor::C_WideSquareRoot(const wide *source)
{
    uint64_t x = (uint64_t)wide_get(source);
    uint64_t r = (uint64_t)sqrtl((long double)x);
    while(r * r > x)
        r--;
    while((r + 1) * (r + 1) <= x && r + 1 <= 0xFFFFFFFFull)
        r++;
    if(x - r * r > r)
        r++;
    return r > 0xFFFFFFFFull ? 0xFFFFFFFF : (ULONGINT)r;
}

wide *Executor::C_WideMultiply(LONGINT multiplicand, LONGINT multiplier, wide *target)
{
    wide_set(target, (int64_t)multiplicand * (int64_t)multiplier);
    return target;
}

/* Truncated, the remainder with the dividend's sign; with no remainder
   wanted (nil, or -1) the quotient is rounded. A quotient past 31 bits, or
   a divisor of 0: 0x7FFFFFFF or 0x80000000 by the quotient's sign (always
   0x80000000 with -1 for the remainder), and a remainder of 0x80000000. */
LONGINT Executor::C_WideDivide(const wide *dividend, LONGINT divisor, GUEST<LONGINT> *remainder)
{
    /* -1 arrives as -1 (SYN68K_TO_US_CHECK0_CHECKNEG1) */
    bool want = remainder && remainder != (GUEST<LONGINT> *)-1;
    int64_t a = wide_get(dividend), q = 0, r = 0;
    bool ok = divisor && divide(a, divisor, !want, q, r)
              && q >= -(int64_t)INT32_MAX && q <= INT32_MAX;
    if(!ok)
    {
        if(remainder && !want)
            return (LONGINT)0x80000000;
        if(want)
            *remainder = (LONGINT)0x80000000;
        return negative_quotient(a, divisor) ? (LONGINT)0x80000000 : 0x7FFFFFFF;
    }
    if(want)
        *remainder = (LONGINT)r;
    return (LONGINT)q;
}

/* The 64-bit quotient in the dividend, rounded with no remainder wanted.
   Overflow (|dividend| past 63 bits): 0x80000000 sign-extended by the
   quotient's sign; by 0: 0x7FFFFFFF80000000 or 0x8000000080000000 by the
   dividend's; the remainder 0x80000000 for both. */
wide *Executor::C_WideWideDivide(wide *dividend, LONGINT divisor, GUEST<LONGINT> *remainder)
{
    int64_t a = wide_get(dividend), q, r;
    if(divisor && divide(a, divisor, !remainder, q, r))
    {
        wide_set(dividend, q);
        if(remainder)
            *remainder = (LONGINT)r;
        return dividend;
    }
    if(!divisor && a != INT64_MIN)
        wide_set(dividend, a < 0 ? (int64_t)0x8000000080000000ull : 0x7FFFFFFF80000000ll);
    else
        wide_set(dividend, negative_quotient(a, divisor) ? (int64_t)0xFFFFFFFF80000000ull : 0x80000000ll);
    if(remainder)
        *remainder = (LONGINT)0x80000000;
    return dividend;
}
