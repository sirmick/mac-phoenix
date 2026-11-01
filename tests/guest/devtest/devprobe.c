/* devprobe.c — DevProbe, a PowerPC application that records how StdCLib
   uses its device table (_getDevHandler, _addDevHandler): every handler of
   the slots it finds is replaced by one that logs its arguments (r3-r6, the
   words they point to) and its result, then calls the original; then the
   low-level calls, stdio and the "dev:" names run. Run on Apple's StdCLib
   (Mac OS 9.0.4) to learn what Executor's has to do. Log to
   Host:devprobe.txt through the File Manager; "---" at the end. */
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

static short out_ref;

static void put(const char *s)
{
    long n = strlen(s);
    FSWrite(out_ref, &n, s);
}

static void hex(unsigned long v)
{
    static const char digits[] = "0123456789ABCDEF";
    char out[10];
    int i;
    for(i = 0; i < 8; i++)
        out[i] = digits[(v >> (28 - 4 * i)) & 15];
    out[8] = ' ';
    out[9] = 0;
    put(out);
}

static void type(OSType t)
{
    char s[6];
    s[0] = t >> 24; s[1] = t >> 16; s[2] = t >> 8; s[3] = t; s[4] = ' '; s[5] = 0;
    put(s);
}

/* Twelve words at p, if p looks like an address in RAM. */
static void words(const char *what, long p)
{
    unsigned long *w = (unsigned long *)p;
    int i;
    if(p < 0x1000 || (unsigned long)p >= 0x40000000)
        return;
    put("    ");
    put(what);
    put(": ");
    for(i = 0; i < 12; i++)
        hex(w[i]);
    put("\n");
}

static void str(const char *what, long p)
{
    const char *s = (const char *)p;
    char buf[40];
    int i;
    if(p < 0x1000 || (unsigned long)p >= 0x40000000)
        return;
    for(i = 0; i < 39 && s[i] >= 32 && s[i] < 127; i++)
        buf[i] = s[i];
    buf[i] = 0;
    if(i < 2)
        return;
    put("    ");
    put(what);
    put(" as text: \"");
    put(buf);
    put("\"\n");
}

#define SLOTS 4
static OSType names[SLOTS];
static Handler orig[SLOTS][5];
static int depth;

static long logged(int slot, int which, long a, long b, long c, long d)
{
    static const char *const procs[5] = { "p1", "p2", "p3", "p4", "p5" };
    long r;
    put("  call ");
    type(names[slot]);
    put(procs[which]);
    put(" ");
    hex(a); hex(b); hex(c); hex(d);
    put("\n");
    words("r3", a); str("r3", a);
    words("r4", b); str("r4", b);
    words("r5", c); str("r5", c);
    words("r6", d);
    if(which != 0 && a >= 0x1000 && (unsigned long)a < 0x40000000)
    {
        /* The device's word for the descriptor, and what it leads to. */
        long c = ((long *)a)[2];
        words("cookie", c);
        if(c >= 0x1000 && (unsigned long)c < 0x40000000)
            words("*cookie", *(long *)c);
    }
    depth++;
    r = orig[slot][which](a, b, c, d);
    depth--;
    put("  -> ");
    type(names[slot]);
    put(procs[which]);
    put(" = ");
    hex(r);
    put(" errno ");
    hex(errno);
    put("\n");
    words("r3 after", a);
    words("r5 after", c);
    return r;
}

#define WRAP(s, w) static long wrap_##s##_##w(long a, long b, long c, long d) \
    { return logged(s, w, a, b, c, d); }
#define WRAPS(s) WRAP(s, 0) WRAP(s, 1) WRAP(s, 2) WRAP(s, 3) WRAP(s, 4)
WRAPS(0) WRAPS(1) WRAPS(2) WRAPS(3)
static Handler wraps[SLOTS][5] = {
    { wrap_0_0, wrap_0_1, wrap_0_2, wrap_0_3, wrap_0_4 },
    { wrap_1_0, wrap_1_1, wrap_1_2, wrap_1_3, wrap_1_4 },
    { wrap_2_0, wrap_2_1, wrap_2_2, wrap_2_3, wrap_2_4 },
    { wrap_3_0, wrap_3_1, wrap_3_2, wrap_3_3, wrap_3_4 },
};

/* A device of its own (slot 3): claims "test:" names, reads "abc",
   takes what is written, answers FIOINTERACTIVE and FIOBUFSIZE; logs each
   call. testerr makes its read and write fail with that result. */
static long testerr;

