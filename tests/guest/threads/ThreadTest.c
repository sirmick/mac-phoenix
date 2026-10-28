/*
 * ThreadTest.c -- the Thread Manager from PowerPC code (ThreadsLib):
 * cooperative threads made, yielding to each other and to the main thread,
 * switcher and termination procedures, results, disposal. On Executor the
 * threads are coroutines whose entries and procedures are PowerPC code
 * (process.cpp); on the G3, Apple's. One line per check, "PASS name" or
 * "FAIL name: why"; then "---", in Host:thread_results.txt
 * (tests/test_guest_suite.sh --results thread_results.txt). Built with
 * Retro68 by build-retro68.sh.
 */

#include <Types.h>
#include <Threads.h>
#include <stdio.h>
#include <string.h>

/* The Multiversal Interfaces' Thread Manager: IDs are longs, routines plain
   ProcPtrs (on PowerPC, transition vectors: what the 8.6 Finder passes). */
typedef LONGINT ThreadID;
#define YieldToAnyThread() YieldToThread(kNoThreadID)

static int failures;
static long counter[3];
static char trace[256];
static int switches_in, switches_out, terminated;

static void check(const char *name, int ok, const char *why, long code)
{
    if(ok)
        printf("PASS %s\n", name);
    else
    {
        printf("FAIL %s: %s (%ld)\n", name, why, code);
        failures++;
    }
}

static void note(char c)
{
    size_t n = strlen(trace);
    if(n + 1 < sizeof trace)
    {
        trace[n] = c;
        trace[n + 1] = 0;
    }
}

static pascal void *worker(void *param)
{
    long which = (long)param;
    int i;
    for(i = 0; i < 3; i++)
    {
        counter[which]++;
        note('a' + which);
        YieldToAnyThread();
    }
    return (void *)(which * 100 + 7);
}

static pascal void switched_in(ThreadID t, void *param) { switches_in++; }
static pascal void switched_out(ThreadID t, void *param) { switches_out++; }
static pascal void terminator(ThreadID t, void *param) { terminated++; }

int main(void)
{
    long have = 0;
    ThreadID t1 = 0, t2 = 0, self = 0;
    LONGINT r1 = 0, r2 = 0;
    OSErr err;
    int spins;

    freopen("Host:thread_results.txt", "w", stdout);
    err = GetCurrentThread(&self);
    check("current_thread", err == noErr && self != 0, "GetCurrentThread", err);

    err = NewThread(kCooperativeThread, (ProcPtr)worker, 1, 0, kCreateIfNeeded, &r1, &t1);
    check("new_thread_1", err == noErr && t1 != 0, "NewThread", err);
    err = NewThread(kCooperativeThread, (ProcPtr)worker, 2, 0, kCreateIfNeeded, &r2, &t2);
    check("new_thread_2", err == noErr && t2 != 0, "NewThread", err);
    SetThreadSwitcher(t1, (ProcPtr)switched_in, 0, true);
    SetThreadSwitcher(t1, (ProcPtr)switched_out, 0, false);
    SetThreadTerminator(t2, (ProcPtr)terminator, 0);

    for(spins = 0; spins < 20 && (counter[1] < 3 || counter[2] < 3); spins++)
    {
        note('m');
        YieldToAnyThread();
    }
    /* let them return */
    for(spins = 0; spins < 5; spins++)
        YieldToAnyThread();
    printf("# order %s\n", trace);
    check("ran_1", counter[1] == 3, "thread 1 didn't run three times", counter[1]);
    check("ran_2", counter[2] == 3, "thread 2 didn't run three times", counter[2]);
    check("interleaved", strstr(trace, "ab") || strstr(trace, "ba") || strstr(trace, "mb") || strstr(trace, "am"),
          "no switch between threads", 0);
    check("result_1", r1 == 107, "thread 1's result", r1);
    check("result_2", r2 == 207, "thread 2's result", r2);
    check("switcher_in", switches_in >= 3, "switch-in procedure", switches_in);
    check("switcher_out", switches_out >= 3, "switch-out procedure", switches_out);
    check("terminator", terminated == 1, "termination procedure", terminated);
    printf("DONE %d\n---\n", failures);
    fclose(stdout);
    return 0;
}
