/*****************************************************************************
 *                                                                           *
 *                                 MathLib.h                                 *
 *                                 =========                                 *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>

#define MODULE_NAME MathLib
#include <base/api-module.h>


namespace Executor {

double h_cos(double x);LIBRARY_FUNCTION(MathLib, cos, h_cos);


double h_sin(double x);LIBRARY_FUNCTION(MathLib, sin, h_sin);


double h_tan(double x);LIBRARY_FUNCTION(MathLib, tan, h_tan);


double h_acos(double x);LIBRARY_FUNCTION(MathLib, acos, h_acos);


double h_asin(double x);LIBRARY_FUNCTION(MathLib, asin, h_asin);


double h_atan(double x);LIBRARY_FUNCTION(MathLib, atan, h_atan);


double h_atan2(double x, double y);LIBRARY_FUNCTION(MathLib, atan2, h_atan2);


double h_cosh(double x);LIBRARY_FUNCTION(MathLib, cosh, h_cosh);


double h_sinh(double x);LIBRARY_FUNCTION(MathLib, sinh, h_sinh);


double h_tanh(double x);LIBRARY_FUNCTION(MathLib, tanh, h_tanh);


double h_acosh(double x);LIBRARY_FUNCTION(MathLib, acosh, h_acosh);


double h_asinh(double x);LIBRARY_FUNCTION(MathLib, asinh, h_asinh);


double h_atanh(double x);LIBRARY_FUNCTION(MathLib, atanh, h_atanh);


double h_exp(double x);LIBRARY_FUNCTION(MathLib, exp, h_exp);


double h_expm1(double x);LIBRARY_FUNCTION(MathLib, expm1, h_expm1);


double h_exp2(double x);LIBRARY_FUNCTION(MathLib, exp2, h_exp2);


double h_log(double x);LIBRARY_FUNCTION(MathLib, log, h_log);


double h_log2(double x);LIBRARY_FUNCTION(MathLib, log2, h_log2);


double h_log1p(double x);LIBRARY_FUNCTION(MathLib, log1p, h_log1p);


double h_log10(double x);LIBRARY_FUNCTION(MathLib, log10, h_log10);


double h_logb(double x);LIBRARY_FUNCTION(MathLib, logb, h_logb);


double h_fabs(double x);LIBRARY_FUNCTION(MathLib, fabs, h_fabs);


double h_sqrt(double x);LIBRARY_FUNCTION(MathLib, sqrt, h_sqrt);


double h_erf(double x);LIBRARY_FUNCTION(MathLib, erf, h_erf);


double h_erfc(double x);LIBRARY_FUNCTION(MathLib, erfc, h_erfc);


double h_gamma(double x);LIBRARY_FUNCTION(MathLib, gamma, h_gamma);


double h_lgamma(double x);LIBRARY_FUNCTION(MathLib, lgamma, h_lgamma);


double h_ceil(double x);LIBRARY_FUNCTION(MathLib, ceil, h_ceil);


double h_floor(double x);LIBRARY_FUNCTION(MathLib, floor, h_floor);


double h_trunc(double x);LIBRARY_FUNCTION(MathLib, trunc, h_trunc);


double h_round(double x);LIBRARY_FUNCTION(MathLib, round, h_round);


double h_rint(double x);LIBRARY_FUNCTION(MathLib, rint, h_rint);


double h_nearbyint(double x);LIBRARY_FUNCTION(MathLib, nearbyint, h_nearbyint);


double h_hypot(double x, double y);LIBRARY_FUNCTION(MathLib, hypot, h_hypot);


double h_pow(double x, double y);LIBRARY_FUNCTION(MathLib, pow, h_pow);


double h_fmod(double x, double y);LIBRARY_FUNCTION(MathLib, fmod, h_fmod);


double h_remainder(double x, double y);LIBRARY_FUNCTION(MathLib, remainder, h_remainder);


double h_copysign(double x, double y);LIBRARY_FUNCTION(MathLib, copysign, h_copysign);


double h_fdim(double x, double y);LIBRARY_FUNCTION(MathLib, fdim, h_fdim);


double h_fmax(double x, double y);LIBRARY_FUNCTION(MathLib, fmax, h_fmax);


double h_fmin(double x, double y);LIBRARY_FUNCTION(MathLib, fmin, h_fmin);


double h_nextafterd(double x, double y);LIBRARY_FUNCTION(MathLib, nextafterd, h_nextafterd);


float m_nextafterf(float x, float y);LIBRARY_FUNCTION(MathLib, nextafterf, m_nextafterf);


double m_remquo(double x, double y, uint32_t quo);LIBRARY_FUNCTION(MathLib, remquo, m_remquo);


double m_modf(double x, uint32_t iptr);LIBRARY_FUNCTION(MathLib, modf, m_modf);


float m_modff(float x, uint32_t iptr);LIBRARY_FUNCTION(MathLib, modff, m_modff);


double m_frexp(double x, uint32_t exp);LIBRARY_FUNCTION(MathLib, frexp, m_frexp);


double m_ldexp(double x, int32_t n);LIBRARY_FUNCTION(MathLib, ldexp, m_ldexp);


double m_scalb(double x, int32_t n);LIBRARY_FUNCTION(MathLib, scalb, m_scalb);


int32_t m_rinttol(double x);LIBRARY_FUNCTION(MathLib, rinttol, m_rinttol);


int32_t m_roundtol(double x);LIBRARY_FUNCTION(MathLib, roundtol, m_roundtol);


int16_t m_relation(double x, double y);LIBRARY_FUNCTION(MathLib, relation, m_relation);


int32_t m_fpclassifyd(double x);LIBRARY_FUNCTION(MathLib, __fpclassifyd, m_fpclassifyd);


int32_t m_fpclassifyf(float x);LIBRARY_FUNCTION(MathLib, __fpclassifyf, m_fpclassifyf);


int32_t m_isnormald(double x);LIBRARY_FUNCTION(MathLib, __isnormald, m_isnormald);


int32_t m_isnormalf(float x);LIBRARY_FUNCTION(MathLib, __isnormalf, m_isnormalf);


int32_t m_isfinited(double x);LIBRARY_FUNCTION(MathLib, __isfinited, m_isfinited);


int32_t m_isfinitef(float x);LIBRARY_FUNCTION(MathLib, __isfinitef, m_isfinitef);


int32_t m_isnand(double x);LIBRARY_FUNCTION(MathLib, __isnand, m_isnand);


int32_t m_isnanf(float x);LIBRARY_FUNCTION(MathLib, __isnanf, m_isnanf);


int32_t m_signbitd(double x);LIBRARY_FUNCTION(MathLib, __signbitd, m_signbitd);


int32_t m_signbitf(float x);LIBRARY_FUNCTION(MathLib, __signbitf, m_signbitf);


LIBRARY_RAW(MathLib, __fpclassify);


LIBRARY_RAW(MathLib, __isnormal);


LIBRARY_RAW(MathLib, __isfinite);


LIBRARY_RAW(MathLib, __isnan);


LIBRARY_RAW(MathLib, __signbit);


double m_inf();LIBRARY_FUNCTION(MathLib, __inf, m_inf);


double m_nan(const char *tag);LIBRARY_FUNCTION(MathLib, nan, m_nan);


float m_nanf(const char *tag);LIBRARY_FUNCTION(MathLib, nanf, m_nanf);


LIBRARY_RAW(MathLib, nanl);


double m_compound(double rate, double periods);LIBRARY_FUNCTION(MathLib, compound, m_compound);


double m_annuity(double rate, double periods);LIBRARY_FUNCTION(MathLib, annuity, m_annuity);


double m_randomx(uint32_t xp);LIBRARY_FUNCTION(MathLib, randomx, m_randomx);


void c_feclearexcept(int32_t e);LIBRARY_FUNCTION(MathLib, feclearexcept, c_feclearexcept);


void c_feraiseexcept(int32_t e);LIBRARY_FUNCTION(MathLib, feraiseexcept, c_feraiseexcept);


int32_t c_fetestexcept(int32_t e);LIBRARY_FUNCTION(MathLib, fetestexcept, c_fetestexcept);


void c_fegetexcept(uint32_t flagp, int32_t e);LIBRARY_FUNCTION(MathLib, fegetexcept, c_fegetexcept);


void c_fesetexcept(uint32_t flagp, int32_t e);LIBRARY_FUNCTION(MathLib, fesetexcept, c_fesetexcept);


int32_t c_fegetround();LIBRARY_FUNCTION(MathLib, fegetround, c_fegetround);


int32_t c_fesetround(int32_t r);LIBRARY_FUNCTION(MathLib, fesetround, c_fesetround);


void c_fegetenv(uint32_t envp);LIBRARY_FUNCTION(MathLib, fegetenv, c_fegetenv);


int32_t c_feholdexcept(uint32_t envp);LIBRARY_FUNCTION(MathLib, feholdexcept, c_feholdexcept);


void c_fesetenv(uint32_t envp);LIBRARY_FUNCTION(MathLib, fesetenv, c_fesetenv);


void c_feupdateenv(uint32_t envp);LIBRARY_FUNCTION(MathLib, feupdateenv, c_feupdateenv);


LIBRARY_RAW(MathLib, cosl);


LIBRARY_RAW(MathLib, sinl);


LIBRARY_RAW(MathLib, tanl);


LIBRARY_RAW(MathLib, acosl);


LIBRARY_RAW(MathLib, asinl);


LIBRARY_RAW(MathLib, atanl);


LIBRARY_RAW(MathLib, coshl);


LIBRARY_RAW(MathLib, sinhl);


LIBRARY_RAW(MathLib, tanhl);


LIBRARY_RAW(MathLib, acoshl);


LIBRARY_RAW(MathLib, asinhl);


LIBRARY_RAW(MathLib, atanhl);


LIBRARY_RAW(MathLib, expl);


LIBRARY_RAW(MathLib, expm1l);


LIBRARY_RAW(MathLib, exp2l);


LIBRARY_RAW(MathLib, logl);


LIBRARY_RAW(MathLib, log1pl);


LIBRARY_RAW(MathLib, log10l);


LIBRARY_RAW(MathLib, log2l);


LIBRARY_RAW(MathLib, logbl);


LIBRARY_RAW(MathLib, sqrtl);


LIBRARY_RAW(MathLib, fabsl);


LIBRARY_RAW(MathLib, erfl);


LIBRARY_RAW(MathLib, erfcl);


LIBRARY_RAW(MathLib, gammal);


LIBRARY_RAW(MathLib, lgammal);


LIBRARY_RAW(MathLib, ceill);


LIBRARY_RAW(MathLib, floorl);


LIBRARY_RAW(MathLib, rintl);


LIBRARY_RAW(MathLib, nearbyintl);


LIBRARY_RAW(MathLib, roundl);


LIBRARY_RAW(MathLib, truncl);


LIBRARY_RAW(MathLib, atan2l);


LIBRARY_RAW(MathLib, powl);


LIBRARY_RAW(MathLib, remainderl);


LIBRARY_RAW(MathLib, copysignl);


LIBRARY_RAW(MathLib, nextafterl);


LIBRARY_RAW(MathLib, fdiml);


LIBRARY_RAW(MathLib, fmaxl);


LIBRARY_RAW(MathLib, fminl);


LIBRARY_RAW(MathLib, hypotl);


LIBRARY_RAW(MathLib, scalbl);


LIBRARY_RAW(MathLib, ldexpl);


LIBRARY_RAW(MathLib, frexpl);


LIBRARY_RAW(MathLib, modfl);


LIBRARY_RAW(MathLib, remquol);


LIBRARY_RAW(MathLib, rinttoll);


LIBRARY_RAW(MathLib, roundtoll);


LIBRARY_RAW(MathLib, relationl);


double m_x80tod(uint32_t x80);LIBRARY_FUNCTION(MathLib, x80tod, m_x80tod);


void m_dtox80(uint32_t xp, uint32_t x80);LIBRARY_FUNCTION(MathLib, dtox80, m_dtox80);


LIBRARY_RAW(MathLib, x80told);


void m_ldtox80(uint32_t xp, uint32_t x80);LIBRARY_FUNCTION(MathLib, ldtox80, m_ldtox80);


void m_num2dec(uint32_t f, double x, uint32_t d);LIBRARY_FUNCTION(MathLib, num2dec, m_num2dec);


LIBRARY_RAW(MathLib, num2decl);


double m_dec2num(uint32_t d);LIBRARY_FUNCTION(MathLib, dec2num, m_dec2num);


LIBRARY_RAW(MathLib, dec2numl);


float m_dec2f(uint32_t d);LIBRARY_FUNCTION(MathLib, dec2f, m_dec2f);


int16_t m_dec2s(uint32_t d);LIBRARY_FUNCTION(MathLib, dec2s, m_dec2s);


int32_t m_dec2l(uint32_t d);LIBRARY_FUNCTION(MathLib, dec2l, m_dec2l);


void m_str2dec(const char *s, uint32_t ixp, uint32_t d, uint32_t vp);LIBRARY_FUNCTION(MathLib, str2dec, m_str2dec);


void m_dec2str(uint32_t f, uint32_t dp, char *out);LIBRARY_FUNCTION(MathLib, dec2str, m_dec2str);


} /* namespace Executor */