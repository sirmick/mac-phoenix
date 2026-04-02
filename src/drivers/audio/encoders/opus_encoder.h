/*
 * Opus Audio Encoder for macemu WebRTC Streaming
 *
 * Handles:
 * - 48kHz audio encoding (matches Mac audio system output)
 * - Dynamic channel changes (mono/stereo)
 * - Low-latency encoding (20ms frames)
 */

#ifndef OPUS_ENCODER_H
#define OPUS_ENCODER_H

#include <vector>
#include <cstdint>
#include "audio_config.h"

#ifdef HAVE_OPUS

#include <opus/opus.h>

class OpusAudioEncoder {
public:
    OpusAudioEncoder();
    ~OpusAudioEncoder();

    // Initialize encoder
    bool init(int output_sample_rate = AUDIO_SAMPLE_RATE, int channels = AUDIO_CHANNELS, int bitrate = OPUS_BITRATE);

    // Cleanup
    void cleanup();

    // Encode PCM samples (S16LE input)
    std::vector<uint8_t> encode(const int16_t* pcm, int frame_size);

    // Get frame size in samples (from audio_config.h)
    int get_frame_size() const { return frame_size_; }

    // Get encoder sample rate
    int get_sample_rate() const { return sample_rate_; }

    // Get encoder channels
    int get_channels() const { return channels_; }

private:
    OpusEncoder* encoder_ = nullptr;
    int sample_rate_ = AUDIO_SAMPLE_RATE;
    int channels_ = AUDIO_CHANNELS;
    int frame_size_ = AUDIO_FRAME_SIZE;
    int bitrate_ = OPUS_BITRATE;
};

#else // !HAVE_OPUS

class OpusAudioEncoder {
public:
    OpusAudioEncoder() = default;
    ~OpusAudioEncoder() = default;
    bool init(int = AUDIO_SAMPLE_RATE, int = AUDIO_CHANNELS, int = OPUS_BITRATE) { return false; }
    void cleanup() {}
    std::vector<uint8_t> encode(const int16_t*, int) { return {}; }
    int get_frame_size() const { return AUDIO_FRAME_SIZE; }
    int get_sample_rate() const { return AUDIO_SAMPLE_RATE; }
    int get_channels() const { return AUDIO_CHANNELS; }
};

#endif // HAVE_OPUS

#endif // OPUS_ENCODER_H
