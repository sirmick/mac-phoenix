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

/* What the application heap keeps for the guest's own allocations once
   it has run out (a fallback made for it): a sixteenth of it, at most
   64K. From then on the Toolbox's allocation that would leave less free
   goes to the System heap at once. Without it the Toolbox fills the heap
   to the last bytes, and the guest's own NewRgn then gets nil where a
   Quadra's never does (Speedometer's fifth colour depth: OpenRgn with
   it). Not before the heap has run out: a heap that is merely low is
   common, and programs don't expect the Toolbox's handles elsewhere
   (AppleScript's initialisation in MacPerl failed so). */
constexpr int32_t FULL_HEAP_APPL_RESERVE = 64 * 1024;
int32_t ROMlib_full_heap_appl_reserve(THz zone);

enum class full_heap_answer
{
    fail,        /* the guest's own allocation call: nil, MemErr -108 */
    system_heap, /* the Toolbox's: retry in the System heap */
    exhausted,   /* the Toolbox's, the System heap at its floor: dsMemFullErr */
};

/* An allocation that must stay in its zone (MoreMasters: master pointers
   are threaded into the zone's own free list) never falls back. */
class FullHeapInZone
{
    static inline int depth = 0;
public:
    FullHeapInZone() { ++depth; }
    ~FullHeapInZone() { --depth; }
    FullHeapInZone(const FullHeapInZone &) = delete;
    FullHeapInZone &operator=(const FullHeapInZone &) = delete;
    static bool active() { return depth > 0; }
};

/* For an allocation of `size` that failed outside the System heap. */
full_heap_answer ROMlib_full_heap_decide(Size size);
/* An allocation of `size` about to be made in `zone`: true when it is
   the Toolbox's, would take `zone` (the application heap) into its
   reserve, and the System heap can take it above its floor. */
bool ROMlib_full_heap_prefer_system(THz zone, Size size);
/* `zone` has run out: its reserve applies from now on. */
void ROMlib_full_heap_ran_out(THz zone);
/* InitZone: a zone starts afresh (a new partition at the same place). */
void ROMlib_full_heap_zone_reset(THz zone);
/* A fallback was made (the handle or pointer `block`): counted, logged
   once per routine and process, and kept on the current process's list. */
void ROMlib_full_heap_count(void *block, bool handle);
/* DisposeHandle/DisposePtr: `block` is gone (off its process's list). */
extern size_t ROMlib_full_heap_tracked; /* how many are listed */
void ROMlib_full_heap_forget(void *block);
/* The process keyed `owner` (process.cpp) has ended, its partition gone:
   its fallback blocks go too. `release(lo, hi)` first drops what
   system-wide lists hold in each block [lo, hi), as for the partition.
   Logged. */
void ROMlib_full_heap_process_exit(uintptr_t owner, void (*release)(uint32_t lo, uint32_t hi));
/* The fallback blocks listed for `owner`, and their bytes (tests). */
size_t ROMlib_full_heap_listed(uintptr_t owner, size_t *bytes = nullptr);
/* System error 25: ends the session. */
[[noreturn]] void ROMlib_full_heap_exhausted(Size size);

/* The System heap's free space plus the room it can grow into. */
int32_t ROMlib_full_heap_system_room();
uint32_t ROMlib_full_heap_fallbacks();
void ROMlib_full_heap_reset(); /* the count and the log (tests) */
}
