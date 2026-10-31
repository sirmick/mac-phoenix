/* audio.c — the Sound Manager answers and _SysBeep returns. */

#include "suite.h"

#include <Gestalt.h>
#include <OSUtils.h>
#include <Sound.h>
#include <stdio.h>

#if !GENERATINGCFM
/* The trap itself: CodeWarrior's interfaces declare SysBeep without its
 * inline (as glue in a library neither toolchain links). */
pascal void SuiteSysBeep(short duration) ONEWORDINLINE(0xA9C8);
#define SysBeep SuiteSysBeep
#else
/* Code fragments import it from InterfaceLib (declared there for that). */
#include <Sound.h>
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
    {
        /* what games check before they play (Afterlife, OIDS: "out-of-date
           Sound Manager"): the attributes and the Sound Manager's version */
        NumVersion v = SndSoundManagerVersion();
        char note[80];
        sprintf(note, "snd attr $%08lX, Sound Manager %d.%d.%d", attr,
                v.majorRev, v.minorAndBugRev >> 4, v.minorAndBugRev & 15);
        report_note(note);
        /* the Quadra 650 on 7.6.1: $1C7F, 3.2.1; the G3 on 9.0.4: $1FFF, 3.6.4 */
        if(v.majorRev < 3 || !(attr & (1L << gestalt16BitAudioSupport)))
            report_fail("audio_sound_manager_3", note);
        else
            report_pass("audio_sound_manager_3");
    }
}
