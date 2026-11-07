#pragma once
/*
 * audio.h - the Mac's sound on the host's output, through miniaudio (one
 * header: ALSA/PulseAudio/PipeWire, Core Audio, WASAPI; no Qt Multimedia).
 *
 * Executor hands over 20 ms frames of 48 kHz 16-bit big-endian stereo
 * (executor_host::Config::on_audio, on the emulator thread); push() puts
 * them in a ring of about 170 ms, and says false when it's full, so
 * Executor keeps the frame and offers it again: the device's pace is the
 * Mac's. The device's callback takes from the ring, silence when it's
 * empty.
 */
#include <cstdint>

namespace audio {

/* The output device. False: no sound (push() then drops what it's given). */
bool open();
void close();
bool is_open();

/* 0..1 */
void set_volume(double v);
/* Muted: the device stays, silence goes out. */
void set_enabled(bool on);

/* One frame from Executor (on_audio). */
bool push(const uint8_t *s16be, uint32_t frames);

}  // namespace audio
