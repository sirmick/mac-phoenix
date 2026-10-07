/* Copyright 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <sound/sounddriver.h>
#include <sound/soundfake.h>

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
}
}
