/* MacPhoenix: the host edge for waiting, natively (rsys/hostidle.h). */
#include <rsys/hostidle.h>

#include <condition_variable>
#include <mutex>

using namespace Executor;

namespace
{
std::mutex wake_mutex;
std::condition_variable wake_cond;
}

void hostidle::wait(Clock::time_point deadline, bool (*pending)())
{
    /* Checked under the lock wake() takes, so no wake-up is lost. */
    std::unique_lock<std::mutex> lock(wake_mutex);
    if(pending())
        return;
    if(deadline != Clock::time_point::max())
        wake_cond.wait_until(lock, deadline);
    else
        wake_cond.wait(lock);
}

void hostidle::wake()
{
    std::lock_guard<std::mutex> lock(wake_mutex);
    wake_cond.notify_all();
}

void hostidle::tick() {}

void hostidle::set_yield_hook(void (*)()) {}
