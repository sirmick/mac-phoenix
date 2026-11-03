#pragma once

/* MacPhoenix: the host edge for waiting (docs/executor/DIRECTION.md, D3).
 * One .cpp per platform, picked by CMake:
 *
 *   posix  host/posix/idle.cpp: a condition variable other host threads
 *          (sockets, the resolver) wake
 *   wasm   host/wasm/idle.cpp: yields to the browser, where the front end
 *          renders and takes input (set_yield_hook)
 */

#include <algorithm>
#include <chrono>
#include <mutex>

namespace Executor::hostidle
{
using Clock = std::chrono::steady_clock;

/* Until pending() (checked so that no wake() is lost) or the deadline
 * (Clock::time_point::max(): none). */
void wait(Clock::time_point deadline, bool (*pending)());

/* Ends a wait early; any thread. */
void wake();

/* Between the CPU cores' time slices, on the emulator thread: posix
 * nothing; wasm yields to the browser about once a frame, so the page
 * keeps rendering and taking input while guest code runs. */
void tick();

/* The front end's work at each yield (render, take input). */
void set_yield_hook(void (*hook)());

/* The emulator thread's load: the share of the last half second it spent
 * outside wait() (0..1; -1 before the first half second). Any thread.
 * Timestamps around the waits and at each tick only. */
double busy();

/* busy()'s bookkeeping, one per platform's idle.cpp. */
class Load
{
public:
    static constexpr auto window = std::chrono::milliseconds(500);

    void asleep(Clock::time_point now)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        roll(now);
        since_ = now;
    }
    void awake(Clock::time_point now)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(since_ != Clock::time_point())
            slept_ += now - since_;
        since_ = Clock::time_point();
        roll(now);
    }
    double busy(Clock::time_point now)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        roll(now);
        return last_;
    }

private:
    /* A window over: its share, a new one (a sleep under way counts up to
       now and goes on in the next). */
    void roll(Clock::time_point now)
    {
        if(start_ == Clock::time_point())
            start_ = now;
        auto total = now - start_;
        if(total < window)
            return;
        auto slept = slept_ + (since_ != Clock::time_point() ? now - since_ : Clock::duration());
        last_ = std::clamp(1.0 - (double)slept.count() / (double)total.count(), 0.0, 1.0);
        start_ = now;
        slept_ = Clock::duration();
        if(since_ != Clock::time_point())
            since_ = now;
    }

    std::mutex mutex_;
    Clock::time_point start_, since_;
    Clock::duration slept_{};
    double last_ = -1;
};
}
