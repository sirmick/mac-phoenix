/* main.c — MacPhoenixBench: the Toolbox, the clocks, the runs and the
 * results file (bench.h). */

#include "bench.h"

#include <Dialogs.h>
#include <Events.h>
#include <Files.h>
#include <Fonts.h>
#include <Gestalt.h>
#include <Menus.h>
#include <MixedMode.h>
#include <OSUtils.h>
#include <Processes.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <Timer.h>
#include <Traps.h>
#include <Windows.h>
#include <stdio.h>
#include <string.h>

#pragma segment Main

QDGlobals qd;

short bench_vref;
long bench_dirid;

enum { kVersion = 1, kMaxTests = 80, kMaxRuns = 25 };

/* ---- the host's clock: the bridge trap's HOST_CLOCK (guest_bridge.h) ---- */

enum { kBridgeTrap = 0xABE5, kHostClock = 0x19 };

#if !BENCH_PPC
#pragma parameter __D0 BridgeCall(__D0, __A0, __D1)
long BridgeCall(long selector, const void *ptr, long value) ONEWORDINLINE(0xABE5);
#else
/* PowerPC: the trap's 68k code through Mixed Mode, register-based. */
static UniversalProcPtr bridge_entry;
static long BridgeCall(long selector, const void *ptr, long value)
{
    enum
    {
        kInfo = kRegisterBased | RESULT_SIZE(SIZE_CODE(4)) | REGISTER_RESULT_LOCATION(kRegisterD0)
            | REGISTER_ROUTINE_PARAMETER(1, kRegisterD0, SIZE_CODE(4))
            | REGISTER_ROUTINE_PARAMETER(2, kRegisterA0, SIZE_CODE(4))
            | REGISTER_ROUTINE_PARAMETER(3, kRegisterD1, SIZE_CODE(4))
    };
    return CallUniversalProc(bridge_entry, kInfo, selector, ptr, value);
}
#endif

static int have_host;

static void host_clock_init(void)
{
    UniversalProcPtr trap = NGetTrapAddress(kBridgeTrap, ToolTrap);

    /* The bridge trap is there unless it is still _Unimplemented. */
    have_host = trap != NGetTrapAddress(_Unimplemented, ToolTrap);
#if BENCH_PPC
    bridge_entry = trap;
#endif
}

static unsigned long host_us(void)
{
    return have_host ? (unsigned long)BridgeCall(kHostClock, NULL, 0) : 0;
}

static unsigned long micro_us(void)
{
    UnsignedWide w;

    Microseconds(&w);
    return w.lo;                /* differences, modulo 2^32: up to 71 minutes */
}

/* ---- the results ---- */

static FILE *results;

/* Each line on the disk at once (FlushVol; outside the timing): a test
 * that crashes the application keeps the lines before it. */
static void flush_results(void)
{
    fflush(results);
    FlushVol(NULL, bench_vref);
}

void bench_note(const char *text)
{
    fprintf(results, "# %s\n", text);
    flush_results();
}

static char skip_why[80];

void bench_skip(const char *why)
{
    strncpy(skip_why, why, sizeof skip_why - 1);
}

int on_powerpc(void)
{
    long arch;

    return Gestalt(gestaltSysArchitecture, &arch) == noErr && arch == gestaltPowerPC;
}

int have_fpu(void)
{
    long fpu;

    return Gestalt(gestaltFPUType, &fpu) == noErr && fpu != gestaltNoFPU;
}

/* ---- the tests ---- */

static struct
{
    const char *name, *work;
    BenchProc proc, setup, done;
} tests[kMaxTests];
static int ntests;

void bench_add(const char *name, const char *work, BenchProc proc, BenchProc setup, BenchProc done)
{
    if(ntests < kMaxTests)
    {
        tests[ntests].name = name;
        tests[ntests].work = work;
        tests[ntests].proc = proc;
        tests[ntests].setup = setup;
        tests[ntests].done = done;
        ntests++;
    }
}

static void idle(void)
{
    EventRecord ev;

    WaitNextEvent(everyEvent & ~keyDownMask & ~autoKeyMask, &ev, 0, NULL);
}

static void sort(unsigned long *v, int n)
{
    int i, j;
    unsigned long t;

    for(i = 1; i < n; i++)
        for(j = i; j > 0 && v[j - 1] > v[j]; j--)
            t = v[j], v[j] = v[j - 1], v[j - 1] = t;
}

