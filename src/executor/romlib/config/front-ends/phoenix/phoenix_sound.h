#pragma once
/*
 * phoenix_sound.h - Executor's sound output on MacPhoenix (M3g).
 *
 * The Sound Manager (sound/sound.cpp) mixes every channel into the buffer
 * the driver offers at HungerStart, 8-bit unsigned mono at 22255 Hz, and
 * calls HungerFinish; the driver keeps the mixing going with a Time Manager
 * task while channels play, as SoundFake does. Here each finished buffer is
 * resampled to the pipeline's 48 kHz, 16-bit big-endian stereo and handed
 * to the host in 20 ms frames through pushHook (the IPC audio ring), where
 * the parent encodes it (Opus) for the web UI, like the ROM machines'
 * audio.
 */
#include <sound/sounddriver.h>
#include <TimeMgr.h>

#include <cstdint>
#include <functional>
#include <vector>

class PhoenixSoundDriver : public Executor::SoundDriver
{
public:
    // One 20 ms frame: 960 frames of S16MSB stereo at 48 kHz. Returns
    // false when the host cannot take it now.
    static std::function<bool(const uint8_t *s16be, uint32_t samples)> pushHook;

    bool sound_init() override;
    void sound_shutdown() override;
    bool sound_works() override;
    bool sound_silent() override;
    void sound_go() override;
    void sound_stop() override;
    Executor::HungerInfo HungerStart() override;
    void HungerFinish() override;
    void sound_clear_pending() override;

private:
    static syn68k_addr_t timer_callback(syn68k_addr_t addr, void *self);
    void note_sound_interrupt();
    void arm_timer();
    void convert_and_queue();
    void push_frames();

    Executor::snd_time t1_ = 0;
    int enqueued_ = 0;          // buffers mixed but not yet "played"
    syn68k_addr_t callback_ = 0;
    bool shutting_down_ = false;
    Executor::TMTask task_ = {};
    double resample_pos_ = 0;   // position in the source buffer
    int16_t last_sample_ = 0;   // for interpolation across buffers
    std::vector<uint8_t> pending_;  // S16MSB stereo frames not yet pushed
    bool announced_ = false;
};
