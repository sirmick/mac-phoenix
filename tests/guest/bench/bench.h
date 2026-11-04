/* bench.h — MacPhoenixBench, our own benchmark (docs/executor/Speed.md,
 * "Our own benchmark"): a fat application (68k and PowerPC code; build.mpw)
 * that runs a fixed amount of work per test, each test RUNS times, timed by
 * Microseconds and by the host's clock through the bridge trap (HOST_CLOCK,
 * guest_bridge.h), and writes one line per test to bench-results.txt in its
 * own folder, then quits. tools/bench/run.py launches it and reads the file.
 *
 * bench.cfg in its folder, if there: the number of runs (first line), the
 * tests to run (second line: names or prefixes, separated by spaces; empty:
 * all). */

#ifndef BENCH_H
#define BENCH_H

#include <ConditionalMacros.h>
#include <Types.h>

/* 1 in the PowerPC build (MPW's Universal Interfaces 2.x: GENERATINGPOWERPC) */
#if defined(__powerc) || defined(powerc) || (defined(GENERATINGPOWERPC) && GENERATINGPOWERPC)
#define BENCH_PPC 1
#else
#define BENCH_PPC 0
#endif

/* A test: does its work once; returns something computed from it (so the
 * compiler keeps the work). */
typedef long (*BenchProc)(void);

/* Each file registers its tests (main.c runs them in this order). Set-up
 * outside the timing: SETUP before the runs, DONE after; 0 if nothing. A
 * setup that returns nonzero skips the test (the reason with skip()). */
void bench_add(const char *name, const char *work, BenchProc proc,
               BenchProc setup, BenchProc done);

/* A note in the results (a "# text" line), and a reason a test is skipped. */
void bench_note(const char *text);
void bench_skip(const char *why);

/* 1 if this is a PowerPC Mac (whatever this code is compiled for); 1 if
 * there is a floating-point unit. */
int on_powerpc(void);
int have_fpu(void);

void add_cpu_tests(void);
void add_fp_tests(void);        /* PowerPC; 68k with SANE */
void add_fp_tests_881(void);    /* 68k with 68881 code (fp.c built again) */
void add_mem_tests(void);
void add_qd_tests(void);
void add_trap_tests(void);
void add_file_tests(void);
void add_heap_tests(void);

/* The volume and folder the application started in (the results go there;
 * the file tests use it as the second volume). */
extern short bench_vref;
extern long bench_dirid;

#endif
