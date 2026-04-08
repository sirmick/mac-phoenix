/*
 * test_extfs.c - ExtFS file I/O tests
 *
 * Tests: create file, write data, read back, compare, delete.
 * All operations target the "Host" ExtFS volume.
 */
#include <Files.h>
#include <Script.h>
#include <string.h>

#include "test_report.h"

#define TEST_DATA "MacPhoenix ExtFS round-trip test 1234567890"
#define TEST_DATA_LEN 43

void test_extfs(void)
{
    FSSpec spec;
    short refNum;
    OSErr err;
    char buf[128];
    long count;

    /* Create test file */
    err = FSMakeFSSpec(0, 0, "\pHost:_test_scratch.txt", &spec);
    if (err != noErr && err != fnfErr) {
        report_fail("extfs_create", err);
        return;
    }

    FSpDelete(&spec);  /* remove if leftover */
    err = FSpCreate(&spec, 'ttxt', 'TEXT', smSystemScript);
    if (err != noErr) {
        report_fail("extfs_create", err);
        return;
    }
    report_pass("extfs_create");

    /* Write + read back (same handle, seek to start for read) */
    err = FSpOpenDF(&spec, fsRdWrPerm, &refNum);
    if (err != noErr) {
        report_fail("extfs_write", err);
        FSpDelete(&spec);
        return;
    }
    count = TEST_DATA_LEN;
    err = FSWrite(refNum, &count, TEST_DATA);
    if (err != noErr || count != TEST_DATA_LEN) {
        report_fail("extfs_write", err);
        FSClose(refNum);
        FSpDelete(&spec);
        return;
    }
    report_pass("extfs_write");

    /* Seek back to start and read */
    SetFPos(refNum, fsFromStart, 0);
    count = sizeof(buf);
    memset(buf, 0, sizeof(buf));
    err = FSRead(refNum, &count, buf);
    FSClose(refNum);
    /* eofErr is expected — we asked for more bytes than the file contains */
    if (err != noErr && err != eofErr) {
        report_fail("extfs_read", err);
        FSpDelete(&spec);
        return;
    }
    report_pass("extfs_read");

    /* Compare */
    if (count == TEST_DATA_LEN && memcmp(buf, TEST_DATA, TEST_DATA_LEN) == 0) {
        report_pass("extfs_roundtrip");
    } else {
        report_fail("extfs_roundtrip", -1);
    }

    /* Delete */
    err = FSpDelete(&spec);
    if (err == noErr) {
        report_pass("extfs_delete");
    } else {
        report_fail("extfs_delete", err);
    }
}
