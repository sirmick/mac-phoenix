/* devtest.c — DevTest, a PowerPC application on StdCLib's device table
   (_addDevHandler, _getDevHandler) and the descriptor layer above it: open,
   read, write, lseek, ioctl, fcntl, faccess, unlink on files (FSYS), the
   "dev:" names (SYST, CONS) and a device of its own (TEST, slot 3); stdio on
   that device; MakeResolvedPath. MrC, PPCLink: tools/mpw/mpw build
   tests/guest/devtest. Executor's StdCLib (docs/executor/PPCLibs.md) against
   Apple's on a Power Mac: ctest devtest_executor and devtest_rom_ppc (Mac OS
   9.0.4, the reference). Results go to Host:devtest_results.txt through the
   File Manager, in the guest suite's form: PASS/FAIL lines, "---" at the
   end. The test's files are made in the application's folder and closed
   before they are deleted (Host: is the host's file system on the ROM
   machine, which deletes open files). */
#include <Types.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Files.h>
#include <Events.h>
#include <OSUtils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <ioctl.h>
#include <errno.h>

QDGlobals qd;

typedef long (*Handler)(long, long, long, long);

extern long _addDevHandler(long slot, OSType name, Handler, Handler, Handler, Handler, Handler);
extern long _getDevHandler(long slot, OSType *name, Handler *, Handler *, Handler *, Handler *, Handler *);
extern OSErr MakeResolvedPath(short vol, long dir, unsigned char *path, Boolean resolveLeaf,
                              unsigned char *buffer, Boolean *isFolder, Boolean *hadAlias,
                              Boolean *leafIsAlias);

static short out_ref;
static int failures;

static void put(const char *s)
{
    long n = strlen(s);
    FSWrite(out_ref, &n, s);
}

static void dec(char *out, long v)
{
    char tmp[16];
    int n = 0, i = 0;
    unsigned long u = v < 0 ? -(unsigned long)v : (unsigned long)v;
    do
    {
        tmp[n++] = (char)('0' + u % 10);
        u /= 10;
    } while(u);
    if(v < 0)
        out[i++] = '-';
    while(n)
        out[i++] = tmp[--n];
    out[i] = 0;
}

/* "PASS label got" or "FAIL label got (want expected)" */
static void check(const char *label, const char *got, const char *want)
{
    int ok = strcmp(got, want) == 0;
    put(ok ? "PASS " : "FAIL ");
    put(label);
    put(" [");
    put(got);
    put("]");
    if(!ok)
    {
        put(" (want [");
        put(want);
        put("])");
        failures++;
    }
    put("\n");
}

static void checkl(const char *label, long got, long want)
{
    char g[16], w[16];
    dec(g, got);
    dec(w, want);
    check(label, g, w);
}

/* A call's result and errno, "result errno"; errno is cleared before the
   calls that are to leave it alone. */
static void checkr(const char *label, long got, long want, long wantErrno)
{
    char g[40], w[40];
    dec(g, got);
    strcat(g, " ");
    dec(g + strlen(g), errno);
    dec(w, want);
    strcat(w, " ");
    dec(w + strlen(w), wantErrno);
    check(label, g, w);
}

/* ---- the TEST device ---- */

/* Claims "test:" names; reads "abc"; takes what is written; answers
   FIOINTERACTIVE, FIOBUFSIZE (77), FIOLSEEK (42) and FIODUPFD. testerr makes
   read and write fail with that result. Records what it was asked. */
static long testerr;
static long lastCmd, lastFlags, lastIoctl, closes;

static long t1(long name, long cmd, long arg, long d)
{
    (void)d;
    if(strncmp((char *)name, "test:", 5) != 0)
        return -1;
    lastCmd = cmd;
    if(cmd == 0x6400)
    {
        lastFlags = *(unsigned short *)arg;
        ((long *)arg)[2] = 0x12345678;   /* the device's word */
    }
    return 0;
}

static long t2(long iob, long b, long c, long d)
{
    (void)b; (void)c; (void)d;
    if(((long *)iob)[2] == 0x12345678)
        closes++;
    return 0;
}

static long t3(long a, long b, long c, long d)
{
    long *iob = (long *)a;
    (void)b; (void)c; (void)d;
    if(testerr)
        return testerr;
    if(iob[3] > 0)
    {
        char *p = (char *)iob[4];
        long n = iob[3] < 3 ? iob[3] : 3;
        memcpy(p, "abc", n);
        iob[3] -= n;
        iob[4] += n;
    }
    return 0;
}

