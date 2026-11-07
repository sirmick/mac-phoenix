/* stdiowait.c — StdioWait: a stream written across three seconds of
 * WaitNextEvent (executor/tests/web/tab_stdio.mjs). Writes "stdiowait.txt"
 * beside itself: a line, the wait, fifty lines, DONE, fclose, and quits.
 *
 * In the tab on Mac OS 8.6 the Finder, given time in the wait, loaded
 * libraries that import StdCLib, and Executor's StdCLib started afresh
 * under the application: its stream was forgotten, and the file kept
 * only what came before the wait. */

#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Menus.h>
#include <Processes.h>
#include <TextEdit.h>
#include <Windows.h>
#include <stdio.h>

QDGlobals qd;

static void idle(UInt32 sleep)
{
    EventRecord ev;

    WaitNextEvent(everyEvent & ~keyDownMask & ~autoKeyMask, &ev, sleep, NULL);
}

int main(void)
{
    ProcessSerialNumber psn = { 0, kCurrentProcess };
    ProcessInfoRec info;
    FSSpec app;
    FILE *out;
    unsigned long t;
    int i;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();

    info.processInfoLength = sizeof info;
    info.processName = NULL;
    info.processAppSpec = &app;
    if(GetProcessInformation(&psn, &info) != noErr)
        return 1;
    HSetVol(NULL, app.vRefNum, app.parID);

    if((out = fopen("stdiowait.txt", "w")) == NULL)
        return 1;
    fprintf(out, "START\n");
    fflush(out);
    t = TickCount();
    while(TickCount() - t < 180)
        idle(1);
    for(i = 1; i <= 50; i++)
        fprintf(out, "LINE %d\n", i);
    fprintf(out, "DONE\n");
    fclose(out);
    return 0;
}
