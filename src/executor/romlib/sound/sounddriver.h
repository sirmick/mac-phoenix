#if !defined(_RSYS_SOUNDDRIVER_H_)
#define _RSYS_SOUNDDRIVER_H_

#include <sound/soundopts.h>

namespace Executor
{


struct HungerInfo
{
    snd_time t2; /* Time of earliest sample which can be provided */
    snd_time t3; /* Time of latest sample which must be provided */
    unsigned char *buf; /* nullptr means there is no buffer; just "pretend" */
    int bufsize; /* to fill it in; (!buf && bufsize) is possible! */
    int rate;
};


class SoundDriver
{
public:
    virtual bool sound_init() = 0;
    virtual void sound_shutdown() = 0;
    virtual bool sound_works() = 0;
    virtual bool sound_silent() = 0;
    virtual void sound_go() = 0;
    virtual void sound_stop() = 0;
    virtual HungerInfo HungerStart() = 0;
    virtual void HungerFinish() = 0;
    virtual void sound_clear_pending() {};

    virtual ~SoundDriver();
};

/* Current sound driver in use. */
extern SoundDriver *sound_driver;

extern void sound_init(void);

/* MacPhoenix: the output's volume, right << 16 | left, each 0 to $100 (the
   Sound Manager's scale), and its mute; the front-end applies them to what
   it sends out. Set by SetDefaultOutputVolume and the output device
   component (Sound control panel, Volumes). */
extern uint32_t ROMlib_sound_output_volume;
extern bool ROMlib_sound_output_muted;

/* The output device component ('sdev' "Built-in", native): registered
   before the System's components, which then leave theirs out. */
extern void ROMlib_install_sound_output_component();
}

#endif /* !_RSYS_SOUNDDRIVER_H_ */