static long t4(long a, long b, long c, long d)
{
    long *iob = (long *)a;
    (void)b; (void)c; (void)d;
    if(testerr)
        return testerr;
    iob[4] += iob[3];
    iob[3] = 0;
    return 0;
}

static long t5(long iob, long cmd, long arg, long d)
{
    (void)iob; (void)d;
    lastIoctl = cmd;
    if(cmd == FIOINTERACTIVE)
        return 0;
    if(cmd == FIOBUFSIZE)
    {
        *(long *)arg = 77;
        return 0;
    }
    if(cmd == FIOLSEEK)
    {
        ((long *)arg)[1] = 42;
        return 0;
    }
    if(cmd == FIODUPFD)
        return 0;
    return EINVAL;
}

/* ---- the table ---- */

static void test_table(void)
{
    static const char *const want[3] = { "FSYS", "CONS", "SYST" };
    char label[40], got[8];
    int slot;
    for(slot = 0; slot < 6; slot++)
    {
        OSType name = 0;
        Handler h[5] = { 0, 0, 0, 0, 0 };
        long r = _getDevHandler(slot, &name, &h[0], &h[1], &h[2], &h[3], &h[4]);
        strcpy(label, "_getDevHandler ");
        dec(label + strlen(label), slot);
        if(slot < 3)
        {
            memcpy(got, &name, 4);
            got[4] = 0;
            check(label, r == 0 && h[0] && h[4] ? got : "fails", want[slot]);
        }
        else
            checkl(label, r, -1);
    }
    checkl("_addDevHandler 3", _addDevHandler(3, 'TEST', t1, t2, t3, t4, t5), 3);
    checkl("_addDevHandler 5", _addDevHandler(5, 'TES5', t1, t2, t3, t4, t5), -1);
    {
        OSType name = 0;
        Handler h[5];
        long r = _getDevHandler(3, &name, &h[0], &h[1], &h[2], &h[3], &h[4]);
        checkl("_getDevHandler 3 after", r, 0);
        check("_getDevHandler 3 handlers", name == 'TEST' && h[0] == t1 && h[4] == t5 ? "TEST" : "other",
              "TEST");
    }
}

/* ---- files (FSYS) ---- */

