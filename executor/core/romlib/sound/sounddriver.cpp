/* Copyright 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <sound/sounddriver.h>
#include <sound/soundfake.h>
#include <prefs/prefs.h>

namespace Executor
{

/* This is the current sound driver. */
SoundDriver *sound_driver = nullptr;

SoundDriver::~SoundDriver()
{
}

void sound_init(void)
{
    if (!sound_driver)
        sound_driver = new SoundFake();

    sound_driver->sound_init();
    /* MacPhoenix: a driver that really plays (phoenix_sound.cpp) turns the
       Sound Manager on; the fake one leaves it pretending, as before. */
    if(sound_driver->sound_works() && !sound_driver->sound_silent())
        ROMlib_PretendSound = soundon;
}
}
