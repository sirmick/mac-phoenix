/* MacPhoenix: the host edge for waiting in WebAssembly (rsys/hostidle.h).
 * Waiting is yielding to the browser (emscripten_sleep, under Asyncify):
 * its event loop delivers the page's input meanwhile, and the front end's
 * hook renders the screen and takes that input on the way back. */
#include <rsys/hostidle.h>

#include <algorithm>
#include <emscripten.h>

using namespace Executor;

/* host/wasm/net.cpp's, when the build has it (EXECUTOR_NET=wasm): what
 * came from the relay meanwhile, into the sockets. */
extern "C" void executor_hostnet_pump() __attribute__((weak));

namespace
{
void (*yield_hook)();
hostidle::Clock::time_point last_yield;
hostidle::Load load;
constexpr auto frame = std::chrono::milliseconds(16);

/* -1: slept already (wait()) */
void yield(int ms)
{
    if(ms >= 0)
        emscripten_sleep(ms);
    last_yield = hostidle::Clock::now();
    if(executor_hostnet_pump)
        executor_hostnet_pump();
    if(yield_hook)
        yield_hook();
}
}

void hostidle::wait(Clock::time_point deadline, bool (*pending)())
{
    while(!pending())
    {
        auto now = Clock::now();
        if(now >= deadline)
            return;
        /* A frame at most, so the screen and input keep flowing. */
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        /* Asleep: the browser's time (the hook's rendering is work). */
        load.asleep(now);
        emscripten_sleep((int)std::clamp<long long>(left.count(), 0, frame.count()));
        load.awake(hostidle::Clock::now());
        yield(-1);
    }
}

/* Nothing runs on another thread here: an interrupt raised during the
 * yield hook is seen by wait()'s pending(). */
void hostidle::wake() {}

void hostidle::tick()
{
    if(Clock::now() - last_yield >= frame)
        yield(0);
}

double hostidle::busy()
{
    return load.busy(Clock::now());
}

void hostidle::set_yield_hook(void (*hook)())
{
    yield_hook = hook;
}
