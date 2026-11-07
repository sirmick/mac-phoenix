/* MacPhoenix: what a full application heap does to the Toolbox's own
   allocations (docs/executor/FullHeap.md).

   On a Mac the guest's NewHandle, NewPtr, NewRgn, NewPalette, NewGWorld
   and OpenPicture answer nil (MemErr -108) when the heap is full, and the
   ROM's internal allocations (TENew, NewDialog, FillRect with a pattern,
   the region calls) end in system error 25, dsMemFullErr. Executor's C++
   wrote through the nil instead. Executor is also far faster than a
   Quadra, so a benchmark that leaves memory behind each pass (Speedometer:
   a 4K palette per window) fills a heap a Quadra never fills. So: an
   allocation Executor makes while serving any other routine retries in
   the System heap, which keeps a floor; past the floor it is
   dsMemFullErr, as the Mac. Decided by the routine being served
   (traps::served_entry), not per call site. */

#include <base/common.h>
#include <MemoryMgr.h>
#include <SysErr.h>
#include <LowMem.h>

#include <mman/mman.h>
#include <mman/mman_private.h>
#include <mman/fullheap.h>
#include <base/traps.h>
#include <error/error.h>

#include <set>
#include <algorithm>
#include <string>
#include <utility>
#include <stdio.h>
#include <unistd.h>

using namespace Executor;

namespace
{
constexpr INTEGER ds_mem_full_err = 25; /* dsMemFullErr (Inside Macintosh: System errors) */
uint32_t fallbacks;
std::set<std::pair<std::string, uint32_t>> logged; /* (routine, ApplZone) */
std::set<uint32_t> ran_out; /* zones that have run out (their reserve applies) */
}

void Executor::ROMlib_full_heap_ran_out(THz zone)
{
    ran_out.insert(US_TO_SYN68K(zone));
}

void Executor::ROMlib_full_heap_zone_reset(THz zone)
{
    ran_out.erase(US_TO_SYN68K(zone));
}

uint32_t Executor::ROMlib_full_heap_fallbacks()
{
    return fallbacks;
}

void Executor::ROMlib_full_heap_reset()
{
    fallbacks = 0;
    logged.clear();
    ran_out.clear();
}

int32_t Executor::ROMlib_full_heap_system_room()
{
    return ZONE_ZCB_FREE(LM(SysZone)) + std::max<int32_t>(ROMlib_syszone_reserve_left(), 0);
}

full_heap_answer Executor::ROMlib_full_heap_decide(Size size)
{
    /* No routine served: Executor's own code (its launch, a gtest standing
       in for the guest) keeps the Memory Manager's answer. */
    const traps::Entrypoint *e = traps::served_entry;
    if(!e || e->guestAllocator || FullHeapInZone::active())
        return full_heap_answer::fail;
    /* More than the System heap could give at all: a size Executor's code
       tries and copes without (a buffer it then makes smaller, a GWorld
       too big for the machine), not the heap running out. */
    int32_t room = ROMlib_full_heap_system_room();
    if((int64_t)size + HDRSIZE >= room)
        return full_heap_answer::fail;
    if(room - (int32_t)size - HDRSIZE < FULL_HEAP_SYSTEM_FLOOR)
        return full_heap_answer::exhausted;
    return full_heap_answer::system_heap;
}

int32_t Executor::ROMlib_full_heap_appl_reserve(THz zone)
{
    int32_t size = (Ptr)ZONE_BK_LIM(zone) - (Ptr)zone;
    return std::min(FULL_HEAP_APPL_RESERVE, size / 16);
}

bool Executor::ROMlib_full_heap_prefer_system(THz zone, Size size)
{
    if(zone != LM(ApplZone) || zone == LM(SysZone) || !ran_out.count(US_TO_SYN68K(zone))
       || ZONE_ZCB_FREE(zone) - (int32_t)size - HDRSIZE >= ROMlib_full_heap_appl_reserve(zone))
        return false;
    return ROMlib_full_heap_decide(size) == full_heap_answer::system_heap;
}

static const char *served_name()
{
    return traps::served_entry ? traps::served_entry->name : "Executor";
}

void Executor::ROMlib_full_heap_count()
{
    ++fallbacks;
    const char *name = served_name();
    if(logged.insert({ name, (uint32_t)US_TO_SYN68K(LM(ApplZone)) }).second)
        fprintf(stderr, "[full heap] %s: system heap fallback (%u so far; %d K left above the floor)\n",
                name, (unsigned)fallbacks,
                (int)((ROMlib_full_heap_system_room() - FULL_HEAP_SYSTEM_FLOOR) / 1024));
}

void Executor::ROMlib_full_heap_exhausted(Size size)
{
    /* The Mac's SysError(dsMemFullErr) ends the session: Executor's
       SysError is the splash screen's alert (it waits for a click and
       returns), so the session ends here instead, with the host's
       snapshot, rather than a nil the caller writes through. */
    LM(DSErrCode) = ds_mem_full_err;
    char msg[160];
    snprintf(msg, sizeof msg,
             "System error %d (dsMemFullErr): %s needed %d bytes; application heap full, "
             "System heap at its floor (%d K)",
             ds_mem_full_err, served_name(), (int)size, (int)(ROMlib_full_heap_system_room() / 1024));
    fprintf(stderr, "[full heap] %s\n", msg);
    if(auto hook = ROMlib_fatal_hook)
    {
        ROMlib_fatal_hook = nullptr;
        hook(msg);
    }
    fflush(nullptr);
    _exit(70);
}

