/*
 * Audio Encoder Thread - Header
 */

#ifndef AUDIO_ENCODER_THREAD_H
#define AUDIO_ENCODER_THREAD_H

#include "audio_output.h"

namespace config { struct EmulatorConfig; }

namespace audio {

/**
 * Audio encoder thread entry point
 *
 * Reads audio samples from AudioOutput ring buffer and encodes to Opus.
 * Runs at 50 Hz (20ms frames) until g_running is set to false.
 *
 * @param audio_output Ring buffer to read samples from
 * @param config       Live config: nothing is sent while audio_enabled is off
 */
void audio_encoder_main(AudioOutput* audio_output, const config::EmulatorConfig* config);

} // namespace audio

#endif // AUDIO_ENCODER_THREAD_H
