/* The Time Manager (romlib/time/time.cpp): RmvTime leaves the time
   remaining in tmCount, negative microseconds or positive milliseconds as
   the task was primed, 0 once it is due (Inside Macintosh: Processes,
   3-21). Speedometer 4.0 times its tests that way. */

#include "gtest/gtest.h"

#include <base/common.h>
#include <MemoryMgr.h>
#include <TimeMgr.h>
#include <time/time.h>

#include <chrono>
#include <thread>

using namespace Executor;

namespace
{
TMTask *task()
{
    TMTask *t = (TMTask *)NewPtrClear(sizeof(TMTask));
    InsXTime((QElemPtr)t);
    return t;
}
}

TEST(TimeMgr, RmvTimeLeavesMicrosecondsRemaining)
{
    TMTask *t = task();
    PrimeTime((QElemPtr)t, -1800000000);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    RmvTime((QElemPtr)t);
    LONGINT elapsed = 1800000000 + t->tmCount;
    EXPECT_GE(elapsed, 20000);
    EXPECT_LT(elapsed, 2000000);
    DisposePtr((Ptr)t);
}

TEST(TimeMgr, RmvTimeLeavesMillisecondsRemaining)
{
    TMTask *t = task();
    PrimeTime((QElemPtr)t, 100000);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    RmvTime((QElemPtr)t);
    EXPECT_LE(t->tmCount, 100000 - 20);
    EXPECT_GT(t->tmCount, 100000 - 2000);
    DisposePtr((Ptr)t);
}

TEST(TimeMgr, RmvTimeAfterTheTaskIsDueLeavesZero)
{
    TMTask *t = task();
    PrimeTime((QElemPtr)t, -1000);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    RmvTime((QElemPtr)t);
    EXPECT_EQ(0, t->tmCount);
    DisposePtr((Ptr)t);
}

TEST(TimeMgr, MicrosecondsHasMicrosecondResolution)
{
    uint64_t a = usecs_elapsed(), b;
    while((b = usecs_elapsed()) == a)
        ;
    EXPECT_LT(b - a, 1000u);
}