static long test_proc(int which, long a, long b, long c, long d)
{
    put("  TEST p");
    hex(which + 1);
    hex(a); hex(b); hex(c); hex(d);
    put("\n");
    words("r3", a); str("r3", a);
    if(which == 4)
        words("r5", c);
    switch(which)
    {
        case 0:
            if(strncmp((char *)a, "test:", 5) != 0)
                return -1;
            if(b == 0x6400)
                ((long *)c)[2] = 0x12345678;
            return 0;
        case 1:
            return 0;
        case 2:
        {
            long *iob = (long *)a;
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
        case 3:
        {
            long *iob = (long *)a;
            if(testerr)
                return testerr;
            iob[4] += iob[3];
            iob[3] = 0;
            return 0;
        }
        case 4:
            if(b == FIOINTERACTIVE)
                return 0;
            if(b == FIOBUFSIZE)
            {
                *(long *)c = 77;
                return 0;
            }
            if(b == FIOLSEEK)
            {
                ((long *)c)[1] = 42;
                return 0;
            }
            if(b == FIODUPFD)
                return 0;
            return 22;
    }
    return 0;
}
static long t1(long a, long b, long c, long d) { return test_proc(0, a, b, c, d); }
static long t2(long a, long b, long c, long d) { return test_proc(1, a, b, c, d); }
static long t3(long a, long b, long c, long d) { return test_proc(2, a, b, c, d); }
static long t4(long a, long b, long c, long d) { return test_proc(3, a, b, c, d); }
static long t5(long a, long b, long c, long d) { return test_proc(4, a, b, c, d); }

static void step(const char *s)
{
    put("== ");
    put(s);
    put("\n");
}

static void result(const char *what, long v)
{
    put("  ");
    put(what);
    put(" = ");
    hex(v);
    put(" errno ");
    hex(errno);
    put("\n");
}

int main(void)
{
    OSErr err;
    long r, n;
    int slot, i, fd;
    char buf[64];
    FILE *f;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(0);
    InitCursor();

    HDelete(0, 0, "\pHost:devprobe.txt");
    err = HCreate(0, 0, "\pHost:devprobe.txt", 'ttxt', 'TEXT');
    if(err == noErr)
        err = HOpen(0, 0, "\pHost:devprobe.txt", fsWrPerm, &out_ref);
    if(err != noErr)
        ExitToShell();

    step("_getDevHandler");
    for(slot = 0; slot < 8; slot++)
    {
        OSType name = 0;
        Handler h[5] = { 0, 0, 0, 0, 0 };
        r = _getDevHandler(slot, &name, &h[0], &h[1], &h[2], &h[3], &h[4]);
        put("  slot ");
        hex(slot);
        put("= ");
        hex(r);
        type(name);
        for(i = 0; i < 5; i++)
            hex((long)h[i]);
        put("\n");
        if(slot < SLOTS && r == 0)
        {
            names[slot] = name;
            for(i = 0; i < 5; i++)
                orig[slot][i] = h[i];
        }
    }
    step("_addDevHandler wrappers");
    for(slot = 0; slot < SLOTS; slot++)
        if(names[slot])
        {
            r = _addDevHandler(slot, names[slot], wraps[slot][0], wraps[slot][1], wraps[slot][2],
                               wraps[slot][3], wraps[slot][4]);
            result("add", r);
        }
    if(!names[3])
    {
        step("_addDevHandler 3 TEST");
        r = _addDevHandler(3, 'TEST', t1, t2, t3, t4, t5);
        result("add", r);
        for(slot = 4; slot < 12; slot++)
        {
            put("  slot ");
            hex(slot);
            result("add", _addDevHandler(slot, 'TES2', t1, t2, t3, t4, t5));
        }
        step("_getDevHandler 3");
        {
            OSType name = 0;
            Handler h[5];
            result("get", _getDevHandler(3, &name, &h[0], &h[1], &h[2], &h[3], &h[4]));
            type(name);
            put(h[0] == t1 ? "t1 " : "not-t1 ");
            put("\n");
        }
    }

    step("open probe.tmp O_RDWR|O_CREAT|O_TRUNC");
    fd = open("probe.tmp", O_RDWR | O_CREAT | O_TRUNC);
    result("fd", fd);
    step("write 5");
    result("write", write(fd, "hello", 5));
    step("lseek 1 SEEK_SET");
    result("lseek", lseek(fd, 1, SEEK_SET));
    step("read 3");
    r = read(fd, buf, 3);
    result("read", r);
    step("ioctl FIOINTERACTIVE");
    result("ioctl", ioctl(fd, FIOINTERACTIVE, &n));
    step("ioctl FIOBUFSIZE");
    n = 0;
    result("ioctl", ioctl(fd, FIOBUFSIZE, &n));
    result("arg", n);
    step("ioctl FIOREFNUM");
    n = 0;
    result("ioctl", ioctl(fd, FIOREFNUM, &n));
    result("arg", n);
    step("ioctl FIOFNAME");
    n = 0;
    result("ioctl", ioctl(fd, FIOFNAME, &n));
    str("arg", n);
    step("dup");
    result("dup", fcntl(fd, F_DUPFD, 0));
    step("close");
    result("close", close(fd));
    step("open missing.tmp O_RDONLY");
    result("fd", open("missing.tmp", O_RDONLY));
    step("open dev:null O_WRONLY");
    fd = open("dev:null", O_WRONLY);
    result("fd", fd);
    if(fd >= 0)
    {
        result("write", write(fd, "x", 1));
        result("close", close(fd));
    }
    step("open dev:console O_RDWR");
    fd = open("dev:console", O_RDWR);
    result("fd", fd);
    if(fd >= 0)
        result("close", close(fd));
    step("open dev:stdout O_WRONLY");
    fd = open("dev:stdout", O_WRONLY);
    result("fd", fd);
    if(fd >= 0)
    {
        result("write", write(fd, "so\n", 3));
        result("close", close(fd));
    }
    step("open Dev:StdErr O_WRONLY");
    fd = open("Dev:StdErr", O_WRONLY);
    result("fd", fd);
    if(fd >= 0)
        result("close", close(fd));
    step("open dev:stdin O_RDONLY");
    fd = open("dev:stdin", O_RDONLY);
    result("fd", fd);
    if(fd >= 0)
        result("close", close(fd));
    step("open dev:test O_RDWR");
    fd = open("dev:test", O_RDWR);
    result("fd", fd);
    if(fd >= 0)
        result("close", close(fd));
    step("write 1 (stdout)");
    result("write", write(1, "out\n", 4));
    step("ioctl 0 FIOINTERACTIVE");
    result("ioctl", ioctl(0, FIOINTERACTIVE, &n));
    step("read 0 (stdin)");
    result("read", read(0, buf, 4));
    step("fopen probe.tmp r");
    f = fopen("probe.tmp", "r");
    result("f", (long)f);
    if(f)
    {
        step("fgets");
        result("fgets", (long)fgets(buf, sizeof buf, f));
        step("fclose");
        result("fclose", fclose(f));
    }
    step("faccess probe.tmp F_DELETE");
    result("faccess", faccess("probe.tmp", F_DELETE, 0));
    step("unlink missing.tmp");
    result("unlink", unlink("missing.tmp"));
    step("fflush stdout");
    printf("printed\n");
    result("fflush", fflush(stdout));

    step("lseek -2 SEEK_END");
    fd = open("probe.tmp", O_RDWR | O_CREAT | O_TRUNC);
    write(fd, "hello", 5);
    result("lseek", lseek(fd, -2, SEEK_END));
    step("lseek 1 SEEK_CUR");
    result("lseek", lseek(fd, 1, SEEK_CUR));
    step("read at end");
    result("read", read(fd, buf, 4));
    step("FIOSETEOF 2");
    n = 2;
    result("ioctl", ioctl(fd, FIOSETEOF, (long *)2));
    result("lseek end", lseek(fd, 0, SEEK_END));
    step("dup, close twice");
    i = fcntl(fd, F_DUPFD, 0);
    result("dup", i);
    result("close", close(fd));
    result("write dup", write(i, "z", 1));
    result("close dup", close(i));
    result("close again", close(i));
    step("write to O_RDONLY");
    fd = open("probe.tmp", O_RDONLY);
    result("write", write(fd, "q", 1));
    close(fd);
    step("faccess F_RENAME");
    result("faccess", faccess("probe.tmp", F_RENAME, (long *)"probe2.tmp"));
    unlink("probe2.tmp");

    step("open test:x O_RDWR");
    fd = open("test:x", O_RDWR);
    result("fd", fd);
    step("read 10 test");
    result("read", read(fd, buf, 10));
    step("write 4 test");
    result("write", write(fd, "wxyz", 4));
    step("ioctl FIOBUFSIZE test");
    n = 0;
    result("ioctl", ioctl(fd, FIOBUFSIZE, &n));
    result("arg", n);
    step("ioctl FIOFNAME test");
    result("ioctl", ioctl(fd, FIOFNAME, &n));
    step("lseek 5 SEEK_SET test");
    result("lseek", lseek(fd, 5, SEEK_SET));
    step("dup test");
    i = fcntl(fd, F_DUPFD, 0);
    result("dup", i);
    step("read fails 5");
    testerr = 5;
    result("read", read(fd, buf, 10));
    step("write fails 0x4000FFD4");
    testerr = 0x4000FFD4;
    result("write", write(fd, "w", 1));
    testerr = 0;
    step("close test");
    result("close", close(fd));
    result("close dup", close(i));
    step("fopen test:y r");
    f = fopen("test:y", "r");
    result("f", (long)f);
    if(f)
    {
        result("getc", getc(f));
        result("fclose", fclose(f));
    }
    step("fopen test:z w, fputs, fclose");
    f = fopen("test:z", "w");
    if(f)
    {
        result("fputs", fputs("line\n", f));
        result("fclose", fclose(f));
    }
    step("faccess test:x F_DELETE");
    result("faccess", faccess("test:x", F_DELETE, 0));

    put("---\n");
    FSClose(out_ref);
    /* Back to Apple's handlers before exit closes the streams. */
    for(slot = 0; slot < SLOTS; slot++)
        if(names[slot])
            _addDevHandler(slot, names[slot], orig[slot][0], orig[slot][1], orig[slot][2],
                           orig[slot][3], orig[slot][4]);
    exit(0);
    return 0;
}
