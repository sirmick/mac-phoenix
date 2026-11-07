#if !defined(_floatconv_h_)
#define _floatconv_h_

/* This file contains inline routines to convert between ieee_t's and
 * SANE representations for various numbers.  The exact type of conversion
 * should be obvious from the function name.
 *
 * This file contains:
 *
 *     x80_to_ieee           ieee_to_x80          
 *     comp_to_ieee          ieee_to_comp
 *
 * These routines will only work on machines that support IEEE floating point.
 * 
 * ieee_t is long double: the x87's 80-bit format on x86 (bits copied),
 * whatever else elsewhere (converted by value; ieee_is_x87).
 */

#include <cmath>
#include <limits>
#include <string.h>
#include <sane/float.h>

namespace Executor
{
/* MacPhoenix: where long double is the x87's 80-bit format (64 mantissa
 * digits) the bits are copied, as upstream did; elsewhere (wasm and arm64
 * Linux: IEEE quad; macOS arm64: double) by value, rounding to nearest. */
constexpr bool ieee_is_x87 = std::numeric_limits<ieee_t>::digits == 64
                             && std::numeric_limits<ieee_t>::max_exponent == 16384;

inline ieee_t x80_to_ieee(const x80_t *x)
{
    ieee_t retval;

    uint64_t mant = x->mantissa;
    uint16_t exp = x->sgn_and_exp;

    /* If it's an Inf or NaN, be sure to set the "normalized" bit.
     * 80387 requires this bit to be set (else Inf's become NaN's)
     * but SANE doesn't care about it either way.
     */
    if((exp & 0x7FFF) == 0x7FFF)
        mant |= 1LL<<63;

    if constexpr(ieee_is_x87)
    {
        memcpy(&retval, &mant, 8);
        memcpy((char *)&retval + 8, &exp, 2);
    }
    else
    {
        bool neg = exp & 0x8000;
        int e = exp & 0x7FFF;
        if(e == 0x7FFF)
            retval = (mant << 1) ? std::numeric_limits<ieee_t>::quiet_NaN()
                                 : std::numeric_limits<ieee_t>::infinity();
        else
            /* value = mant * 2^(e - 16383 - 63); a denormal's e is 1. */
            retval = std::ldexp((ieee_t)mant, (e ? e : 1) - 16383 - 63);
        if(neg)
            retval = -retval;
    }
    return retval;
}

inline void ieee_to_x80(ieee_t n, x80_t *x)
{
    uint64_t mant;
    uint16_t exp;

    if constexpr(ieee_is_x87)
    {
        memcpy(&mant, &n, 8);
        memcpy(&exp, (char *)&n + 8, 2);
    }
    else
    {
        uint16_t sign = std::signbit(n) ? 0x8000 : 0;
        if(std::isnan(n))
        {
            exp = 0x7FFF;
            mant = 0xC000000000000000ULL;
        }
        else if(std::isinf(n))
        {
            exp = 0x7FFF;
            mant = 0x8000000000000000ULL;
        }
        else if(n == 0)
        {
            exp = 0;
            mant = 0;
        }
        else
        {
            /* |n| = m * 2^e, m in [0.5, 1): mant = m * 2^64, rounded. */
            int e;
            ieee_t m = std::frexp(std::fabs(n), &e);
            ieee_t r = std::nearbyint(std::ldexp(m, 64));
            if(r >= std::ldexp((ieee_t)1, 64))
            {
                r = std::ldexp(r, -1);
                e++;
            }
            mant = (uint64_t)r;
            int biased = e + 16382;
            if(biased >= 0x7FFF)
            {
                biased = 0x7FFF;
                mant = 0x8000000000000000ULL;
            }
            else if(biased <= 0)
            {
                /* A denormal: the exponent field 0 means 2^(1 - 16383). */
                int shift = 1 - biased;
                mant = shift < 64 ? mant >> shift : 0;
                biased = 0;
            }
            exp = (uint16_t)biased;
        }
        exp |= sign;
    }

    x->mantissa = mant;
    x->sgn_and_exp = exp;
}

inline ieee_t comp_to_ieee(const comp_t *cp)
{
    int64_t val = cp->val;
    if(val == (int64_t)0x8000000000000000ULL)
    {
        /* Magical value for SANE NaN derived from comp NaN. */
        static const unsigned char f64_nan_comp[] __attribute__((aligned(2)))
        = { 0x7F, 0xFF, 0x40, 0x00, 0x00, 0x00, 0x00, 0x14 };
        return *(const GUEST<double> *)&f64_nan_comp;
    }
    else
        return val;
}

inline void ieee_to_comp(ieee_t val, comp_t *dest)
{
    /* FIXME - does not handle range errors! */

    /* Check to see if val is NaN. */
    if(val != val)
        dest->val = (int64_t)0x8000000000000000ULL;
    else /* Not NaN */
        dest->val = val;
}
}
#endif
