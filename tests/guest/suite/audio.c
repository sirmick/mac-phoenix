/* audio.c — the Sound Manager answers and _SysBeep returns. */

#include "suite.h"

#include <Gestalt.h>
#include <OSUtils.h>

void test_audio(void)
{
    long attr;

    if(Gestalt(gestaltSoundAttr, &attr) != noErr)
    {
        report_fail("audio_sysbeep", "no Sound Manager (Gestalt 'snd ')");
        return;
    }
    SysBeep(1);
    report_pass("audio_sysbeep");
}
