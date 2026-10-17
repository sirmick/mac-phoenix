/* files.c — the File Manager on the boot disk and on the Host volume
 * (ExtFS): create, write, read back, delete; and that HFS won't delete an
 * open file (fBsyErr, -47: SimpleText's save relies on it). */

#include "suite.h"

#include <Errors.h>
#include <Files.h>
#include <Folders.h>
#include <stdio.h>
#include <string.h>

static void pstr(Str255 out, const char *s)
{
    size_t n = strlen(s);

    out[0] = (unsigned char)n;
    memcpy(out + 1, s, n);
}

static void fail_err(const char *name, OSErr err)
{
    char detail[32];

    sprintf(detail, "err=%d", err);
    report_fail(name, detail);
}

/* One round trip on (vRefNum, dirID, name); test names start with
 * prefix. check_busy: also try to delete the file while it is open. */
static void roundtrip(const char *prefix, short vRefNum, long dirID, const char *cname,
                      const char *data, int check_busy)
{
    Str255 name;
    char test[48], buf[128];
    short ref;
    long len;
    OSErr err;

    pstr(name, cname);
    HDelete(vRefNum, dirID, name);

    sprintf(test, "%s_create", prefix);
    if((err = HCreate(vRefNum, dirID, name, 'ttxt', 'TEXT')) != noErr)
    {
        fail_err(test, err);
        return;
    }
    report_pass(test);

    sprintf(test, "%s_write", prefix);
    len = strlen(data);
    if((err = HOpenDF(vRefNum, dirID, name, fsRdWrPerm, &ref)) == noErr)
    {
        err = FSWrite(ref, &len, data);
        FSClose(ref);
    }
    if(err != noErr)
        fail_err(test, err);
    else
        report_pass(test);

    sprintf(test, "%s_read", prefix);
    len = sizeof buf - 1;
    if((err = HOpenDF(vRefNum, dirID, name, fsRdPerm, &ref)) == noErr)
    {
        err = FSRead(ref, &len, buf);
        if(err == eofErr)
            err = noErr;
        FSClose(ref);
    }
    if(err != noErr)
        fail_err(test, err);
    else
    {
        report_pass(test);
        buf[len] = 0;
        sprintf(test, "%s_roundtrip", prefix);
        if(strcmp(buf, data) == 0)
            report_pass(test);
        else
            report_fail(test, "mismatch");
    }

    if(check_busy)
    {
        sprintf(test, "%s_delete_open", prefix);
        if((err = HOpenDF(vRefNum, dirID, name, fsRdPerm, &ref)) != noErr)
            fail_err(test, err);
        else
        {
            err = HDelete(vRefNum, dirID, name);
            FSClose(ref);
            if(err == fBsyErr)
                report_pass(test);
            else if(err == noErr)
                report_fail(test, "deleted while open");
            else
            {
                sprintf(buf, "err=%d, want -47", err);
                report_fail(test, buf);
            }
        }
    }

    err = HDelete(vRefNum, dirID, name);
    if(!check_busy)
    {
        sprintf(test, "%s_delete", prefix);
        if(err == noErr)
            report_pass(test);
        else
            fail_err(test, err);
    }
}

void test_disk(void)
{
    short vRefNum;
    long dirID;
    OSErr err;

    if((err = FindFolder(kOnSystemDisk, kSystemFolderType, kDontCreateFolder, &vRefNum, &dirID)) != noErr)
    {
        fail_err("disk_reachable", err);
        return;
    }
    report_pass("disk_reachable");
    roundtrip("disk", vRefNum, fsRtDirID, "_test_scratch.tmp", "disk io test data", 1);
}

void test_extfs(void)
{
    roundtrip("extfs", 0, 0, "Host:_test_scratch.txt", "MacPhoenix ExtFS round-trip test 1234567890", 0);
}
