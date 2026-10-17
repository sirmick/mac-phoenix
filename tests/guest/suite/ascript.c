/* ascript.c — AppleScript dates and reals as text: the Script Manager and
 * International Utilities underneath (StringToDate/StringToTime,
 * IULDateString, number formats), through the OSA component. */

#include "suite.h"

#include <AppleEvents.h>
#include <AppleScript.h>
#include <Components.h>
#include <Memory.h>
#include <OSA.h>
#include <stdio.h>
#include <string.h>

static ComponentInstance as;

/* What AppleScript displays for a script's result, or an error. */
static void as_result(const char *src, char *out, int size)
{
    AEDesc source, text;
    OSAID result = kOSANullScript;
    OSAError err;
    long n;

    out[0] = 0;
    if((err = AECreateDesc(typeChar, src, strlen(src), &source)) != noErr)
    {
        sprintf(out, "AECreateDesc %ld", (long)err);
        return;
    }
    err = OSACompileExecute(as, &source, kOSANullScript, kOSAModeNull, &result);
    AEDisposeDesc(&source);
    if(err != noErr)
    {
        sprintf(out, "OSACompileExecute %ld", (long)err);
        return;
    }
    err = OSADisplay(as, result, typeChar, kOSAModeNull, &text);
    OSADispose(as, result);
    if(err != noErr)
    {
        sprintf(out, "OSADisplay %ld", (long)err);
        return;
    }
    n = GetHandleSize(text.dataHandle);
    if(n > size - 1)
        n = size - 1;
    BlockMoveData(*text.dataHandle, out, n);
    out[n] = 0;
    AEDisposeDesc(&text);
}

int as_run(const char *src, char *out, int size)
{
    if(!as)
        as = OpenDefaultComponent(kOSAComponentType, kAppleScriptSubtype);
    if(!as)
    {
        strcpy(out, "no AppleScript component");
        return 0;
    }
    as_result(src, out, size);
    return 1;
}

void test_applescript_dates(void)
{
    /* AppleScript on a Power Mac prints reals with 12 significant digits,
     * on a 68k Mac (SANE's extended) 13; both PPC cores agree
     * (docs/cpu/PLAN.md S9). It is the machine's AppleScript, whatever
     * this code runs as. */
    static const char *cases[][3] = {
        { "as_date_string", "return date string of (date \"October 9, 2026\")",
          "\"Friday, October 9, 2026\"" },
        { "as_date_time_string", "return (date \"Friday, October 9, 2026 4:11:52 PM\") as string",
          "\"Friday, October 9, 2026 4:11:52 PM\"" },
        { "as_date_numeric", "return (date \"10/9/2026\") as string",
          "\"Friday, October 9, 2026 12:00:00 AM\"" },
        { "as_date_value", "return date \"October 9, 2026\"",
          "date \"Friday, October 9, 2026 12:00:00 AM\"" },
        { "as_date_arithmetic", "return ((date \"October 10, 2026\") - (date \"October 9, 2026\")) / days",
          "1.0" },
        { "as_real_plain", "return 1 / 3", NULL },
        { "as_real_exponent", "return 1234567.5", "1.2345675E+6" },
        { "as_real_small", "return 1.0E-5", "1.0E-5" },
    };
    char got[256], detail[400];
    const char *want;
    int i;

    for(i = 0; i < sizeof cases / sizeof cases[0]; i++)
    {
        if(!as_run(cases[i][1], got, sizeof got))
        {
            report_fail(cases[i][0], got);
            continue;
        }
        want = cases[i][2] ? cases[i][2] : on_powerpc() ? "0.333333333333" : "0.3333333333333";
        if(strcmp(got, want) == 0)
            report_pass(cases[i][0]);
        else
        {
            sprintf(detail, "got '%s' want '%s'", got, want);
            report_fail(cases[i][0], detail);
        }
    }
}
