#pragma once

/* MacPhoenix: the Toolbox's allocations when the application heap is
   full (fullheap.cpp, docs/executor/FullHeap.md). */

#include <MemoryMgr.h>

namespace Executor
{
/* What the System heap keeps for everyone else: the Finder's and the
   other processes' resource loads, fonts, file control blocks and the
   Process Manager's launches all come from it. 256K is the most any of
   those took at once in the measurements (FullHeap.md); a fallback that
   would leave less is dsMemFullErr. */
constexpr int32_t FULL_HEAP_SYSTEM_FLOOR = 256 * 1024;

enum class full_heap_answer
{
    fail,        /* the guest's own allocation call: nil, MemErr -108 */
    system_heap, /* the Toolbox's: retry in the System heap */
    exhausted,   /* the Toolbox's, the System heap at its floor: dsMemFullErr */
};

/* For an allocation of `size` that failed outside the System heap. */
full_heap_answer ROMlib_full_heap_decide(Size size);
/* A fallback is being made: counted, logged once per routine and process. */
void ROMlib_full_heap_count();
/* System error 25: ends the session. */
[[noreturn]] void ROMlib_full_heap_exhausted(Size size);

/* The System heap's free space plus the room it can grow into. */
int32_t ROMlib_full_heap_system_room();
uint32_t ROMlib_full_heap_fallbacks();
void ROMlib_full_heap_reset(); /* the count and the log (tests) */
}
