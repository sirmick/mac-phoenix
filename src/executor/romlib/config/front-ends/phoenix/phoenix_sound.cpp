/* phoenix_sound.cpp - see phoenix_sound.h. */
#include <base/common.h>
#include "phoenix_sound.h"
#include <base/m68kint.h>
#include <base/cpu.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace Executor;

namespace
{
const int kSourceRate = 22255;     // Executor mixes at this rate
const int kOutRate = 48000;        // the pipeline's rate (Opus)
const int kOutChannels = 2;
const int kFrameSamples = kOutRate / 50;              // 20 ms
const size_t kFrameBytes = kFrameSamples * kOutChannels * 2;
const int kBufBytes = 1024;        // ~46 ms of source per hunger
const int kMsPerBuf = kBufBytes * 1000 / kSourceRate;
const size_t kMaxPending = kFrameBytes * 50;          // a second, then drop
uint8_t mix_buf[kBufBytes];
}

std::function<bool(const uint8_t *, uint32_t)> PhoenixSoundDriver::pushHook;

bool PhoenixSoundDriver::sound_init()
{
    callback_ = callback_install(timer_callback, this);
    shutting_down_ = false;
    enqueued_ = 0;
    return true;
}

void PhoenixSoundDriver::sound_shutdown()
{
    shutting_down_ = true;
    enqueued_ = 0;
}

bool PhoenixSoundDriver::sound_works()
{
    return !shutting_down_ && (bool)pushHook;
}

bool PhoenixSoundDriver::sound_silent()
{
    return false;
}

HungerInfo PhoenixSoundDriver::HungerStart()
{
    HungerInfo info;
    memset(mix_buf, 0x80, sizeof mix_buf); /* silence, 8-bit unsigned */
    info.buf = mix_buf;
    info.bufsize = kBufBytes;
    info.rate = kSourceRate;
    info.t2 = t1_ + kBufBytes;
    info.t3 = info.t2 + kBufBytes;
    return info;
}

/* The mixed buffer: to 48 kHz stereo S16MSB, linear interpolation. */
void PhoenixSoundDriver::convert_and_queue()
{
    const double step = (double)kSourceRate / kOutRate;
    bool audible = false;
    while(resample_pos_ < kBufBytes)
    {
        int i = (int)resample_pos_;
        double frac = resample_pos_ - i;
        int16_t a = i > 0 ? (int16_t)((mix_buf[i - 1] - 128) << 8) : last_sample_;
        int16_t b = (int16_t)((mix_buf[i] - 128) << 8);
        int16_t s = (int16_t)(a + (b - a) * frac);
        if(s)
            audible = true;
        uint8_t hi = (uint8_t)(s >> 8), lo = (uint8_t)s;
        pending_.push_back(hi);
        pending_.push_back(lo);
        pending_.push_back(hi);
        pending_.push_back(lo);
        resample_pos_ += step;
    }
    resample_pos_ -= kBufBytes;
    last_sample_ = (int16_t)((mix_buf[kBufBytes - 1] - 128) << 8);
    if(audible && !announced_)
    {
        announced_ = true;
        fprintf(stderr, "[Executor] sound: first audible buffer (%d Hz -> %d Hz stereo)\n",
                kSourceRate, kOutRate);
    }
    if(pending_.size() > kMaxPending)
        pending_.erase(pending_.begin(), pending_.begin() + (pending_.size() - kMaxPending));
}

void PhoenixSoundDriver::push_frames()
{
    while(pending_.size() >= kFrameBytes)
    {
        if(!pushHook || !pushHook(pending_.data(), kFrameSamples))
            break;
        pending_.erase(pending_.begin(), pending_.begin() + kFrameBytes);
    }
}

void PhoenixSoundDriver::HungerFinish()
{
    static int hungers;
    if(hungers++ == 0)
        fprintf(stderr, "[Executor] sound: mixing started\n");
    t1_ += kBufBytes;
    convert_and_queue();
    push_frames();
    ++enqueued_;
    if(enqueued_ == 1)
        arm_timer();
    note_sound_interrupt(); /* mix ahead, as SoundFake does */
}

void PhoenixSoundDriver::note_sound_interrupt()
{
    if(!shutting_down_ && enqueued_ < 2)
        sound_callback(0, nullptr);
}

void PhoenixSoundDriver::arm_timer()
{
    task_.tmAddr = guest_cast<ProcPtr>((uint32_t)callback_);
    InsTime((QElemPtr)&task_);
    PrimeTime((QElemPtr)&task_, kMsPerBuf);
}

/* A buffer's worth of time has passed: it has "played". */
syn68k_addr_t PhoenixSoundDriver::timer_callback(syn68k_addr_t addr, void *self_)
{
    PhoenixSoundDriver *self = (PhoenixSoundDriver *)self_;
    if(self->enqueued_ > 0)
        --self->enqueued_;
    self->push_frames();
    if(!self->shutting_down_ && self->enqueued_ > 0)
    {
        M68kReg saved_regs[16];
        CCRElement saved_ccnz, saved_ccn, saved_ccc, saved_ccv, saved_ccx;
        memcpy(saved_regs, &cpu_state.regs, sizeof saved_regs);
        saved_ccnz = cpu_state.ccnz;
        saved_ccn = cpu_state.ccn;
        saved_ccc = cpu_state.ccc;
        saved_ccv = cpu_state.ccv;
        saved_ccx = cpu_state.ccx;
        self->arm_timer();
        self->note_sound_interrupt();
        memcpy(&cpu_state.regs, saved_regs, sizeof saved_regs);
        cpu_state.ccnz = saved_ccnz;
        cpu_state.ccn = saved_ccn;
        cpu_state.ccc = saved_ccc;
        cpu_state.ccv = saved_ccv;
        cpu_state.ccx = saved_ccx;
    }
    return POPADDR();
}

void PhoenixSoundDriver::sound_go()
{
    note_sound_interrupt();
}

void PhoenixSoundDriver::sound_stop()
{
}

void PhoenixSoundDriver::sound_clear_pending()
{
    task_.tmCount = -1;
    enqueued_ = 0;
    pending_.clear();
}
