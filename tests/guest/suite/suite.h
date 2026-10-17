/* suite.h — MacPhoenixSuite, the guest-side test suite (a fat application
 * built by tools/mpw/mpw; see build.mpw).
 *
 * Each section reports PASS/FAIL/SKIP lines to a results file on the Host
 * volume, the bridge's shared folder, which tests/test_guest_suite.sh
 * reads. Host:suite.cfg, if there, names the results file (first line) and
 * the sections to run (second line, separated by spaces). */

#ifndef SUITE_H
#define SUITE_H

#include <ConditionalMacros.h>
#include <Types.h>

void report_pass(const char *name);
void report_fail(const char *name, const char *detail);
void report_skip(const char *name, const char *reason);

/* Waits for events (the app stays responsive and in front). */
void idle_ticks(long ticks);

/* 1 on a PowerPC Mac (whatever this code is compiled for). */
int on_powerpc(void);

/* The sections (one file each). */
void test_disk(void);
void test_extfs(void);
void test_audio(void);
void test_applescript_dates(void);
void test_network(void);
void test_keyboard(void);

#endif
