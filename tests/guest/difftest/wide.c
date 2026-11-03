/* wide: InterfaceLib's 64-bit arithmetic (FixMath.h's wide routines),
   differential (tools/difftest/run.py wide): WideAdd, WideSubtract,
   WideCompare, WideNegate, WideShift, WideBitShift, WideSquareRoot,
   WideMultiply, WideDivide and WideWideDivide on edge values, each result
   and what the call wrote. Photoshop 4.0 sizes its images with them;
   Executor lacked all ten (unresolved imports, answering 0). */
#include "probe.h"

typedef struct { long hi; unsigned long lo; } W;

typedef W *(*WW)(W *, const W *);
typedef short (*WCmp)(const W *, const W *);
typedef W *(*WNeg)(W *);
typedef W *(*WSh)(W *, long);
typedef unsigned long (*WSqrt)(const W *);
typedef W *(*WMul)(long, long, W *);
typedef long (*WDiv)(const W *, long, long *);
typedef W *(*WWDiv)(W *, long, long *);

static const W values[] = {
    { 0, 0 }, { 0, 1 }, { -1, 0xFFFFFFFF }, { 0, 0xFFFFFFFF }, { 1, 0 },
    { 0x7FFFFFFF, 0xFFFFFFFF }, { (long)0x80000000, 0 }, { 0x12345678, 0x9ABCDEF0 },
    { -2, 0x80000000 }, { 0, 0x7FFFFFFF }, { -1, 0x80000000 }, { 0x3FFFFFFF, 0 },
    { (long)0xEDCBA987, 0x65432110 }, { 0, 100 }, { -1, (unsigned long)-100 },
};
#define NV (sizeof values / sizeof values[0])
static const long longs[] = { 0, 1, -1, 2, 3, 7, -7, 1000, 65536, 0x7FFFFFFF, (long)0x80000000, -65536 };
#define NL (sizeof longs / sizeof longs[0])
static const long shifts[] = { -65, -64, -63, -33, -32, -31, -1, 0, 1, 2, 31, 32, 33, 62, 63, 64, 65 };
#define NS (sizeof shifts / sizeof shifts[0])

static void out_w(const char *key, const W *w, const void *ret, const void *target)
{
    out(key, "%08lx %08lx%s", (unsigned long)w->hi, w->lo,
        ret == target ? "" : ret ? " (returned another pointer)" : " (returned nil)");
}

int main(void)
{
    WW add = (WW)probe_bind("InterfaceLib", "WideAdd");
    WW sub = (WW)probe_bind("InterfaceLib", "WideSubtract");
    WCmp cmp = (WCmp)probe_bind("InterfaceLib", "WideCompare");
    WNeg neg = (WNeg)probe_bind("InterfaceLib", "WideNegate");
    WSh shift = (WSh)probe_bind("InterfaceLib", "WideShift");
    WSh bitshift = (WSh)probe_bind("InterfaceLib", "WideBitShift");
    WSqrt wsqrt = (WSqrt)probe_bind("InterfaceLib", "WideSquareRoot");
    WMul mul = (WMul)probe_bind("InterfaceLib", "WideMultiply");
    WDiv div = (WDiv)probe_bind("InterfaceLib", "WideDivide");
    WWDiv wdiv = (WWDiv)probe_bind("InterfaceLib", "WideWideDivide");
    char k[64];
    unsigned i, j;
    W t;
    void *r;

    probe_open("wide");
    if(SECTION("add") && add && sub && cmp)
        for(i = 0; i < NV; i++)
            for(j = 0; j < NV; j++)
            {
                t = values[i];
                r = add(&t, &values[j]);
                sprintf(k, "add.%u.%u", i, j);
                out_w(k, &t, r, &t);
                t = values[i];
                r = sub(&t, &values[j]);
                sprintf(k, "sub.%u.%u", i, j);
                out_w(k, &t, r, &t);
                sprintf(k, "cmp.%u.%u", i, j);
                out(k, "%d", cmp(&values[i], &values[j]));
            }
    if(SECTION("negate") && neg && wsqrt)
        for(i = 0; i < NV; i++)
        {
            t = values[i];
            r = neg(&t);
            sprintf(k, "neg.%u", i);
            out_w(k, &t, r, &t);
            sprintf(k, "sqrt.%u", i);
            out(k, "%08lx", wsqrt(&values[i]));
        }
    if(SECTION("shift") && shift && bitshift)
        for(i = 0; i < NV; i++)
            for(j = 0; j < NS; j++)
            {
                t = values[i];
                r = shift(&t, shifts[j]);
                sprintf(k, "shift.%u.%ld", i, shifts[j]);
                out_w(k, &t, r, &t);
                t = values[i];
                r = bitshift(&t, shifts[j]);
                sprintf(k, "bitshift.%u.%ld", i, shifts[j]);
                out_w(k, &t, r, &t);
            }
    if(SECTION("multiply") && mul)
        for(i = 0; i < NL; i++)
            for(j = 0; j < NL; j++)
            {
                memset(&t, SENTINEL, sizeof t);
                r = mul(longs[i], longs[j], &t);
                sprintf(k, "mul.%ld.%ld", longs[i], longs[j]);
                out_w(k, &t, r, &t);
            }
    if(SECTION("divide") && div && wdiv)
        for(i = 0; i < NV; i++)
            for(j = 1; j < NL; j++) /* not by 0 here: its own section */
            {
                long rem, q;
                memset(&rem, SENTINEL, sizeof rem);
                q = div(&values[i], longs[j], &rem);
                sprintf(k, "div.%u.%ld", i, longs[j]);
                out(k, "%08lx rem %08lx", (unsigned long)q, (unsigned long)rem);
                t = values[i];
                memset(&rem, SENTINEL, sizeof rem);
                r = wdiv(&t, longs[j], &rem);
                sprintf(k, "wdiv.%u.%ld", i, longs[j]);
                out(k, "%08lx %08lx rem %08lx%s", (unsigned long)t.hi, t.lo, (unsigned long)rem,
                    r == &t ? "" : " (returned another pointer)");
            }
    if(SECTION("noremainder") && div && wdiv)
        for(i = 0; i < NV; i++)
        {
            /* nil and -1 for the remainder (FixMath.h: no remainder wanted) */
            sprintf(k, "div.nil.%u", i);
            out(k, "%08lx", (unsigned long)div(&values[i], 7, 0));
            sprintf(k, "div.minus1.%u", i);
            out(k, "%08lx", (unsigned long)div(&values[i], 7, (long *)-1));
            t = values[i];
            wdiv(&t, 7, 0);
            sprintf(k, "wdiv.nil.%u", i);
            out(k, "%08lx %08lx", (unsigned long)t.hi, t.lo);
        }
    if(SECTION("byzero") && div && wdiv)
        for(i = 0; i < NV; i++)
        {
            long rem;
            memset(&rem, SENTINEL, sizeof rem);
            sprintf(k, "div.0.%u", i);
            out(k, "%08lx rem %08lx", (unsigned long)div(&values[i], 0, &rem), (unsigned long)rem);
            t = values[i];
            memset(&rem, SENTINEL, sizeof rem);
            wdiv(&t, 0, &rem);
            sprintf(k, "wdiv.0.%u", i);
            out(k, "%08lx %08lx rem %08lx", (unsigned long)t.hi, t.lo, (unsigned long)rem);
        }
    probe_close();
    return 0;
}
