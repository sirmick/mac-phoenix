/* traps.c — Toolbox call overhead: 100,000 calls each of TickCount,
 * GetPort + SetPort and Gestalt; a plain function call for scale; from
 * PowerPC, a Mixed Mode switch to a 68k routine (rts) 10,000 times. */

#include "bench.h"

#include <Events.h>
#include <Gestalt.h>
#include <MixedMode.h>
#include <Quickdraw.h>

#pragma segment Traps

enum { kCalls = 100000 };

static long t_tickcount(void)
{
    long i, sum = 0;

    for(i = 0; i < kCalls; i++)
        sum += TickCount();
    return sum;
}

static long t_getsetport(void)
{
    GrafPtr port;
    long i;

    for(i = 0; i < kCalls; i++)
    {
        GetPort(&port);
        SetPort(port);
    }
    return 0;
}

static long t_gestalt(void)
{
    long i, v, sum = 0;

    for(i = 0; i < kCalls; i++)
    {
        Gestalt(gestaltSystemVersion, &v);
        sum += v;
    }
    return sum;
}

/* a function the compiler can't inline away (called through a pointer) */
static long leaf(long x)
{
    return x + 1;
}

static long (*volatile leaf_ptr)(long) = leaf;

static long t_call(void)
{
    long i, sum = 0;

    for(i = 0; i < kCalls; i++)
        sum = leaf_ptr(sum);
    return sum;
}

#if BENCH_PPC
static unsigned short rts68k[] = { 0x4E75 };

static long mixedmode(void)
{
    long i;

    for(i = 0; i < 10000; i++)
        CallUniversalProc((UniversalProcPtr)rts68k, kCStackBased);
    return 0;
}
#endif

void add_trap_tests(void)
{
    bench_add("call", "100000", t_call, NULL, NULL);
    bench_add("tickcount", "100000", t_tickcount, NULL, NULL);
    bench_add("getsetport", "100000", t_getsetport, NULL, NULL);
    bench_add("gestalt", "100000", t_gestalt, NULL, NULL);
#if BENCH_PPC
    bench_add("mixedmode", "10000", mixedmode, NULL, NULL);
#endif
}
