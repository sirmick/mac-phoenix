/* main.c — MacPhoenixSuite: Toolbox set-up, the results file, the sections
 * named in Host:suite.cfg. */

#include "suite.h"

#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Gestalt.h>
#include <Menus.h>
#include <Quickdraw.h>
#include <TextEdit.h>
#include <Windows.h>
#include <stdio.h>
#include <string.h>

QDGlobals qd;

static FILE *results;
static int passed, failed, skipped;

void report_pass(const char *name)
{
    fprintf(results, "PASS %s\n", name);
    fflush(results);
    passed++;
}

void report_fail(const char *name, const char *detail)
{
    fprintf(results, "FAIL %s%s%s\n", name, detail ? " " : "", detail ? detail : "");
    fflush(results);
    failed++;
}

void report_skip(const char *name, const char *reason)
{
    fprintf(results, "SKIP %s%s%s\n", name, reason ? " " : "", reason ? reason : "");
    fflush(results);
    skipped++;
}

void idle_ticks(long ticks)
{
    EventRecord ev;
    unsigned long end = TickCount() + ticks;

    while(TickCount() < end)
        WaitNextEvent(everyEvent & ~keyDownMask & ~autoKeyMask, &ev, 1, NULL);
}

int on_powerpc(void)
{
    long arch;

    return Gestalt(gestaltSysArchitecture, &arch) == noErr && arch == gestaltPowerPC;
}

static const struct
{
    const char *name;
    void (*run)(void);
} sections[] = {
    { "disk", test_disk },
    { "extfs", test_extfs },
    { "audio", test_audio },
    { "applescript", test_applescript_dates },
    { "network", test_network },
    { "keyboard", test_keyboard },
};

int main(void)
{
    char results_name[64] = "test_results.txt";
    char which[256] = "disk extfs audio applescript network serial";
    char path[80];
    FILE *cfg;
    char *word;
    int i;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    idle_ticks(30);                     /* come to the front */

    if((cfg = fopen("Host:suite.cfg", "r")) != NULL)
    {
        if(fgets(results_name, sizeof results_name, cfg))
            results_name[strcspn(results_name, "\r\n")] = 0;
        if(fgets(which, sizeof which, cfg))
            which[strcspn(which, "\r\n")] = 0;
        fclose(cfg);
    }
    sprintf(path, "Host:%s", results_name);
    if((results = fopen(path, "w")) == NULL)
        return 1;

    for(word = strtok(which, " "); word; word = strtok(NULL, " "))
    {
        if(strcmp(word, "serial") == 0)
        {
            /* No serial backend yet: probing the drivers hangs (as it did
             * from MacPerl). */
            report_skip("serial_probe_disabled", "awaiting host serial backend");
            continue;
        }
        for(i = 0; i < sizeof sections / sizeof sections[0]; i++)
            if(strcmp(word, sections[i].name) == 0)
                break;
        if(i < sizeof sections / sizeof sections[0])
            sections[i].run();
        else
            report_fail("unknown_section", word);
    }

    fprintf(results, "---\n%d passed, %d failed, %d skipped\n", passed, failed, skipped);
    fclose(results);
    return 0;
}
