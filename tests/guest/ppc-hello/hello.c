/* hello.c — PPCHello, a PowerPC-only application on InterfaceLib alone (no
   StdCLib, no runtime: PPCLink -main main). Executor's PowerPC test
   (ctest ppc_hello_executor): integer and floating-point code, Toolbox
   calls from PowerPC, a PowerPC routine the Toolbox calls back through a
   routine descriptor. Results go to Host:ppc_hello.txt in the guest
   suite's form (PASS/FAIL lines, "---" at the end). */
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
#include <TextUtils.h>
#include <MixedMode.h>

QDGlobals qd;

static short out_ref;

static void put(const unsigned char *s)
{
    long n = s[0];
    FSWrite(out_ref, &n, s + 1);
}

/* "PASS label value" or "FAIL label value (want expected)" */
static void check(const char *label, long value, long expected)
{
    Str255 num;
    unsigned char line[128];
    const char *verdict = value == expected ? "PASS " : "FAIL ";
    int i, n = 0;
    for(i = 0; verdict[i]; i++)
        line[1 + n++] = verdict[i];
    for(i = 0; label[i]; i++)
        line[1 + n++] = label[i];
    line[1 + n++] = ' ';
    NumToString(value, num);
    for(i = 1; i <= num[0]; i++)
        line[1 + n++] = num[i];
    if(value != expected)
    {
        const char *want = " (want ";
        for(i = 0; want[i]; i++)
            line[1 + n++] = want[i];
        NumToString(expected, num);
        for(i = 1; i <= num[0]; i++)
            line[1 + n++] = num[i];
        line[1 + n++] = ')';
    }
    line[1 + n++] = 13;
    line[0] = n;
    put(line);
}

static long fib(long n)
{
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

/* sum 1/k^2 for k = 1..n */
static double basel(int n)
{
    double s = 0.0;
    int k;
    for(k = n; k >= 1; k--)
        s += 1.0 / ((double)k * (double)k);
    return s;
}

/* A modal filter the Toolbox calls back: the PowerPC side of a UPP. */
static long g_compared;
static pascal Boolean idle_filter(DialogPtr d, EventRecord *e, short *item)
{
    (void)d; (void)e; (void)item;
    g_compared++;
    return false;
}

int main(void)
{
    WindowPtr w;
    Rect r;
    long i, sum;
    long ticks;
    OSErr err;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(0);
    InitCursor();

    HDelete(0, 0, "\pHost:ppc_hello.txt");
    err = HCreate(0, 0, "\pHost:ppc_hello.txt", 'ttxt', 'TEXT');
    if(err == noErr)
        err = HOpen(0, 0, "\pHost:ppc_hello.txt", fsWrPerm, &out_ref);
    if(err != noErr)
        ExitToShell();

    check("fib", fib(24), 46368);
    sum = 0;
    for(i = 1; i <= 1000; i++)
        sum += i * i % 97;
    check("loop", sum, 47867);
    check("basel", (long)(basel(10000) * 1000000.0), 1644834);
    check("divw", -1000000L / 7, -142857);

    SetRect(&r, 60, 60, 420, 200);
    w = NewWindow(0, &r, "\pPPCHello", true, documentProc, (WindowPtr)-1, false, 0);
    SetPort(w);
    MoveTo(20, 40);
    DrawString("\pHello from PowerPC");
    check("window", w != 0, 1);
    check("width", StringWidth("\pHello from PowerPC") > 0, 1);

    /* The Toolbox calling PowerPC code: a ModalFilterUPP through a routine
       descriptor, invoked directly with CallUniversalProc. */
    {
        ModalFilterUPP upp = NewModalFilterProc(idle_filter);
        EventRecord e;
        short item = 0;
        g_compared = 0;
        CallModalFilterProc(upp, w, &e, &item);
        CallModalFilterProc(upp, w, &e, &item);
        check("upp", g_compared, 2);
        DisposeRoutineDescriptor(upp);
    }

    ticks = TickCount();
    Delay(30, &ticks);
    put("\p---\n");   /* MPW C: '\n' is 13 */
    FSClose(out_ref);
    FlushVol(0, 0);
    DisposeWindow(w);
    return 0;
}
