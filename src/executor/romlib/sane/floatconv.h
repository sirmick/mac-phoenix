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
 * FIXME: The current code assumes that ieee_t is a long double implemented as IEEE
 *        80 bit floating point (possibly padded at the end).
 *        This will not work on PowerPC or ARM, and not on MSVC either.
 *
 *          Possible solutions:
 *          * Use IEEE 64 bit double and live with the loss of precision.
 *          * Use whatever "long double" is and use frexp/ldexp for conversion
 *          * Use __float128 where available
 *          * Use boost multiprecision to do it with the right precision. 
 */

#include <string.h>
#include <sane/float.h>

namespace Executor
{
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

    if constexpr(sizeof(ieee_t) >= 10) // ASSUME for now that this is the right kind of extended FP
    {
        memcpy(&retval, &mant, 8);
        memcpy((char *)&retval + 8, &exp, 2);
    }
    else if constexpr(sizeof(ieee_t) == 8)
    {
        uint64_t bits = 0;
        bits |= uint64_t(exp >> 4ULL) << 52ULL;
        bits |= (mant & ~(1ULL << 63ULL)) >> 11;
        std::memcpy(&retval, &bits, 8);
    }
    return retval;
}

inline void ieee_to_x80(ieee_t n, x80_t *x)
{
    uint64_t mant;
    uint16_t exp;

    if constexpr(sizeof(ieee_t) >= 10) // ASSUME for now that this is the right kind of extended FP
    {
        memcpy(&mant, &n, 8);
        memcpy(&exp, (char *)&n + 8, 2);
    }
    else if constexpr(sizeof(ieee_t) == 8)
    {
        uint64_t bits;
        memcpy(&bits, &n, 8);

        exp = (bits >> 52ULL) << 4ULL;
        mant = (bits & ((1ULL << 52ULL) - 1)) << 11;

        if((exp & 0x7FFF) == 0)
            ;
        else
            mant |= 1ULL << 63;
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
