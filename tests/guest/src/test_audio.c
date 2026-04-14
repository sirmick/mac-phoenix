/*
 * test_audio.c - Sound Manager tests
 *
 * Tests: open channel, play a short buffer, verify playback completes,
 * close channel.  Verifies the Sound Manager driver is functional and
 * that the audio pipeline actually drains queued buffers.
 */
#include <Sound.h>
#include <Memory.h>
#include <Timer.h>
#include <string.h>

#include "test_report.h"

/* 1kHz square wave, 22kHz sample rate, 256 samples (~12ms) */
#define SAMPLE_RATE rate22khz
#define NUM_SAMPLES 256

/* Callback flag — set by callBackCmd when the buffer finishes playing */
static volatile long gCallbackFired = 0;

static pascal void sound_callback(SndChannelPtr chan, SndCommand *cmd)
{
    (void)chan;
    (void)cmd;
    gCallbackFired = 1;
}

void test_audio(void)
{
    SndChannelPtr chan = NULL;
    SndCommand cmd;
    ExtSoundHeader header;
    OSErr err;
    unsigned char *samples;
    int i;

    /* Allocate channel with callback */
    err = SndNewChannel(&chan, sampledSynth, initMono,
                        NewSndCallBackUPP(sound_callback));
    if (err != noErr) {
        report_skip("audio_open_channel", "Sound Manager not available");
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
        DisposePtr((Ptr)samples);
        SndDisposeChannel(chan, true);
        return;
    }
    report_pass("audio_play_buffer");

    /* Queue a callBackCmd — fires after the buffer finishes playing.
     * If the audio pipeline is actually draining buffers, this callback
     * will fire within a few hundred ms. If the pipeline is a stub that
     * never calls AudioInterrupt, the callback never fires. */
    gCallbackFired = 0;
    cmd.cmd = callBackCmd;
    cmd.param1 = 0;
    cmd.param2 = 0;
    err = SndDoCommand(chan, &cmd, false);
    if (err != noErr) {
        report_fail("audio_drain_callback", err);
    } else {
        /* Wait up to 2 seconds for the callback to fire */
        unsigned long deadline = *(unsigned long *)0x016A + 120;  /* Ticks + 2s */
        while (!gCallbackFired && *(unsigned long *)0x016A < deadline) {
            /* spin — jGNEFilter and interrupts still run */
        }

        if (gCallbackFired) {
            report_pass("audio_drain_callback");
        } else {
            report_fail("audio_drain_callback", -1);
        }
    }

    SndDisposeChannel(chan, true);
    DisposePtr((Ptr)samples);
    report_pass("audio_close_channel");
}
