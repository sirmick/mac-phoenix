/* MacPhoenix: MathLib for PowerPC fragments (docs/executor/PPCLibs.md):
 * fp.h and fenv.h, the Floating-Point C Extensions as MPW declares them,
 * computed by the host's C library. double_t is double; long double is
 * MathLib's double-double (two doubles, passed and returned in a pair of
 * floating-point registers), computed in the host's long double. The
 * floating-point environment is the PowerPC's FPSCR (fenv.h: fenv_t is the
 * register; the exception macros are its sticky bits). The decimal records
 * (decimal, decform: SANE's) follow fp.h and the Apple Numerics Manual. */

#include "clib.h"

#include <algorithm>
#include <cfenv>
#include <cfloat>
#include <climits>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace Executor;
using namespace Executor::clib;

namespace
{

const char LIB[] = "MathLib";

alignas(8) uint8_t pi_[8];
alignas(8) uint8_t feDflEnv_[4];

/* ---- FPSCR ---- */

constexpr uint32_t FE_INEXACT_ = 0x02000000, FE_DIVBYZERO_ = 0x04000000,
                   FE_UNDERFLOW_ = 0x08000000, FE_OVERFLOW_ = 0x10000000,
                   FE_INVALID_ = 0x20000000;
constexpr uint32_t FE_ALL = FE_INEXACT_ | FE_DIVBYZERO_ | FE_UNDERFLOW_ | FE_OVERFLOW_ | FE_INVALID_;
/* FPSCR: FX (bit 0, 0x80000000) summarizes, VX (0x20000000) is the invalid
   summary of the VX* bits; the enables are the low byte's 0xF8, RN the
   low two bits. */
constexpr uint32_t FPSCR_FX = 0x80000000, FPSCR_VXSNAN = 0x01000000,
                   FPSCR_ENABLES = 0x000000F8, FPSCR_RN = 3;
constexpr uint32_t FPSCR_VX_ALL = 0x01F80700;   /* VXSNAN..VXVC, VXSOFT, VXSQRT, VXCVI */

uint32_t &fpscr() { return getPowerCore().fpscr; }

/* The invalid summary follows its causes: raising "invalid" sets VXSOFT. */
void setExceptions(uint32_t bits)
{
    bits &= FE_ALL;
    if(!bits)
        return;
    uint32_t f = fpscr() | bits | FPSCR_FX;
    if(bits & FE_INVALID_)
        f |= 0x00000400;   /* VXSOFT */
    fpscr() = f;
}

void clearExceptions(uint32_t bits)
{
    bits &= FE_ALL;
    uint32_t f = fpscr() & ~bits;
    if(bits & FE_INVALID_)
        f &= ~FPSCR_VX_ALL;
    if(!(f & (FE_ALL & ~FE_INVALID_)) && !(f & FPSCR_VX_ALL))
        f &= ~FPSCR_FX;
    fpscr() = f;
}

void c_feclearexcept(int32_t e) { clearExceptions((uint32_t)e); }
void c_feraiseexcept(int32_t e) { setExceptions((uint32_t)e); }
int32_t c_fetestexcept(int32_t e) { return (int32_t)(fpscr() & FE_ALL & (uint32_t)e); }
void c_fegetexcept(uint32_t flagp, int32_t e)
{
    write32(flagp, fpscr() & FE_ALL & (uint32_t)e);
}
void c_fesetexcept(uint32_t flagp, int32_t e)
{
    uint32_t want = read32(flagp) & (uint32_t)e & FE_ALL;
    clearExceptions((uint32_t)e & ~want);
    setExceptions(want);
}
int32_t c_fegetround() { return (int32_t)(fpscr() & FPSCR_RN); }
int32_t c_fesetround(int32_t r)
{
    if(r < 0 || r > 3)
        return 0;
    fpscr() = (fpscr() & ~FPSCR_RN) | (uint32_t)r;
    return 1;
}
void c_fegetenv(uint32_t envp) { write32(envp, fpscr()); }
int32_t c_feholdexcept(uint32_t envp)
{
    write32(envp, fpscr());
    fpscr() &= ~(FE_ALL | FPSCR_VX_ALL | FPSCR_FX | FPSCR_ENABLES);
    return 1;
}
void c_fesetenv(uint32_t envp) { fpscr() = read32(envp); }
void c_feupdateenv(uint32_t envp)
{
    uint32_t raised = fpscr() & FE_ALL;
    fpscr() = read32(envp);
    setExceptions(raised);
}

/* What the host's computation raised, onto the guest's flags. */
struct HostFlags
{
    HostFlags() { std::feclearexcept(FE_ALL_EXCEPT); }
    ~HostFlags()
    {
        /* WebAssembly has no floating-point exception flags: none of
           these is defined there, and the host raises nothing. */
        int e = std::fetestexcept(FE_ALL_EXCEPT);
        uint32_t bits = 0;
#ifdef FE_INEXACT
        if(e & FE_INEXACT) bits |= FE_INEXACT_;
#endif
#ifdef FE_DIVBYZERO
        if(e & FE_DIVBYZERO) bits |= FE_DIVBYZERO_;
#endif
#ifdef FE_UNDERFLOW
        if(e & FE_UNDERFLOW) bits |= FE_UNDERFLOW_;
#endif
#ifdef FE_OVERFLOW
        if(e & FE_OVERFLOW) bits |= FE_OVERFLOW_;
#endif
#ifdef FE_INVALID
        if(e & FE_INVALID) bits |= FE_INVALID_;
#endif
        (void)e;
        setExceptions(bits);
    }
};

/* The guest's rounding direction for the host's computation. */
struct HostRounding
{
    int old;
    HostRounding()
    {
        static const int modes[4] = { FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD };
        old = std::fegetround();
        std::fesetround(modes[fpscr() & FPSCR_RN]);
    }
    ~HostRounding() { std::fesetround(old); }
};

/* ---- double ---- */

template<double (*F)(double)>
double unary(double x)
{
    HostFlags flags;
    return F(x);
}

template<double (*F)(double, double)>
double binary(double x, double y)
{
    HostFlags flags;
    return F(x, y);
}

double m_cos(double x) { return std::cos(x); }
double m_sin(double x) { return std::sin(x); }
double m_tan(double x) { return std::tan(x); }
double m_acos(double x);
double m_asin(double x);
double m_atan(double x) { return std::atan(x); }
double m_atan2(double y, double x) { return std::atan2(y, x); }
double m_cosh(double x) { return std::cosh(x); }
double m_sinh(double x) { return std::sinh(x); }
double m_tanh(double x) { return std::tanh(x); }
double m_acosh(double x) { return std::acosh(x); }
double m_asinh(double x) { return std::asinh(x); }
double m_atanh(double x) { return std::atanh(x); }
double m_exp(double x) { return std::exp(x); }
double m_expm1(double x) { return std::expm1(x); }
double m_exp2(double x) { return std::exp2(x); }
double m_log(double x);
double m_log2(double x);
double m_log1p(double x);
double m_log10(double x);
double m_logb(double x) { return std::logb(x); }
double m_fabs(double x) { return std::fabs(x); }
double m_sqrt(double x);
double m_erf(double x) { return std::erf(x); }
double m_erfc(double x) { return std::erfc(x); }
double m_gamma(double x) { return std::tgamma(x); }
double m_lgamma(double x) { return std::lgamma(x); }
double m_ceil(double x) { return std::ceil(x); }
double m_floor(double x) { return std::floor(x); }
double m_trunc(double x) { return std::trunc(x); }
double m_round(double x) { return std::round(x); }
double m_rint(double x) { HostRounding r; return std::rint(x); }
double m_nearbyint(double x) { HostRounding r; return std::nearbyint(x); }
double m_hypot(double x, double y) { return std::hypot(x, y); }
double m_pow(double x, double y) { return std::pow(x, y); }
double m_fmod(double x, double y) { return std::fmod(x, y); }
double m_remainder(double x, double y) { return std::remainder(x, y); }
double m_copysign(double x, double y) { return std::copysign(x, y); }
double m_fdim(double x, double y) { return std::fdim(x, y); }
double m_fmax(double x, double y) { return std::fmax(x, y); }
double m_fmin(double x, double y) { return std::fmin(x, y); }
double m_nextafterd(double x, double y) { return std::nextafter(x, y); }

float m_nextafterf(float x, float y) { HostFlags f; return std::nextafter(x, y); }

double m_remquo(double x, double y, uint32_t quo)
{
    HostFlags flags;
    int q = 0;
    double r = std::remquo(x, y, &q);
    if(quo)
        write32(quo, (uint32_t)q);
    return r;
}

double m_modf(double x, uint32_t iptr)
{
    double i;
    double f = std::modf(x, &i);
    if(iptr)
        writeDouble(iptr, i);
    return f;
}

float m_modff(float x, uint32_t iptr)
{
    float i;
    float f = std::modf(x, &i);
    if(iptr)
        writeFloat(iptr, i);
    return f;
}

double m_frexp(double x, uint32_t exp)
{
    int e = 0;
    double m = std::frexp(x, &e);
    if(exp)
        write32(exp, (uint32_t)e);
    return m;
}

double m_ldexp(double x, int32_t n) { HostFlags f; return std::ldexp(x, n); }
double m_scalb(double x, int32_t n) { HostFlags f; return std::scalbn(x, n); }

int32_t clampLong(double r)
{
    if(std::isnan(r) || r >= 2147483648.0 || r < -2147483648.0)
    {
        setExceptions(FE_INVALID_);
        return INT32_MIN;
    }
    return (int32_t)r;
}

int32_t m_rinttol(double x) { HostRounding r; return clampLong(std::nearbyint(x)); }
int32_t m_roundtol(double x) { return clampLong(std::round(x)); }

/* relop (fp.h) */
int16_t relationOf(long double x, long double y)
{
    if(std::isnan(x) || std::isnan(y))
        return 3;
    return x > y ? 0 : x < y ? 1 : 2;
}
int16_t m_relation(double x, double y) { return relationOf(x, y); }

/* NumberKind (fp.h): SNAN, QNAN, INFINITE, ZERO, NORMAL, SUBNORMAL. */
int32_t classifyDouble(double x)
{
    uint64_t bits;
    memcpy(&bits, &x, sizeof bits);
    switch(std::fpclassify(x))
    {
        case FP_NAN: return (bits & 0x0008000000000000ull) ? 1 : 0;
        case FP_INFINITE: return 2;
        case FP_ZERO: return 3;
        case FP_SUBNORMAL: return 5;
        default: return 4;
    }
}
int32_t classifyFloat(float x)
{
    uint32_t bits;
    memcpy(&bits, &x, sizeof bits);
    switch(std::fpclassify(x))
    {
        case FP_NAN: return (bits & 0x00400000) ? 1 : 0;
        case FP_INFINITE: return 2;
        case FP_ZERO: return 3;
        case FP_SUBNORMAL: return 5;
        default: return 4;
    }
}

int32_t m_fpclassifyd(double x) { return classifyDouble(x); }
int32_t m_fpclassifyf(float x) { return classifyFloat(x); }
int32_t m_isnormald(double x) { return classifyDouble(x) == 4; }
int32_t m_isnormalf(float x) { return classifyFloat(x) == 4; }
int32_t m_isfinited(double x) { return std::isfinite(x); }
int32_t m_isfinitef(float x) { return std::isfinite(x); }
int32_t m_isnand(double x) { return std::isnan(x); }
int32_t m_isnanf(float x) { return std::isnan(x); }
int32_t m_signbitd(double x) { return std::signbit(x); }
int32_t m_signbitf(float x) { return std::signbit(x); }

double m_inf() { return HUGE_VAL; }

/* nan("n"): a quiet NaN with code n (0-255) in its fraction, SANE's NaN
   codes (Apple Numerics Manual): bits 37-44, as Apple's MathLib puts them
   (tests/guest/clib). */
constexpr int NAN_CODE_SHIFT = 37;

double nanFromCode(unsigned code)
{
    uint64_t bits = 0x7FF8000000000000ull | (uint64_t(code & 0xFF) << NAN_CODE_SHIFT);
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

unsigned nanCode(double x)
{
    uint64_t bits;
    memcpy(&bits, &x, sizeof bits);
    return unsigned(bits >> NAN_CODE_SHIFT) & 0xFF;
}

/* nan("n"): codes past 255 are 255; no digits, 0 (Apple's). */
double nanWithCode(const char *tag)
{
    unsigned code = 0;
    if(tag)
        for(const char *p = tag; *p >= '0' && *p <= '9'; p++)
            code = std::min(code * 10 + unsigned(*p - '0'), 255u);
    return nanFromCode(code);
}

/* An invalid operation's NaN carries SANE's code for it (Apple Numerics
   Manual): NANSQRT 1, NANINVTRIG 34, NANLOG 36. A NaN argument passes
   through as it is. */
double invalidCode(double result, double arg, unsigned code)
{
    if(std::isnan(result) && !std::isnan(arg))
        return nanFromCode(code);
    return result;
}

double m_nan(const char *tag) { return nanWithCode(tag); }
double m_acos(double x) { return invalidCode(std::acos(x), x, 34); }
double m_asin(double x) { return invalidCode(std::asin(x), x, 34); }
double m_log(double x) { return invalidCode(std::log(x), x, 36); }
double m_log2(double x) { return invalidCode(std::log2(x), x, 36); }
double m_log1p(double x) { return invalidCode(std::log1p(x), x, 36); }
double m_log10(double x) { return invalidCode(std::log10(x), x, 36); }
double m_sqrt(double x) { return invalidCode(std::sqrt(x), x, 1); }
float m_nanf(const char *tag) { return (float)nanWithCode(tag); }

double m_compound(double rate, double periods)
{
    HostFlags f;
    return std::pow(1.0 + rate, periods);
}

double m_annuity(double rate, double periods)
{
    HostFlags f;
    if(rate == 0)
        return periods;
    return (1.0 - std::pow(1.0 + rate, -periods)) / rate;
}

/* SANE's random: the Lehmer generator, x = 7^5 x mod (2^31 - 1). */
double m_randomx(uint32_t xp)
{
    double x = readDouble(xp);
    double r = std::fmod(16807.0 * std::trunc(x), 2147483647.0);
    if(r < 0)
        r += 2147483647.0;
    writeDouble(xp, r);
    return r;
}

/* ---- long double: double-double in a register pair ---- */

long double fromPair(double hi, double lo) { return (long double)hi + (long double)lo; }

void toPair(PowerCore &cpu, int f, long double v)
{
    double hi = (double)v;
    double lo = std::isfinite(hi) ? (double)(v - (long double)hi) : 0.0;
    cpu.f[f] = hi;
    cpu.f[f + 1] = lo;
}

long double argL(PowerCore &cpu, int f) { return fromPair(cpu.f[f], cpu.f[f + 1]); }

template<long double (*F)(long double)>
uint32_t unaryL(PowerCore &cpu)
{
    HostFlags flags;
    toPair(cpu, 1, F(argL(cpu, 1)));
    return cpu.lr;
}

template<long double (*F)(long double, long double)>
uint32_t binaryL(PowerCore &cpu)
{
    HostFlags flags;
    toPair(cpu, 1, F(argL(cpu, 1), argL(cpu, 3)));
    return cpu.lr;
}

long double l_cos(long double x) { return std::cos(x); }
long double l_sin(long double x) { return std::sin(x); }
long double l_tan(long double x) { return std::tan(x); }
long double l_acos(long double x) { return std::acos(x); }
long double l_asin(long double x) { return std::asin(x); }
long double l_atan(long double x) { return std::atan(x); }
long double l_cosh(long double x) { return std::cosh(x); }
long double l_sinh(long double x) { return std::sinh(x); }
long double l_tanh(long double x) { return std::tanh(x); }
long double l_acosh(long double x) { return std::acosh(x); }
long double l_asinh(long double x) { return std::asinh(x); }
long double l_atanh(long double x) { return std::atanh(x); }
long double l_exp(long double x) { return std::exp(x); }
long double l_expm1(long double x) { return std::expm1(x); }
long double l_exp2(long double x) { return std::exp2(x); }
long double l_log(long double x) { return std::log(x); }
long double l_log1p(long double x) { return std::log1p(x); }
long double l_log10(long double x) { return std::log10(x); }
long double l_log2(long double x) { return std::log2(x); }
long double l_logb(long double x) { return std::logb(x); }
long double l_sqrt(long double x) { return std::sqrt(x); }
long double l_fabs(long double x) { return std::fabs(x); }
long double l_erf(long double x) { return std::erf(x); }
long double l_erfc(long double x) { return std::erfc(x); }
long double l_gamma(long double x) { return std::tgamma(x); }
long double l_lgamma(long double x) { return std::lgamma(x); }
long double l_ceil(long double x) { return std::ceil(x); }
long double l_floor(long double x) { return std::floor(x); }
long double l_rint(long double x) { HostRounding r; return std::rint(x); }
long double l_nearbyint(long double x) { HostRounding r; return std::nearbyint(x); }
long double l_round(long double x) { return std::round(x); }
long double l_trunc(long double x) { return std::trunc(x); }
long double l_atan2(long double y, long double x) { return std::atan2(y, x); }
long double l_pow(long double x, long double y) { return std::pow(x, y); }
long double l_remainder(long double x, long double y) { return std::remainder(x, y); }
long double l_copysign(long double x, long double y) { return std::copysign(x, y); }
long double l_nextafter(long double x, long double y) { return (long double)std::nextafter((double)x, (double)y); }
long double l_fdim(long double x, long double y) { return std::fdim(x, y); }
long double l_fmax(long double x, long double y) { return std::fmax(x, y); }
long double l_fmin(long double x, long double y) { return std::fmin(x, y); }
long double l_hypot(long double x, long double y) { return std::hypot(x, y); }

/* Mixed arguments: a long double takes four parameter words. */
uint32_t raw_scalbl(PowerCore &cpu)
{
    HostFlags flags;
    toPair(cpu, 1, std::scalbn(argL(cpu, 1), (int32_t)PPCArgs{cpu}.word(4)));
    return cpu.lr;
}

uint32_t raw_frexpl(PowerCore &cpu)
{
    int e = 0;
    toPair(cpu, 1, std::frexp(argL(cpu, 1), &e));
    if(uint32_t p = PPCArgs{cpu}.word(4))
        write32(p, (uint32_t)e);
    return cpu.lr;
}

uint32_t raw_modfl(PowerCore &cpu)
{
    long double i;
    long double f = std::modf(argL(cpu, 1), &i);
    if(uint32_t p = PPCArgs{cpu}.word(4))
    {
        double hi = (double)i;
        writeDouble(p, hi);
        writeDouble(p + 8, (double)(i - (long double)hi));
    }
    toPair(cpu, 1, f);
    return cpu.lr;
}

uint32_t raw_remquol(PowerCore &cpu)
{
    HostFlags flags;
    int q = 0;
    long double r = std::remquo(argL(cpu, 1), argL(cpu, 3), &q);
    if(uint32_t p = PPCArgs{cpu}.word(8))
        write32(p, (uint32_t)q);
    toPair(cpu, 1, r);
    return cpu.lr;
}

uint32_t raw_rinttoll(PowerCore &cpu)
{
    HostRounding r;
    cpu.r[3] = (uint32_t)clampLong((double)std::nearbyint(argL(cpu, 1)));
    return cpu.lr;
}

uint32_t raw_roundtoll(PowerCore &cpu)
{
    cpu.r[3] = (uint32_t)clampLong((double)std::round(argL(cpu, 1)));
    return cpu.lr;
}

uint32_t raw_relationl(PowerCore &cpu)
{
    cpu.r[3] = (uint32_t)(int32_t)relationOf(argL(cpu, 1), argL(cpu, 3));
    return cpu.lr;
}

uint32_t raw_nanl(PowerCore &cpu)
{
    cpu.f[1] = nanWithCode(host<char>(cpu.r[3]));
    cpu.f[2] = 0;
    return cpu.lr;
}

/* The long double inquiries look at the high double. */
uint32_t raw_classifyL(PowerCore &cpu, int32_t (*f)(double))
{
    cpu.r[3] = (uint32_t)f(cpu.f[1]);
    return cpu.lr;
}

/* ---- extended80 (10 bytes: sign and exponent, then a 64-bit significand
   with an explicit integer bit) ---- */

long double fromX80(uint32_t p)
{
    uint16_t se = read16(p);
    uint64_t man = (uint64_t(read32(p + 2)) << 32) | read32(p + 6);
    bool neg = se & 0x8000;
    int exp = se & 0x7FFF;
    long double v;
    if(exp == 0x7FFF)
    {
        if((man << 1) == 0)
            v = HUGE_VALL;
        else
        {
            uint64_t bits = 0x7FF0000000000000ull | ((man >> 11) & 0x000FFFFFFFFFFFFFull);
            if(!(bits & 0x000FFFFFFFFFFFFFull))
                bits |= 0x0008000000000000ull;
            double d;
            memcpy(&d, &bits, sizeof d);
            v = d;
        }
    }
    else
        v = std::ldexp((long double)man, exp - 16383 - 63);
    return neg ? -v : v;
}

void toX80(uint32_t p, long double v)
{
    uint16_t se = std::signbit(v) ? 0x8000 : 0;
    uint64_t man = 0;
    v = std::fabs(v);
    if(std::isnan(v))
    {
        double d = (double)v;
        uint64_t bits;
        memcpy(&bits, &d, sizeof bits);
        se |= 0x7FFF;
        man = 0x8000000000000000ull | ((bits & 0x000FFFFFFFFFFFFFull) << 11);
    }
    else if(std::isinf(v))
    {
        se |= 0x7FFF;
        man = 0x8000000000000000ull;
    }
    else if(v != 0)
    {
        int e;
        long double m = std::frexp(v, &e);   /* [0.5, 1) */
        int exp = e - 1 + 16383;
        if(exp <= 0)
        {
            /* denormal in extended: shift the significand right */
            man = (uint64_t)std::ldexp(m, 64 + exp - 1);
            exp = 0;
        }
        else
            man = (uint64_t)std::ldexp(m, 64);
        se |= (uint16_t)exp;
    }
    write16(p, se);
    write32(p + 2, uint32_t(man >> 32));
    write32(p + 6, uint32_t(man));
}

double m_x80tod(uint32_t x80) { return (double)fromX80(x80); }
void m_dtox80(uint32_t xp, uint32_t x80) { toX80(x80, readDouble(xp)); }
uint32_t raw_x80told(PowerCore &cpu)
{
    toPair(cpu, 1, fromX80(cpu.r[3]));
    return cpu.lr;
}
void m_ldtox80(uint32_t xp, uint32_t x80)
{
    toX80(x80, fromPair(readDouble(xp), readDouble(xp + 8)));
}

/* ---- decimal records ---- */

/* struct decimal: sgn, unused, exp (short), sig { length, text[36], unused }.
   struct decform: style, unused, digits (short). */
constexpr int SIGDIGLEN = 36;

struct Decimal
{
    bool neg = false;
    int exp = 0;
    std::string sig;
};

Decimal readDecimal(uint32_t d)
{
    Decimal r;
    r.neg = host(d)[0] != 0;
    r.exp = (int16_t)read16(d + 2);
    int len = std::min<int>(host(d)[4], SIGDIGLEN);
    r.sig.assign(host<char>(d + 5), len);
    return r;
}

void writeDecimal(uint32_t d, const Decimal &v)
{
    host(d)[0] = v.neg ? 1 : 0;
    host(d)[1] = 0;
    write16(d + 2, (uint16_t)(int16_t)std::clamp(v.exp, -32768, 32767));
    int len = std::min<int>((int)v.sig.size(), SIGDIGLEN);
    host(d)[4] = (uint8_t)len;
    memcpy(host(d + 5), v.sig.data(), len);
}

/* NaN codes in a decimal record: "N" and the significand's hex digits. */
Decimal num2decimal(int style, int digits, long double x)
{
    Decimal r;
    r.neg = std::signbit(x);
    long double a = std::fabs(x);
    if(std::isnan(a))
    {
        /* "N" and four hex digits: the top of the significand below its
           integer bit, as SANE's extended has it (quiet bit, code). */
        double d = (double)a;
        uint64_t bits;
        memcpy(&bits, &d, sizeof bits);
        char text[8];
        snprintf(text, sizeof text, "N%04X", unsigned(((bits & 0x000FFFFFFFFFFFFFull) << 11) >> 48));
        r.sig = text;
        return r;
    }
    if(std::isinf(a))
    {
        r.sig = "I";
        return r;
    }
    if(a == 0)
    {
        r.sig = "0";
        return r;
    }
    char buf[512];
    if(style == 0)   /* FLOATDECIMAL: digits significant digits */
    {
        int n = std::clamp(digits, 1, SIGDIGLEN);
        snprintf(buf, sizeof buf, "%.*Le", n - 1, a);
        char *e = strchr(buf, 'e');
        int e10 = atoi(e + 1);
        std::string s(1, buf[0]);
        if(buf[1] == '.')
            s.append(buf + 2, e);
        r.sig = s;
        r.exp = e10 - (n - 1);
    }
    else   /* FIXEDDECIMAL: digits after the point */
    {
        int n = std::clamp(digits, -32767, 32767);
        if(n >= 0)
        {
            snprintf(buf, sizeof buf, "%.*Lf", std::min(n, 400), a);
            std::string s;
            for(char *p = buf; *p; p++)
                if(*p != '.')
                    s += *p;
            size_t lead = s.find_first_not_of('0');
            s = lead == std::string::npos ? "0" : s.substr(lead);
            r.exp = s == "0" ? 0 : -std::min(n, 400);
            if((int)s.size() > SIGDIGLEN)
            {
                /* More digits than a record holds. */
                r.sig = "?";
                r.exp = 0;
                return r;
            }
            r.sig = s;
        }
        else
        {
            long double scaled = std::nearbyint(a / std::pow(10.0L, -n));
            snprintf(buf, sizeof buf, "%.0Lf", scaled);
            r.sig = buf;
            r.exp = -n;
            if(r.sig == "0")
                r.exp = 0;
            if((int)r.sig.size() > SIGDIGLEN)
            {
                r.sig = "?";
                r.exp = 0;
            }
        }
    }
    return r;
}

long double decimal2num(const Decimal &d)
{
    long double v;
    if(d.sig.empty())
        v = 0;
    else if(d.sig[0] == 'I')
        v = HUGE_VALL;
    else if(d.sig[0] == 'N')
        v = nanFromCode((unsigned)strtoul(d.sig.c_str() + 1, nullptr, 16) & 0xFF);
    else if(d.sig[0] < '0' || d.sig[0] > '9')
        v = nanWithCode("17");
    else
    {
        std::string s = d.sig + "e" + std::to_string(d.exp);
        v = strtold(s.c_str(), nullptr);
    }
    return d.neg ? -v : v;
}

struct CLocale
{
    locale_t old;
    CLocale()
    {
        static locale_t c = newlocale(LC_ALL_MASK, "C", (locale_t)0);
        old = uselocale(c);
    }
    ~CLocale() { uselocale(old); }
};

void m_num2dec(uint32_t f, double x, uint32_t d)
{
    CLocale c;
    writeDecimal(d, num2decimal((int8_t)host(f)[0], (int16_t)read16(f + 2), x));
}

uint32_t raw_num2decl(PowerCore &cpu)
{
    CLocale c;
    uint32_t f = cpu.r[3];
    uint32_t d = PPCArgs{cpu}.word(5);
    writeDecimal(d, num2decimal((int8_t)host(f)[0], (int16_t)read16(f + 2), argL(cpu, 1)));
    return cpu.lr;
}

double m_dec2num(uint32_t d)
{
    CLocale c;
    HostFlags flags;
    return (double)decimal2num(readDecimal(d));
}

uint32_t raw_dec2numl(PowerCore &cpu)
{
    CLocale c;
    toPair(cpu, 1, decimal2num(readDecimal(cpu.r[3])));
    return cpu.lr;
}

float m_dec2f(uint32_t d)
{
    CLocale c;
    HostFlags flags;
    return (float)decimal2num(readDecimal(d));
}

int16_t m_dec2s(uint32_t d)
{
    CLocale c;
    long double v = std::nearbyint(decimal2num(readDecimal(d)));
    if(std::isnan(v) || v > 32767 || v < -32768)
    {
        setExceptions(FE_INVALID_);
        return -32768;
    }
    return (int16_t)v;
}

int32_t m_dec2l(uint32_t d)
{
    CLocale c;
    return clampLong((double)std::nearbyint(decimal2num(readDecimal(d))));
}

/* str2dec: the scanner. From s[*ix]: blanks, a sign, digits with a point,
   an exponent; or INF, NAN(n). *ix moves past what was taken; *vp says
   whether the text from where it started to the end of the string is a
   number or the start of one. */
void m_str2dec(const char *s, uint32_t ixp, uint32_t d, uint32_t vp)
{
    int16_t ix = (int16_t)read16(ixp);
    int len = (int)strlen(s);
    int i = std::clamp<int>(ix, 0, len);
    Decimal r;
    int start = i;
    while(i < len && (s[i] == ' ' || s[i] == '\t'))
        i++;
    if(i < len && (s[i] == '+' || s[i] == '-'))
        r.neg = s[i++] == '-';
    int afterSign = i;
    bool valid = false;
    int end = start;
    auto upper = [](char c) { return (char)(c >= 'a' && c <= 'z' ? c - 32 : c); };
    if(i < len && (upper(s[i]) == 'I' || upper(s[i]) == 'N'))
    {
        if(!strncasecmp(s + i, "INF", 3))
        {
            r.sig = "I";
            i += 3;
            valid = true;
            end = i;
        }
        else if(!strncasecmp(s + i, "NAN", 3))
        {
            i += 3;
            unsigned code = 0;
            if(i < len && s[i] == '(')
            {
                int j = i + 1;
                while(j < len && cIsDigit(s[j]))
                    code = code * 10 + unsigned(s[j++] - '0');
                if(j < len && s[j] == ')')
                    i = j + 1;
            }
            char text[8];
            snprintf(text, sizeof text, "N%04X", 0x4000 | std::min(code, 255u));
            r.sig = text;
            valid = true;
            end = i;
        }
    }
    else
    {
        /* All the digits, and how many came after the point. */
        std::string digits;
        bool point = false, any = false;
        int fracDigits = 0;
        while(i < len && (cIsDigit(s[i]) || (s[i] == '.' && !point)))
        {
            if(s[i] == '.')
                point = true;
            else
            {
                any = true;
                digits += s[i];
                if(point)
                    fracDigits++;
            }
            i++;
        }
        if(any)
        {
            valid = true;
            end = i;
            int exp = 0;
            if(i < len && (s[i] == 'e' || s[i] == 'E'))
            {
                int j = i + 1;
                bool eneg = false;
                if(j < len && (s[j] == '+' || s[j] == '-'))
                    eneg = s[j++] == '-';
                if(j < len && cIsDigit(s[j]))
                {
                    int e = 0;
                    while(j < len && cIsDigit(s[j]))
                        e = std::min(e * 10 + (s[j++] - '0'), 99999);
                    exp = eneg ? -e : e;
                    end = i = j;
                }
                else
                    i = j;   /* "1e" is the start of a number */
            }
            size_t lead = digits.find_first_not_of('0');
            if(lead == std::string::npos)
            {
                r.sig = "0";
                r.exp = 0;
            }
            else
            {
                digits.erase(0, lead);
                int scale = 0;
                if((int)digits.size() > SIGDIGLEN)
                {
                    scale = (int)digits.size() - SIGDIGLEN;
                    digits.resize(SIGDIGLEN);
                }
                r.sig = digits;
                r.exp = exp - fracDigits + scale;
            }
        }
        else
            i = std::max(i, afterSign);
    }
    if(!valid)
    {
        /* Not a number: NaN code 17, NANASCBIN (as Apple's: no quiet bit). */
        r = Decimal();
        r.sig = "N0011";
        end = start;
    }
    write16(ixp, (uint16_t)end);
    writeDecimal(d, r);
    if(vp)
        write16(vp, (uint16_t)(i >= len ? 1 : 0));
}

/* dec2str: FLOATDECIMAL "[-]d.ddde[+|-]ddd", FIXEDDECIMAL "[-]ddd.ddd"
   (digits after the point as the decform has them). */
void m_dec2str(uint32_t f, uint32_t dp, char *out)
{
    Decimal d = readDecimal(dp);
    int style = (int8_t)host(f)[0];
    int digits = (int16_t)read16(f + 2);
    /* A positive FLOATDECIMAL number has a blank where the sign goes. */
    std::string s = d.neg ? "-" : style == 0 ? " " : "";
    if(d.sig.empty() || d.sig == "0")
        d.sig = "0", d.exp = 0;
    if(d.sig[0] == 'I')
        s += "INF";
    else if(d.sig[0] == 'N')
    {
        char buf[16];
        snprintf(buf, sizeof buf, "NAN(%03lu)", strtoul(d.sig.c_str() + 1, nullptr, 16) & 0xFF);
        s += buf;
    }
    else if(d.sig[0] == '?')
        s += "?";
    else if(style == 0)
    {
        std::string m = d.sig;
        int e = d.exp + (int)m.size() - 1;
        if(m == "0")
            e = 0;
        s += m[0];
        if(m.size() > 1)
            s += "." + m.substr(1);
        s += e < 0 ? "e-" : "e+";
        s += std::to_string(std::abs(e));
    }
    else
    {
        std::string m = d.sig;
        int exp = d.exp;
        /* The digits as an integer part and a fraction. */
        std::string intPart, frac;
        if(exp >= 0)
            intPart = m + std::string(exp, '0');
        else if((int)m.size() > -exp)
        {
            intPart = m.substr(0, m.size() + exp);
            frac = m.substr(m.size() + exp);
        }
        else
        {
            intPart = "0";
            frac = std::string(-exp - m.size(), '0') + m;
        }
        size_t lead = intPart.find_first_not_of('0');
        intPart = lead == std::string::npos ? "0" : intPart.substr(lead);
        s += intPart;
        int want = std::max(digits, 0);
        if((int)frac.size() < want)
            frac += std::string(want - frac.size(), '0');
        if(!frac.empty())
            s += "." + frac;
    }
    if(s.size() > 80)
        s = "?";
    memcpy(out, s.c_str(), s.size() + 1);
}

}

void Executor::clib::addMathLib()
{
    writeDouble(guest(pi_), 3.14159265358979323846);
    write32(guest(feDflEnv_), 0);
    exportData(LIB, "pi", pi_);
    exportData(LIB, "_FE_DFL_ENV", feDflEnv_);

    exportFn(LIB, "cos", &unary<m_cos>);
    exportFn(LIB, "sin", &unary<m_sin>);
    exportFn(LIB, "tan", &unary<m_tan>);
    exportFn(LIB, "acos", &unary<m_acos>);
    exportFn(LIB, "asin", &unary<m_asin>);
    exportFn(LIB, "atan", &unary<m_atan>);
    exportFn(LIB, "atan2", &binary<m_atan2>);
    exportFn(LIB, "cosh", &unary<m_cosh>);
    exportFn(LIB, "sinh", &unary<m_sinh>);
    exportFn(LIB, "tanh", &unary<m_tanh>);
    exportFn(LIB, "acosh", &unary<m_acosh>);
    exportFn(LIB, "asinh", &unary<m_asinh>);
    exportFn(LIB, "atanh", &unary<m_atanh>);
    exportFn(LIB, "exp", &unary<m_exp>);
    exportFn(LIB, "expm1", &unary<m_expm1>);
    exportFn(LIB, "exp2", &unary<m_exp2>);
    exportFn(LIB, "log", &unary<m_log>);
    exportFn(LIB, "log2", &unary<m_log2>);
    exportFn(LIB, "log1p", &unary<m_log1p>);
    exportFn(LIB, "log10", &unary<m_log10>);
    exportFn(LIB, "logb", &unary<m_logb>);
    exportFn(LIB, "fabs", &unary<m_fabs>);
    exportFn(LIB, "sqrt", &unary<m_sqrt>);
    exportFn(LIB, "erf", &unary<m_erf>);
    exportFn(LIB, "erfc", &unary<m_erfc>);
    exportFn(LIB, "gamma", &unary<m_gamma>);
    exportFn(LIB, "lgamma", &unary<m_lgamma>);
    exportFn(LIB, "ceil", &unary<m_ceil>);
    exportFn(LIB, "floor", &unary<m_floor>);
    exportFn(LIB, "trunc", &unary<m_trunc>);
    exportFn(LIB, "round", &unary<m_round>);
    exportFn(LIB, "rint", &unary<m_rint>);
    exportFn(LIB, "nearbyint", &unary<m_nearbyint>);
    exportFn(LIB, "hypot", &binary<m_hypot>);
    exportFn(LIB, "pow", &binary<m_pow>);
    exportFn(LIB, "fmod", &binary<m_fmod>);
    exportFn(LIB, "remainder", &binary<m_remainder>);
    exportFn(LIB, "copysign", &binary<m_copysign>);
    exportFn(LIB, "fdim", &binary<m_fdim>);
    exportFn(LIB, "fmax", &binary<m_fmax>);
    exportFn(LIB, "fmin", &binary<m_fmin>);
    exportFn(LIB, "nextafterd", &binary<m_nextafterd>);
    exportFn(LIB, "nextafterf", &m_nextafterf);
    exportFn(LIB, "remquo", &m_remquo);
    exportFn(LIB, "modf", &m_modf);
    exportFn(LIB, "modff", &m_modff);
    exportFn(LIB, "frexp", &m_frexp);
    exportFn(LIB, "ldexp", &m_ldexp);
    exportFn(LIB, "scalb", &m_scalb);
    exportFn(LIB, "rinttol", &m_rinttol);
    exportFn(LIB, "roundtol", &m_roundtol);
    exportFn(LIB, "relation", &m_relation);
    exportFn(LIB, "__fpclassifyd", &m_fpclassifyd);
    exportFn(LIB, "__fpclassifyf", &m_fpclassifyf);
    exportFn(LIB, "__isnormald", &m_isnormald);
    exportFn(LIB, "__isnormalf", &m_isnormalf);
    exportFn(LIB, "__isfinited", &m_isfinited);
    exportFn(LIB, "__isfinitef", &m_isfinitef);
    exportFn(LIB, "__isnand", &m_isnand);
    exportFn(LIB, "__isnanf", &m_isnanf);
    exportFn(LIB, "__signbitd", &m_signbitd);
    exportFn(LIB, "__signbitf", &m_signbitf);
    exportRaw(LIB, "__fpclassify", [](PowerCore &cpu) { return raw_classifyL(cpu, m_fpclassifyd); });
    exportRaw(LIB, "__isnormal", [](PowerCore &cpu) { return raw_classifyL(cpu, m_isnormald); });
    exportRaw(LIB, "__isfinite", [](PowerCore &cpu) { return raw_classifyL(cpu, m_isfinited); });
    exportRaw(LIB, "__isnan", [](PowerCore &cpu) { return raw_classifyL(cpu, m_isnand); });
    exportRaw(LIB, "__signbit", [](PowerCore &cpu) { return raw_classifyL(cpu, m_signbitd); });
    exportFn(LIB, "__inf", &m_inf);
    exportFn(LIB, "nan", &m_nan);
    exportFn(LIB, "nanf", &m_nanf);
    exportRaw(LIB, "nanl", &raw_nanl);
    exportFn(LIB, "compound", &m_compound);
    exportFn(LIB, "annuity", &m_annuity);
    exportFn(LIB, "randomx", &m_randomx);

    exportFn(LIB, "feclearexcept", &c_feclearexcept);
    exportFn(LIB, "feraiseexcept", &c_feraiseexcept);
    exportFn(LIB, "fetestexcept", &c_fetestexcept);
    exportFn(LIB, "fegetexcept", &c_fegetexcept);
    exportFn(LIB, "fesetexcept", &c_fesetexcept);
    exportFn(LIB, "fegetround", &c_fegetround);
    exportFn(LIB, "fesetround", &c_fesetround);
    exportFn(LIB, "fegetenv", &c_fegetenv);
    exportFn(LIB, "feholdexcept", &c_feholdexcept);
    exportFn(LIB, "fesetenv", &c_fesetenv);
    exportFn(LIB, "feupdateenv", &c_feupdateenv);

    exportRaw(LIB, "cosl", &unaryL<l_cos>);
    exportRaw(LIB, "sinl", &unaryL<l_sin>);
    exportRaw(LIB, "tanl", &unaryL<l_tan>);
    exportRaw(LIB, "acosl", &unaryL<l_acos>);
    exportRaw(LIB, "asinl", &unaryL<l_asin>);
    exportRaw(LIB, "atanl", &unaryL<l_atan>);
    exportRaw(LIB, "coshl", &unaryL<l_cosh>);
    exportRaw(LIB, "sinhl", &unaryL<l_sinh>);
    exportRaw(LIB, "tanhl", &unaryL<l_tanh>);
    exportRaw(LIB, "acoshl", &unaryL<l_acosh>);
    exportRaw(LIB, "asinhl", &unaryL<l_asinh>);
    exportRaw(LIB, "atanhl", &unaryL<l_atanh>);
    exportRaw(LIB, "expl", &unaryL<l_exp>);
    exportRaw(LIB, "expm1l", &unaryL<l_expm1>);
    exportRaw(LIB, "exp2l", &unaryL<l_exp2>);
    exportRaw(LIB, "logl", &unaryL<l_log>);
    exportRaw(LIB, "log1pl", &unaryL<l_log1p>);
    exportRaw(LIB, "log10l", &unaryL<l_log10>);
    exportRaw(LIB, "log2l", &unaryL<l_log2>);
    exportRaw(LIB, "logbl", &unaryL<l_logb>);
    exportRaw(LIB, "sqrtl", &unaryL<l_sqrt>);
    exportRaw(LIB, "fabsl", &unaryL<l_fabs>);
    exportRaw(LIB, "erfl", &unaryL<l_erf>);
    exportRaw(LIB, "erfcl", &unaryL<l_erfc>);
    exportRaw(LIB, "gammal", &unaryL<l_gamma>);
    exportRaw(LIB, "lgammal", &unaryL<l_lgamma>);
    exportRaw(LIB, "ceill", &unaryL<l_ceil>);
    exportRaw(LIB, "floorl", &unaryL<l_floor>);
    exportRaw(LIB, "rintl", &unaryL<l_rint>);
    exportRaw(LIB, "nearbyintl", &unaryL<l_nearbyint>);
    exportRaw(LIB, "roundl", &unaryL<l_round>);
    exportRaw(LIB, "truncl", &unaryL<l_trunc>);
    exportRaw(LIB, "atan2l", &binaryL<l_atan2>);
    exportRaw(LIB, "powl", &binaryL<l_pow>);
    exportRaw(LIB, "remainderl", &binaryL<l_remainder>);
    exportRaw(LIB, "copysignl", &binaryL<l_copysign>);
    exportRaw(LIB, "nextafterl", &binaryL<l_nextafter>);
    exportRaw(LIB, "fdiml", &binaryL<l_fdim>);
    exportRaw(LIB, "fmaxl", &binaryL<l_fmax>);
    exportRaw(LIB, "fminl", &binaryL<l_fmin>);
    exportRaw(LIB, "hypotl", &binaryL<l_hypot>);
    exportRaw(LIB, "scalbl", &raw_scalbl);
    exportRaw(LIB, "ldexpl", &raw_scalbl);
    exportRaw(LIB, "frexpl", &raw_frexpl);
    exportRaw(LIB, "modfl", &raw_modfl);
    exportRaw(LIB, "remquol", &raw_remquol);
    exportRaw(LIB, "rinttoll", &raw_rinttoll);
    exportRaw(LIB, "roundtoll", &raw_roundtoll);
    exportRaw(LIB, "relationl", &raw_relationl);

    exportFn(LIB, "x80tod", &m_x80tod);
    exportFn(LIB, "dtox80", &m_dtox80);
    exportRaw(LIB, "x80told", &raw_x80told);
    exportFn(LIB, "ldtox80", &m_ldtox80);

    exportFn(LIB, "num2dec", &m_num2dec);
    exportRaw(LIB, "num2decl", &raw_num2decl);
    exportFn(LIB, "dec2num", &m_dec2num);
    exportRaw(LIB, "dec2numl", &raw_dec2numl);
    exportFn(LIB, "dec2f", &m_dec2f);
    exportFn(LIB, "dec2s", &m_dec2s);
    exportFn(LIB, "dec2l", &m_dec2l);
    exportFn(LIB, "str2dec", &m_str2dec);
    exportFn(LIB, "dec2str", &m_dec2str);
}
