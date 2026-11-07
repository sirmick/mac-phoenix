#if !defined(_romlib_float_h_)
#define _romlib_float_h_

/*
 * Copyright 1995 by Abacus Research and Development, Inc.
 * All rights reserved.
 *

 */

#include <syn68k_public.h>
#include <SANE.h>
#include <float.h>
#include <math.h>

namespace Executor
{
const LowMemGlobal<Byte[6]> macfpstate { 0xA4A }; // unknown ToolEqu.a (true-b);

/* Macros for manipulating the sgn and exponent fields of an x80_t. */

#define GET_X80_SGN(x) ((x)->sgn_and_exp >> 15)
#define SET_X80_SGN(x, v) ((x)->sgn_and_exp = ((x)->sgn_and_exp & 0x7FFF) | (v << 15))
#define GET_X80_EXP(x) ((x)->sgn_and_exp & 0x7FFF)
#define SET_X80_EXP(x, v) ((x)->sgn_and_exp = ((x)->sgn_and_exp & 0x8000) | v)


/* This is meant to represent an IEEE FP value with 80 bits of precision.
 */
typedef long double ieee_t;


#define OPCODE_MASK 0x3F00

#define DECLAREIN() ieee_t in
#define DECLAREIN2() ieee_t in1, in2
#define DECLAREIN2OUT() ieee_t in1, in2, out
#define DECLAREINOUT() ieee_t in, out

#endif /* Not _romlib_float_h_ */

#if !defined(_romlib_float_h_2_)

#if !defined(EXTERN_INLINE)
#define EXTERN_INLINE extern inline
#endif

#define ALWAYS_INLINE __attribute__((always_inline))

#define _romlib_float_h_2_

}
#endif
