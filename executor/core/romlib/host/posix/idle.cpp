/* MacPhoenix: the host edge for waiting, natively (rsys/hostidle.h). */
#include <rsys/hostidle.h>

#include <condition_variable>
#include <mutex>

using namespace Executor;

namespace
{
std::mutex wake_mutex;
std::condition_variable wake_cond;
hostidle::Load load;
}

void hostidle::wait(Clock::time_point deadline, bool (*pending)())
{
    /* Checked under the lock wake() takes, so no wake-up is lost. */
    std::unique_lock<std::mutex> lock(wake_mutex);
    if(pending())
        return;
    load.asleep(Clock::now());
    if(deadline != Clock::time_point::max())
        wake_cond.wait_until(lock, deadline);
    else
        wake_cond.wait(lock);
    load.awake(Clock::now());
}

void hostidle::wake()
{
    std::lock_guard<std::mutex> lock(wake_mutex);
    wake_cond.notify_all();
}

void hostidle::tick() {}

double hostidle::busy()
{
    return load.busy(Clock::now());
}

void hostidle::set_yield_hook(void (*)()) {}
