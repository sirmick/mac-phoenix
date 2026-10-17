/* keyboard.c — keystrokes from the host reach the front application.
 *
 * The host waits for Host:test_keyboard_ready.txt, sends a known sequence
 * through /api/keypress and expects its character codes back, comma
 * separated, in Host:test_keyboard_recv.txt (tests/test_keyboard_guest.sh):
 * browser/HTTP -> /api/keypress -> ADB -> the key map -> the event queue ->
 * this application's keyDown events. */

#include "suite.h"

#include <Events.h>
#include <Quickdraw.h>
#include <Windows.h>
#include <stdio.h>
#include <string.h>

void test_keyboard(void)
{
    Rect r;
    WindowPtr w;
    EventRecord ev;
    char line[128], out[512];
    int n = 0, i;
    unsigned long until;
    FILE *f;

    SetRect(&r, 60, 60, 360, 160);
    w = NewWindow(NULL, &r, "\pKeyboard test", true, documentProc, (WindowPtr)-1, false, 0);
    SetPort(w);
    idle_ticks(30);                         /* in front before the host types */

    if((f = fopen("Host:test_keyboard_ready.txt", "w")) == NULL)
    {
        report_fail("keyboard_ready", "cannot create the marker");
        return;
    }
    fprintf(f, "ready\n");
    fclose(f);

    for(until = TickCount() + 15 * 60; TickCount() < until && n < sizeof line;)
    {
        if(!WaitNextEvent(keyDownMask | autoKeyMask | updateMask, &ev, 2, NULL))
            continue;
        if(ev.what == updateEvt)
        {
            BeginUpdate((WindowPtr)ev.message);
            EndUpdate((WindowPtr)ev.message);
            continue;
        }
        line[n] = (char)(ev.message & charCodeMask);
        if(line[n] == 13 || line[n] == 3)      /* Return, Enter (MPW C swaps \r and \n) */
            break;
        n++;
    }

    out[0] = 0;
    for(i = 0; i < n; i++)
        sprintf(out + strlen(out), i ? ",%d" : "%d", (unsigned char)line[i]);
    if((f = fopen("Host:test_keyboard_recv.txt", "w")) != NULL)
    {
        fprintf(f, "%s\n", out);
        fclose(f);
    }
    DisposeWindow(w);
    if(n)
        report_pass("keyboard_recv");
    else
        report_fail("keyboard_recv", "no keys");
}
