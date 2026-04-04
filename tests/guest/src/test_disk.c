/*
 * test_disk.c - Disk I/O tests on the boot volume
 *
 * Tests: volume info, create/write/read/delete on boot volume.
 */
#include <Files.h>
#include <string.h>

#include "test_report.h"

void test_disk(void)
{
    HParamBlockRec vpb;
    Str255 volName;
    OSErr err;
    FSSpec spec;
    short refNum;
    long count;
    char writeBuf[] = "disk io test data";
    char readBuf[64];

    /* Get boot volume info */
    memset(&vpb, 0, sizeof(vpb));
    vpb.volumeParam.ioNamePtr = volName;
    vpb.volumeParam.ioVRefNum = 0;
    vpb.volumeParam.ioVolIndex = 1;
    err = PBHGetVInfoSync(&vpb);
    if (err != noErr) {
        report_fail("disk_volume_info", err);
        return;
    }
    report_pass("disk_volume_info");

    /* Create temp file on boot volume */
    err = FSMakeFSSpec(vpb.volumeParam.ioVRefNum, fsRtDirID,
                       "\p_test_scratch.tmp", &spec);
    if (err != noErr && err != fnfErr) {
        report_fail("disk_create", err);
        return;
    }
    FSpDelete(&spec);
    err = FSpCreate(&spec, 'ttxt', 'TEXT', smSystemScript);
    if (err != noErr) {
        report_fail("disk_create", err);
        return;
    }
    report_pass("disk_create");

    /* Write */
    err = FSpOpenDF(&spec, fsRdWrPerm, &refNum);
    if (err != noErr) {
        report_fail("disk_write", err);
        FSpDelete(&spec);
        return;
    }
    count = sizeof(writeBuf) - 1;
    err = FSWrite(refNum, &count, writeBuf);
    FSClose(refNum);
    if (err != noErr) {
        report_fail("disk_write", err);
        FSpDelete(&spec);
        return;
    }
    report_pass("disk_write");

    /* Read back */
    err = FSpOpenDF(&spec, fsRdPerm, &refNum);
    if (err != noErr) {
        report_fail("disk_read", err);
        FSpDelete(&spec);
        return;
    }
    count = sizeof(readBuf);
    memset(readBuf, 0, sizeof(readBuf));
    err = FSRead(refNum, &count, readBuf);
    FSClose(refNum);
    if (err != noErr) {
        report_fail("disk_read", err);
        FSpDelete(&spec);
        return;
    }
    report_pass("disk_read");

    /* Verify */
    if (count == (long)(sizeof(writeBuf) - 1) &&
        memcmp(readBuf, writeBuf, count) == 0) {
        report_pass("disk_roundtrip");
    } else {
        report_fail("disk_roundtrip", -1);
    }

    /* Cleanup */
    FSpDelete(&spec);
}
