/*
 * test_audio.c - Sound Manager tests
 *
 * Tests: open channel, play a short buffer, close channel.
 * Verifies the Sound Manager driver is functional.
 */
#include <Sound.h>
#include <Memory.h>
#include <string.h>

#include "test_report.h"

/* 1kHz square wave, 22kHz sample rate, 256 samples (~12ms) */
#define SAMPLE_RATE rate22khz
#define NUM_SAMPLES 256

void test_audio(void)
{
    SndChannelPtr chan = NULL;
    SndCommand cmd;
    ExtSoundHeader header;
    OSErr err;
    unsigned char *samples;
    int i;

    /* Allocate channel */
    err = SndNewChannel(&chan, sampledSynth, initMono, NULL);
    if (err != noErr) {
        report_fail("audio_open_channel", err);
        return;
    }
    report_pass("audio_open_channel");

    /* Build a simple square wave buffer */
    samples = (unsigned char *)NewPtr(NUM_SAMPLES);
    if (samples == NULL) {
        report_fail("audio_alloc_buffer", MemError());
        SndDisposeChannel(chan, true);
        return;
    }

    for (i = 0; i < NUM_SAMPLES; i++) {
        samples[i] = (i & 0x10) ? 0xFF : 0x00;  /* square wave */
    }

    memset(&header, 0, sizeof(header));
    header.samplePtr = (Ptr)samples;
    header.numChannels = 1;
    header.sampleRate = SAMPLE_RATE;
    header.encode = extSH;
    header.numFrames = NUM_SAMPLES;
    header.sampleSize = 8;

    /* Play via bufferCmd */
    cmd.cmd = bufferCmd;
    cmd.param1 = 0;
    cmd.param2 = (long)&header;
    err = SndDoCommand(chan, &cmd, false);
    if (err != noErr) {
        report_fail("audio_play_buffer", err);
    } else {
        report_pass("audio_play_buffer");
    }

    /* Quiet flush */
    cmd.cmd = quietCmd;
    cmd.param1 = 0;
    cmd.param2 = 0;
    SndDoCommand(chan, &cmd, false);

    SndDisposeChannel(chan, true);
    DisposePtr((Ptr)samples);
    report_pass("audio_close_channel");
}