static unsigned long median(unsigned long *v, int n)
{
    sort(v, n);
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

volatile long bench_sink;

static void run_test(int t, int runs)
{
    unsigned long mic[kMaxRuns], host[kMaxRuns];
    unsigned long m0, h0, med;
    int r;

    skip_why[0] = 0;
    if(tests[t].setup && tests[t].setup())
    {
        fprintf(results, "SKIP %s %s\n", tests[t].name, skip_why[0] ? skip_why : "-");
        flush_results();
        return;
    }
    for(r = 0; r < runs; r++)
    {
        idle();
        h0 = host_us();
        m0 = micro_us();
        bench_sink += tests[t].proc();
        mic[r] = micro_us() - m0;
        host[r] = host_us() - h0;
    }
    if(tests[t].done)
        tests[t].done();
    /* TEST name runs min median host-min host-median work (µs; median()
     * sorts, so [0] is the least) */
    med = median(mic, runs);
    fprintf(results, "TEST %s %d %lu %lu", tests[t].name, runs, mic[0], med);
    if(have_host)
    {
        med = median(host, runs);
        fprintf(results, " %lu %lu", host[0], med);
    }
    else
        fprintf(results, " - -");
    fprintf(results, " %s\n", tests[t].work);
    flush_results();
}

/* The clocks against each other over two seconds of TickCount: CLOCK
 * ticks-µs Microseconds-µs host-µs, and Microseconds' resolution (the
 * smallest step seen). */
static void clock_check(int ticks, int steps)
{
    unsigned long t, m0, m1, h0, h1, step = 0, a, b;
    int i;

    if(!ticks)
        goto resolution;

    /* Busy, no event calls: two seconds of WaitNextEvent lose the session
     * in the tab on 8.6 (docs/executor/Speed.md); 4 s of Microseconds at
     * most, in case ticks don't come without one. */
    t = TickCount();
    m0 = micro_us();
    while(TickCount() == t && micro_us() - m0 < 4000000ul)
        ;
    t = TickCount();
    m0 = micro_us();
    h0 = host_us();
    while(TickCount() < t + 120 && micro_us() - m0 < 4000000ul)
        ;
    m1 = micro_us();
    h1 = host_us();
    fprintf(results, "CLOCK %lu %lu %s", 2000000ul, m1 - m0, have_host ? "" : "-\n");
    if(have_host)
        fprintf(results, "%lu\n", h1 - h0);
    flush_results();
resolution:
    if(!steps)
        return;
    for(i = 0; i < 2000; i++)
    {
        a = micro_us();
        while((b = micro_us()) == a)
            ;
        if(!step || b - a < step)
            step = b - a;
        if(step <= 1)
            break;
    }
    fprintf(results, "# Microseconds steps by %lu us at the least\n", step);
    flush_results();
}

/* A test's name starts with a word of the filter ("copybits" matches
 * "copybits_8_to"); an empty filter: every test. */
static int wanted(const char *name, const char *filter)
{
    char buf[256], *w;

    if(!filter[0])
        return 1;
    strncpy(buf, filter, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    for(w = strtok(buf, " "); w; w = strtok(NULL, " "))
        if(strncmp(name, w, strlen(w)) == 0)
            return 1;
    return 0;
}

static void header(int runs)
{
    long mach = 0, sysv = 0, fpu = 0, qdv = 0, ram = 0;
    char line[160];

    Gestalt(gestaltMachineType, &mach);
    Gestalt(gestaltSystemVersion, &sysv);
    Gestalt(gestaltFPUType, &fpu);
    Gestalt(gestaltQuickdrawVersion, &qdv);
    Gestalt(gestaltPhysicalRAMSize, &ram);
    fprintf(results, "BENCH MacPhoenixBench %d\n", kVersion);
    fprintf(results, "CODE %s\n", BENCH_PPC ? "ppc" : "68k");
    sprintf(line, "machine %ld, system %lx, %s Mac, fpu %ld, QuickDraw %lx, RAM %ld MB, screen depth %d",
            mach, sysv, on_powerpc() ? "PowerPC" : "68k", fpu, qdv, ram >> 20,
            (*(*GetMainDevice())->gdPMap)->pixelSize);
    bench_note(line);
    fprintf(results, "RUNS %d\nHOST %s\n", runs, have_host ? "yes" : "no");
    flush_results();
}

int main(void)
{
    char filter[256] = "";
    char line[64];
    int runs = 5, t;
    FILE *cfg;
    ProcessSerialNumber psn = { 0, kCurrentProcess };
    ProcessInfoRec info;
    FSSpec app;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    MaxApplZone();
    for(t = 0; t < 30; t++)             /* come to the front */
        idle();

    info.processInfoLength = sizeof info;
    info.processName = NULL;
    info.processAppSpec = &app;
    if(GetProcessInformation(&psn, &info) != noErr)
        return 1;
    bench_vref = app.vRefNum;
    bench_dirid = app.parID;
    HSetVol(NULL, bench_vref, bench_dirid);

    if((cfg = fopen("bench.cfg", "r")) != NULL)
    {
        if(fgets(line, sizeof line, cfg) && sscanf(line, "%d", &t) == 1 && t > 0)
            runs = t < kMaxRuns ? t : kMaxRuns;
        if(fgets(filter, sizeof filter, cfg))
            filter[strcspn(filter, "\r\n")] = 0;
        fclose(cfg);
    }
    if((results = fopen("bench-results.txt", "w")) == NULL)
        return 1;
    host_clock_init();
    header(runs);
    if(wanted("clock", filter) || wanted("clock_ticks", filter) || wanted("clock_steps", filter))
        clock_check(wanted("clock_ticks", filter), wanted("clock_steps", filter));

    add_cpu_tests();
    add_fp_tests();
#if !BENCH_PPC
    add_fp_tests_881();
#endif
    add_mem_tests();
    add_qd_tests();
    add_trap_tests();
    add_file_tests();
    add_heap_tests();

    for(t = 0; t < ntests; t++)
        if(wanted(tests[t].name, filter))
        {
            HSetVol(NULL, bench_vref, bench_dirid);
            run_test(t, runs);
        }

    fprintf(results, "---\nDONE\n");
    fclose(results);
    FlushVol(NULL, bench_vref);         /* on the disk now: run.py reads the image */
    return 0;
}
