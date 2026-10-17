/* demo.c — the bridge trap and an event-driven wake-up (docs/CommandBridge.md,
 * "Bridge v2"), as the next BridgeAgent will use them.
 *
 * HELLO through the trap; then three rounds of: sleep in WaitNextEvent for up
 * to 20 s, while a Time Manager task asks the host every 50 ms (PENDING)
 * whether to wake up, and calls WakeUpProcess when it says so. The host asks
 * 2 s after HELLO and 1 s after each REPORT, and REPORT answers how long
 * after the asking the application got to run. Rounds 2 and 3 have the
 * Finder in front, so the application wakes in the background, as the agent
 * would. Results to Host:bridge_demo.txt. */

#include <ConditionalMacros.h>
#include <Events.h>
#include <Fonts.h>
#include <Menus.h>
#include <Processes.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <Timer.h>
#include <Windows.h>
#include <Dialogs.h>
#include <stdio.h>
#include <string.h>

QDGlobals qd;

/* The bridge trap: D0 selector, A0 pointer, D1 value; D0 back. */
#pragma parameter __D0 BridgeCall(__D0, __A0, __D1)
long BridgeCall(long selector, const void *ptr, long value) ONEWORDINLINE(0xABE5);

enum { kHello = 1, kPending = 2, kReport = 3, kMagic = 0x4D506878 };

typedef struct
{
    TMTask tm;
    ProcessSerialNumber psn;
    volatile long woke;                 /* Ticks when the task woke us */
} DemoTask;

/* The Time Manager passes the task record in A1 (SC applies #pragma
 * parameter to calls, not to a function's own definition). */
#pragma parameter __A0 GetA1
DemoTask *GetA1(void) ONEWORDINLINE(0x2049);              /* movea.l a1,a0 */

/* Interrupt time: no globals (A5 is someone else's), just the task record. */
static pascal void TimerTask(void)
{
    DemoTask *t = GetA1();

    if(BridgeCall(kPending, 0, 0) == 1)
    {
        t->woke = *(volatile long *)0x16A;      /* Ticks */
        WakeUpProcess(&t->psn);
    }
    PrimeTime((QElemPtr)&t->tm, 50);
}

static void finder_to_front(void)
{
    ProcessSerialNumber psn = { 0, kNoProcess };
    ProcessInfoRec info;

    while(GetNextProcess(&psn) == noErr)
    {
        info.processInfoLength = sizeof info;
        info.processName = NULL;
        info.processAppSpec = NULL;
        if(GetProcessInformation(&psn, &info) == noErr && info.processSignature == 'MACS')
        {
            SetFrontProcess(&psn);
            return;
        }
    }
}

int main(void)
{
    static DemoTask task;
    FILE *out;
    EventRecord ev;
    char line[128];
    long hello, ms, start, slept;
    int round, fails = 0, passes = 0;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();

    if((out = fopen("Host:bridge_demo.txt", "w")) == NULL)
        return 1;
    hello = BridgeCall(kHello, 0, 1);
    if(hello != kMagic)
    {
        fprintf(out, "FAIL hello got %08lx\n---\n", hello);
        fclose(out);
        return 0;
    }
    fprintf(out, "PASS hello\n");
    passes++;

    GetCurrentProcess(&task.psn);
    task.tm.tmAddr = (TimerUPP)TimerTask;
    InsTime((QElemPtr)&task.tm);
    PrimeTime((QElemPtr)&task.tm, 50);

    for(round = 1; round <= 3; round++)
    {
        if(round >= 2)
            finder_to_front();
        task.woke = 0;
        start = TickCount();
        while(!task.woke && TickCount() - start < 20 * 60)
            WaitNextEvent(everyEvent, &ev, 20 * 60, NULL);    /* sleep: up to 20 s */
        slept = TickCount() - start;
        if(!task.woke)
        {
            fprintf(out, "FAIL wake_%d never woken (slept %ld ticks)\n", round, slept);
            fails++;
            break;
        }
        sprintf(line, "round %d (%s): running %ld ticks after the task woke it",
                round, round >= 2 ? "background" : "front", TickCount() - task.woke);
        ms = BridgeCall(kReport, line, strlen(line));
        if(ms >= 0 && ms < 500)
        {
            fprintf(out, "PASS wake_%d_%s_%ldms\n", round, round >= 2 ? "background" : "front", ms);
            passes++;
        }
        else
        {
            fprintf(out, "FAIL wake_%d %ld ms after the host asked (slept %ld ticks)\n", round, ms, slept);
            fails++;
        }
    }
    RmvTime((QElemPtr)&task.tm);
    fprintf(out, "---\n%d passed, %d failed, 0 skipped\n", passes, fails);
    fclose(out);
    return 0;
}