static void test_files(void)
{
    char buf[16];
    long n;
    short refnum;
    int fd, fd2;
    FILE *f;

    errno = 0;
    fd = open("devtest.tmp", O_RDWR | O_CREAT | O_TRUNC);
    checkr("open O_CREAT", fd, 3, 0);
    checkr("write", write(fd, "hello", 5), 5, 0);
    checkr("lseek SEEK_SET", lseek(fd, 1, SEEK_SET), 1, 0);
    memset(buf, 0, sizeof buf);
    checkr("read", read(fd, buf, 3), 3, 0);
    check("read text", buf, "ell");
    checkr("lseek SEEK_END", lseek(fd, -2, SEEK_END), 3, 0);
    checkr("lseek SEEK_CUR", lseek(fd, 1, SEEK_CUR), 4, 0);
    checkr("read at end", read(fd, buf, 4), 1, 0);
    checkr("read past end", read(fd, buf, 4), 0, 0);
    checkr("ioctl FIOINTERACTIVE", ioctl(fd, FIOINTERACTIVE, &n), -1, EINVAL);
    errno = 0;
    n = 0;
    checkr("ioctl FIOBUFSIZE", ioctl(fd, FIOBUFSIZE, &n), -1, EINVAL);
    errno = 0;
    checkr("ioctl FIOFNAME", ioctl(fd, FIOFNAME, &n), -1, EINVAL);
    errno = 0;
    refnum = 0;
    checkr("ioctl FIOREFNUM", ioctl(fd, FIOREFNUM, (long *)&refnum), 0, 0);
    check("FIOREFNUM a file's", refnum > 0 ? "refnum" : "none", "refnum");
    checkr("ioctl FIOSETEOF", ioctl(fd, FIOSETEOF, (long *)2), 0, 0);
    checkr("lseek after FIOSETEOF", lseek(fd, 0, SEEK_END), 2, 0);
    fd2 = fcntl(fd, F_DUPFD, 0);
    checkr("F_DUPFD", fd2, 4, 0);
    checkr("close", close(fd), 0, 0);
    checkr("write to the dup", write(fd2, "y", 1), 1, 0);
    checkr("close the dup", close(fd2), 0, 0);
    checkr("close again", close(fd2), -1, EBADF);
    errno = 0;
    checkr("read a closed fd", read(fd, buf, 1), -1, EBADF);
    errno = 0;
    fd = open("devtest.tmp", O_RDONLY);
    checkr("open O_RDONLY", fd, 3, 0);
    checkr("write to O_RDONLY", write(fd, "q", 1), -1, EBADF);
    errno = 0;
    close(fd);
    checkr("open O_CREAT|O_EXCL existing", open("devtest.tmp", O_RDWR | O_CREAT | O_EXCL), -1, EEXIST);
    errno = 0;

    f = fopen("devtest.tmp", "r");
    check("fopen", f ? "FILE" : "NULL", "FILE");
    if(f)
    {
        memset(buf, 0, sizeof buf);
        check("fgets", fgets(buf, sizeof buf, f) ? buf : "NULL", "hey");
        checkr("fclose", fclose(f), 0, 0);
    }
    f = fopen("devtest.tmp", "a");
    if(f)
    {
        checkr("fputs to a file", fputs("abc", f), 3, 0);
        checkr("fclose appended", fclose(f), 0, 0);
    }
    errno = 0;

    checkr("faccess F_RENAME", faccess("devtest.tmp", F_RENAME, (long *)"devtest2.tmp"), 0, 0);
    checkr("open the old name", open("devtest.tmp", O_RDONLY), -1, ENOENT);
    errno = 0;
    checkr("faccess F_DELETE", faccess("devtest2.tmp", F_DELETE, 0), 0, 0);
    checkr("unlink missing", unlink("devtest2.tmp"), -1, ENOENT);
    errno = 0;
    checkr("open missing O_RDONLY", open("missing.tmp", O_RDONLY), -1, ENOENT);
    errno = 0;
}

/* ---- the "dev:" names ---- */

static void test_dev(void)
{
    long n;
    int fd;

    errno = 0;
    fd = open("dev:null", O_WRONLY);
    checkr("open dev:null", fd, 3, 0);
    checkr("write dev:null", write(fd, "x", 1), 1, 0);
    checkr("close dev:null", close(fd), 0, 0);
    fd = open("Dev:Null", O_RDONLY);
    checkr("open Dev:Null", fd, 3, 0);
    checkr("read Dev:Null", read(fd, &n, 4), 0, 0);
    close(fd);
    fd = open("dev:console", O_RDWR);
    checkr("open dev:console", fd, 3, 0);
    checkr("close dev:console", close(fd), 0, 0);
    fd = open("dev:stdout", O_WRONLY);
    checkr("open dev:stdout", fd, 3, 0);
    checkr("write dev:stdout", write(fd, "so\n", 3), 3, 0);
    checkr("close dev:stdout", close(fd), 0, 0);
    fd = open("Dev:StdErr", O_WRONLY);
    checkr("open Dev:StdErr", fd, 3, 0);
    close(fd);
    fd = open("dev:stdin", O_RDONLY);
    checkr("open dev:stdin", fd, 3, 0);
    close(fd);
    checkr("open dev:test", open("dev:test", O_RDWR), -1, ENXIO);
    errno = 0;
    checkr("write fd 1", write(1, "out\n", 4), 4, 0);
    errno = 0;
}

/* ---- a device of its own ---- */

