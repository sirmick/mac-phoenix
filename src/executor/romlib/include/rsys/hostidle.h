#pragma once

/* MacPhoenix: the host edge for waiting (docs/executor/DIRECTION.md, D3).
 * One .cpp per platform, picked by CMake:
 *
 *   posix  host/posix/idle.cpp: a condition variable other host threads
 *          (sockets, the resolver) wake
 *   wasm   host/wasm/idle.cpp: yields to the browser, where the front end
 *          renders and takes input (set_yield_hook)
 */

#include <chrono>

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
}
