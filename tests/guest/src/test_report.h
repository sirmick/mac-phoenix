/*
 * test_report.h - Test result reporting for guest test suite
 *
 * Writes PASS/FAIL/SKIP lines to a results file on the ExtFS "Host" volume.
 * Host-side runner reads the file directly from the shared folder.
 */
#ifndef TEST_REPORT_H
#define TEST_REPORT_H

#include <Files.h>
#include <Script.h>
#include <stdio.h>
#include <string.h>

#define MAX_RESULTS 64
#define RESULT_LINE_MAX 128

static short gResultRefNum = 0;
static int gPassCount = 0;
static int gFailCount = 0;
static int gSkipCount = 0;

static void report_init(void)
{
    FSSpec spec;
    OSErr err;

    /* Create/overwrite results file on Host (ExtFS) volume */
    err = FSMakeFSSpec(0, 0, "\pHost:test_results.txt", &spec);
    if (err == fnfErr) {
        FSpCreate(&spec, 'ttxt', 'TEXT', smSystemScript);
    } else if (err != noErr) {
        /* Fall back to boot volume */
        FSMakeFSSpec(0, 0, "\ptest_results.txt", &spec);
        FSpCreate(&spec, 'ttxt', 'TEXT', smSystemScript);
    }
    FSpOpenDF(&spec, fsRdWrPerm, &gResultRefNum);

    /* Truncate */
    SetEOF(gResultRefNum, 0);
}

static void report_write_line(const char *status, const char *name, const char *detail)
{
    char line[RESULT_LINE_MAX];
    long count;

    if (detail && detail[0]) {
        count = snprintf(line, sizeof(line), "%s %s %s\r", status, name, detail);
    } else {
        count = snprintf(line, sizeof(line), "%s %s\r", status, name);
    }
    if (count > 0) {
        FSWrite(gResultRefNum, &count, line);
    }
}

static void report_pass(const char *name)
{
    report_write_line("PASS", name, NULL);
    gPassCount++;
}

static void report_fail(const char *name, OSErr err)
{
    char detail[32];
    snprintf(detail, sizeof(detail), "err=%d", (int)err);
    report_write_line("FAIL", name, detail);
    gFailCount++;
}

static void report_skip(const char *name, const char *reason)
{
    report_write_line("SKIP", name, reason);
    gSkipCount++;
}

static void report_finish(void)
{
    char summary[RESULT_LINE_MAX];
    long count;

    count = snprintf(summary, sizeof(summary),
        "---\r%d passed, %d failed, %d skipped\r",
        gPassCount, gFailCount, gSkipCount);
    if (count > 0) {
        FSWrite(gResultRefNum, &count, summary);
    }
    FSClose(gResultRefNum);
    FlushVol(NULL, 0);
}

#endif /* TEST_REPORT_H */