static void test_device(void)
{
    char buf[16];
    long n;
    int fd, fd2;
    FILE *f;

    errno = 0;
    fd = open("test:x", O_RDWR);
    checkr("open test:x", fd, 3, 0);
    checkl("open asks F_OPEN", lastCmd, 0x6400);
    checkl("iob flags O_RDWR", lastFlags & 0xFF, O_RDWR + 1);
    memset(buf, 0, sizeof buf);
    checkr("read test", read(fd, buf, 10), 3, 0);
    check("read test text", buf, "abc");
    checkr("write test", write(fd, "wxyz", 4), 4, 0);
    n = 0;
    checkr("ioctl FIOBUFSIZE test", ioctl(fd, FIOBUFSIZE, &n), 0, 0);
    checkl("FIOBUFSIZE value", n, 77);
    checkl("ioctl reaches the device", lastIoctl, FIOBUFSIZE);
    checkr("ioctl FIOFNAME test", ioctl(fd, FIOFNAME, &n), -1, EINVAL);
    errno = 0;
    checkr("lseek test", lseek(fd, 5, SEEK_SET), 42, 0);
    checkl("lseek is FIOLSEEK", lastIoctl, FIOLSEEK);
    fd2 = fcntl(fd, F_DUPFD, 0);
    checkr("F_DUPFD test", fd2, 4, 0);
    checkl("F_DUPFD is FIODUPFD", lastIoctl, FIODUPFD);
    testerr = EIO;
    checkr("read fails EIO", read(fd, buf, 10), -1, EIO);
    errno = 0;
    testerr = 0x4000FFD4;
    checkr("write fails 0x4000FFD4", write(fd, "w", 1), -1, 0x4000FFD4);
    errno = 0;
    testerr = 0;
    closes = 0;
    checkr("close test", close(fd), 0, 0);
    checkr("close test dup", close(fd2), 0, 0);
    checkl("close calls", closes, 2);

    f = fopen("test:y", "r");
    check("fopen test:y", f ? "FILE" : "NULL", "FILE");
    if(f)
    {
        checkl("getc test", getc(f), 'a');
        checkr("fclose test:y", fclose(f), 0, 0);
    }
    f = fopen("test:z", "w");
    check("fopen test:z", f ? "FILE" : "NULL", "FILE");
    if(f)
    {
        checkr("fputs test:z", fputs("line\n", f), 5, 0);
        checkr("fclose test:z", fclose(f), 0, 0);
    }
    errno = 0;
    checkr("faccess F_DELETE test", faccess("test:x", F_DELETE, 0), 0, 0);
    checkl("F_DELETE reaches the device", lastCmd, 0x6401);
}

/* ---- MakeResolvedPath ---- */

/* "err folder|file alias|- length text end|more": the path as a Pascal
   string in buffer, nothing after it. */
static void resolved(const char *label, const char *name, OSErr wantErr, int wantFolder)
{
    Str255 path;
    unsigned char buf[300];
    Boolean folder = 0, alias = 0, leaf = 0;
    char got[300], want[300];
    OSErr e;
    path[0] = strlen(name);
    memcpy(path + 1, name, path[0]);
    memset(buf, 0xEE, sizeof buf);
    e = MakeResolvedPath(0, 0, path, true, buf, &folder, &alias, &leaf);
    dec(got, e);
    strcat(got, folder ? " folder" : " file");
    strcat(got, alias || leaf ? " alias " : " - ");
    dec(got + strlen(got), buf[0]);
    strcat(got, " ");
    strncat(got, (char *)buf + 1, buf[0] < 200 ? buf[0] : 200);
    strcat(got, buf[buf[0] + 1] == 0xEE ? " end" : " more");
    dec(want, wantErr);
    strcat(want, wantFolder ? " folder - " : " file - ");
    dec(want + strlen(want), strlen(name));
    strcat(want, " ");
    strcat(want, name);
    strcat(want, " end");
    check(label, got, want);
}

static void test_resolve(void)
{
    resolved("MakeResolvedPath file", "Host:devtest_results.txt", noErr, 0);
    resolved("MakeResolvedPath folder", "Host:", noErr, 1);
    resolved("MakeResolvedPath missing", "Host:missing.tmp", fnfErr, 0);
    resolved("MakeResolvedPath no folder", "Host:nodir:x", dirNFErr, 0);
    resolved("MakeResolvedPath no volume", "novolume:x", nsvErr, 0);
}

int main(void)
{
    OSErr err;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(0);
    InitCursor();

    HDelete(0, 0, "\pHost:devtest_results.txt");
    err = HCreate(0, 0, "\pHost:devtest_results.txt", 'ttxt', 'TEXT');
    if(err == noErr)
        err = HOpen(0, 0, "\pHost:devtest_results.txt", fsWrPerm, &out_ref);
    if(err != noErr)
        ExitToShell();

    test_table();
    test_files();
    test_dev();
    test_device();
    test_resolve();

    put("---\n");
    FSClose(out_ref);
    exit(failures ? 1 : 0);
    return 0;
}
