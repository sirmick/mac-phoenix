/* MacPhoenix: the host edge for waiting in WebAssembly (rsys/hostidle.h).
 * Waiting is yielding to the browser (emscripten_sleep, under Asyncify):
 * its event loop delivers the page's input meanwhile, and the front end's
 * hook renders the screen and takes that input on the way back. */
#include <rsys/hostidle.h>

#include <algorithm>
#include <emscripten.h>

using namespace Executor;

namespace
{
void (*yield_hook)();
hostidle::Clock::time_point last_yield;
constexpr auto frame = std::chrono::milliseconds(16);

void yield(int ms)
{
    emscripten_sleep(ms);
    last_yield = hostidle::Clock::now();
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
        yield((int)std::clamp<long long>(left.count(), 0, frame.count()));
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

void hostidle::set_yield_hook(void (*hook)())
{
    yield_hook = hook;
}
