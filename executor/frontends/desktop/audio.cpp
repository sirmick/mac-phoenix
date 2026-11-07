/*
 * audio.cpp - the Mac's sound through miniaudio (audio.h).
 */
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include "miniaudio.h"

#include "audio.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <vector>

namespace audio {

namespace {

constexpr uint32_t RATE = 48000;
constexpr uint32_t RING = 8192;  /* stereo frames: 170 ms (a power of two: the counters wrap) */

ma_device device;
bool opened = false;
std::atomic<float> volume{0.8f};
std::atomic<bool> enabled{true};

/* Single producer (the emulator thread), single consumer (the device). */
std::vector<int16_t> ring(RING * 2);
std::atomic<uint32_t> head{0}, tail{0};  /* frames written, read */

void callback(ma_device *, void *out, const void *, ma_uint32 frames)
{
    auto *o = (int16_t *)out;
    uint32_t t = tail.load(std::memory_order_relaxed);
    uint32_t avail = head.load(std::memory_order_acquire) - t;
    uint32_t n = std::min<uint32_t>(avail, frames);
    float v = enabled ? volume.load() : 0.0f;
    for(uint32_t i = 0; i < n; i++)
    {
        uint32_t at = (t + i) % RING;
        o[2 * i] = (int16_t)(ring[2 * at] * v);
        o[2 * i + 1] = (int16_t)(ring[2 * at + 1] * v);
    }
    for(uint32_t i = n; i < frames; i++)
        o[2 * i] = o[2 * i + 1] = 0;
    tail.store(t + n, std::memory_order_release);
}

}

bool open()
{
    if(opened)
        return true;
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_s16;
    cfg.playback.channels = 2;
    cfg.sampleRate = RATE;
    cfg.dataCallback = callback;
    cfg.periodSizeInMilliseconds = 10;
    if(ma_device_init(nullptr, &cfg, &device) != MA_SUCCESS)
    {
        fprintf(stderr, "[desktop] no sound output device\n");
        return false;
    }
    if(ma_device_start(&device) != MA_SUCCESS)
    {
        ma_device_uninit(&device);
        fprintf(stderr, "[desktop] the sound output device won't start\n");
        return false;
    }
    fprintf(stderr, "[desktop] sound: %s (%s)\n", device.playback.name,
            ma_get_backend_name(device.pContext->backend));
    opened = true;
    return true;
}

void close()
{
    if(!opened)
        return;
    fprintf(stderr, "[desktop] sound: %.1f s from the Mac\n", head.load() / (double)RATE);
    ma_device_uninit(&device);
    opened = false;
}

bool is_open() { return opened; }

void set_volume(double v) { volume = (float)std::clamp(v, 0.0, 1.0); }

void set_enabled(bool on) { enabled = on; }

bool push(const uint8_t *s16be, uint32_t frames)
{
    if(!opened)
        return true;  /* dropped: the Mac plays on */
    uint32_t h = head.load(std::memory_order_relaxed);
    if(RING - (h - tail.load(std::memory_order_acquire)) < frames)
        return false;
    for(uint32_t i = 0; i < frames; i++)
    {
        uint32_t at = (h + i) % RING;
        const uint8_t *p = s16be + 4 * i;
        ring[2 * at] = (int16_t)(p[0] << 8 | p[1]);
        ring[2 * at + 1] = (int16_t)(p[2] << 8 | p[3]);
    }
    head.store(h + frames, std::memory_order_release);
    return true;
}

}  // namespace audio
