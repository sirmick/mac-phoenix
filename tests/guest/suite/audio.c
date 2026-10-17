/* audio.c — the Sound Manager answers and _SysBeep returns. */

#include "suite.h"

#include <Gestalt.h>
#include <OSUtils.h>

#if !GENERATINGCFM
/* The trap itself: CodeWarrior's interfaces declare SysBeep without its
 * inline (as glue in a library neither toolchain links). */
pascal void SuiteSysBeep(short duration) ONEWORDINLINE(0xA9C8);
#define SysBeep SuiteSysBeep
#endif

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
