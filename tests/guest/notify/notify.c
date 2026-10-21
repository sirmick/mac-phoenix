/* notify.c — the Notification Manager, from an application in the
 * background (as CodeWarrior's CFM-68K notice is posted).
 *
 * Finder comes to the front; then NMInstall with a text, the system beep
 * and a response procedure. The response must not run while the alert is
 * up: after 2 s it hasn't; the line saying so is flushed and the host
 * presses Return on the alert (test_guest_suite.sh --key-on; an event we
 * post ourselves doesn't reach 7.5.5's alert), after which it must run. A
 * request whose qType isn't nmType is refused (nmTypErr). Results to
 * Host:notify_test.txt. */

#include <ConditionalMacros.h>
#include <Events.h>
#include <Fonts.h>
#include <Menus.h>
#include <Notification.h>
#include <OSUtils.h>
#include <Processes.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <Windows.h>
#include <Dialogs.h>
#include <stdio.h>

QDGlobals qd;

/* No globals here: it may run in another application's world. */
static pascal void Response(NMRecPtr nm)
{
    nm->nmRefCon = 1;
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

static void wait_ticks(long ticks, volatile long *until)
{
    EventRecord ev;
    long start = TickCount();

    while(TickCount() - start < ticks && !(until && *until))
        WaitNextEvent(everyEvent, &ev, 10, NULL);
}

int main(void)
{
    static NMRec nm, bad;
    FILE *out;
    OSErr err;
    int fails = 0, passes = 0;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();

    if((out = fopen("Host:notify_test.txt", "w")) == NULL)
        return 1;

    finder_to_front();
    wait_ticks(60, NULL);

    bad.qType = 0;
    err = NMInstall(&bad);
    if(err == nmTypErr)
    {
        fprintf(out, "PASS bad_qtype\n");
        passes++;
    }
    else
    {
        fprintf(out, "FAIL bad_qtype NMInstall gave %d\n", err);
        fails++;
    }

    nm.qType = nmType;
    nm.nmMark = 0;
    nm.nmIcon = NULL;
    nm.nmSound = (Handle)-1;
    nm.nmStr = (StringPtr)"\pNotifyTest: hello from the Notification Manager.";
    nm.nmResp = (NMUPP)Response;
    nm.nmRefCon = 0;
    err = NMInstall(&nm);
    if(err == noErr)
    {
        fprintf(out, "PASS install\n");
        passes++;
    }
    else
    {
        fprintf(out, "FAIL install NMInstall gave %d\n", err);
        fails++;
    }

    wait_ticks(120, NULL);
    if(nm.nmRefCon == 0)
    {
        fprintf(out, "PASS waits_for_alert\n");
        passes++;
        fflush(out);
    }
    else
    {
        fprintf(out, "FAIL waits_for_alert response ran before the alert was dismissed\n");
        fails++;
    }

    wait_ticks(20 * 60, &nm.nmRefCon);
    if(nm.nmRefCon == 1)
    {
        fprintf(out, "PASS response\n");
        passes++;
    }
    else
    {
        fprintf(out, "FAIL response never ran\n");
        fails++;
    }
    NMRemove(&nm);

    fprintf(out, "---\n%d passed, %d failed, 0 skipped\n", passes, fails);
    fclose(out);
    return 0;
}
