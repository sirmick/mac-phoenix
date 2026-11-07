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
#include <base/cpu.h>
#include <rsys/process.h>
#include <error/error.h>

#include <set>
#include <unordered_map>
#include <vector>
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

/* The fallback blocks still allocated: handle (its master pointer's
   address) or pointer, by the process they were made for. */
struct listed_t
{
    uintptr_t owner;
    bool handle;
};
std::unordered_map<uint32_t, listed_t> listed;
/* Blocks of processes that have ended, kept then because something named
   them: tried again at every process's end. */
std::unordered_map<uint32_t, bool> kept_before; /* address -> handle */
}

size_t Executor::ROMlib_full_heap_tracked = 0;

void Executor::ROMlib_full_heap_forget(void *block)
{
    listed.erase(US_TO_SYN68K(block));
    kept_before.erase(US_TO_SYN68K(block));
    ROMlib_full_heap_tracked = listed.size() + kept_before.size();
}

namespace
{
/* A listed block as it is now in the System heap: its data [lo, lo+len)
   (len 0: a purged handle), or false when it isn't a block of the System
   heap's any more. */
bool block_now(uint32_t a, bool handle, uint32_t *lo, uint32_t *len)
{
    THz sys = LM(SysZone);
    uint32_t zlo = US_TO_SYN68K((Ptr)ZONE_HEAP_DATA(sys)), zhi = US_TO_SYN68K((Ptr)ZONE_BK_LIM(sys));
    auto inside = [&](uint32_t x) { return x >= zlo && x < zhi; };
    block_header_t *b;
    if(handle)
    {
        if(!inside(a))
            return false;
        Handle h = (Handle)SYN68K_TO_US(a);
        if(!*h)
        {
            *lo = *len = 0;
            return true;
        }
        if(!inside(US_TO_SYN68K((Ptr)*h)))
            return false;
        b = HANDLE_TO_BLOCK(h);
        if(USE(b) != REL || BLOCK_TO_HANDLE(sys, b) != h)
            return false;
    }
    else
    {
        if(!inside(a))
            return false;
        b = POINTER_TO_BLOCK((Ptr)SYN68K_TO_US(a));
        if(USE(b) != NREL)
            return false;
    }
    *lo = US_TO_SYN68K(BLOCK_DATA(b));
    *len = LSIZE(b);
    return true;
}

struct span
{
    uint32_t lo, hi;
    int block; /* index in the process's blocks; -1: not one (excluded) */
    bool operator<(const span &o) const { return lo < o.lo; }
};

/* The span holding x, or nullptr. */
const span *find_span(const std::vector<span> &v, uint32_t x)
{
    auto it = std::upper_bound(v.begin(), v.end(), span{ x, 0, 0 });
    if(it == v.begin())
        return nullptr;
    --it;
    return x < it->hi ? &*it : nullptr;
}
}

size_t Executor::ROMlib_full_heap_listed(uintptr_t owner, size_t *bytes)
{
    size_t n = 0, b = 0;
    for(auto &[a, l] : listed)
        if(l.owner == owner)
        {
            uint32_t lo, len;
            n++;
            if(block_now(a, l.handle, &lo, &len))
                b += len;
        }
    if(bytes)
        *bytes = b;
    return n;
}

void Executor::ROMlib_full_heap_process_exit(uintptr_t owner, uint32_t part_lo, uint32_t part_hi,
                                             void (*release)(uint32_t lo, uint32_t hi),
                                             const std::vector<std::pair<uint32_t, uint32_t>> &dead)
{
    struct blk
    {
        uint32_t addr, lo, len;
        bool handle;
        bool kept = false;
    };
    std::vector<blk> blocks;
    size_t gone = 0;
    for(auto it = listed.begin(); it != listed.end();)
    {
        if(it->second.owner != owner)
        {
            ++it;
            continue;
        }
        blk b{ it->first, 0, 0, it->second.handle };
        if(block_now(b.addr, b.handle, &b.lo, &b.len))
            blocks.push_back(b);
        else
            gone++; /* freed by some way Executor doesn't list */
        it = listed.erase(it);
    }
    size_t earlier = 0;
    for(auto &[a, handle] : kept_before)
    {
        blk b{ a, 0, 0, handle };
        if(block_now(b.addr, b.handle, &b.lo, &b.len))
        {
            blocks.push_back(b);
            earlier++;
        }
    }
    kept_before.clear();
    ROMlib_full_heap_tracked = listed.size();
    for(auto z = logged.begin(); z != logged.end();)
        z = z->second == (uint32_t)US_TO_SYN68K(LM(ApplZone)) ? logged.erase(z) : std::next(z);
    if(blocks.empty())
        return;

    /* What system-wide lists hold in them goes, as for the partition. */
    if(release)
        for(const blk &b : blocks)
            if(b.len)
                release(b.lo, b.lo + b.len);

    /* What names a block: its handle (the master pointer's address) or
       its pointer, exactly. An address inside a block's data isn't kept
       anywhere for long (a handle's data moves), and pixels, fonts and
       pattern data are full of such values. Not scanned: the blocks
       themselves (their data, a handle's master pointer), the dead
       partition and the System heap's free blocks. */
    std::vector<span> spans, skip;
    for(size_t i = 0; i < blocks.size(); i++)
    {
        spans.push_back({ blocks[i].addr, blocks[i].addr + 1, (int)i });
        if(blocks[i].len)
            skip.push_back({ blocks[i].lo, blocks[i].lo + blocks[i].len, (int)i });
        if(blocks[i].handle)
            skip.push_back({ blocks[i].addr, blocks[i].addr + 4, (int)i });
    }
    if(part_hi > part_lo)
        skip.push_back({ part_lo, part_hi, -1 });
    for(auto [lo, hi] : dead)
        if(hi > lo)
            skip.push_back({ lo, hi, -1 });
    THz sys = LM(SysZone);
    for(THz z : { sys, ROMlib_pm_zone })
        for(block_header_t *b = z ? ZONE_HEAP_DATA(z) : nullptr; b && b < ZONE_BK_LIM(z); b = BLOCK_NEXT(b))
        {
            if(PSIZE(b) < MIN_BLOCK_SIZE)
                break;
            if(USE(b) == FREE)
                skip.push_back({ US_TO_SYN68K((Ptr)b), US_TO_SYN68K((Ptr)b) + (uint32_t)PSIZE(b), -1 });
        }
    std::sort(spans.begin(), spans.end());
    std::sort(skip.begin(), skip.end());
    uint32_t zlo = US_TO_SYN68K((Ptr)sys), zhi = US_TO_SYN68K((Ptr)ZONE_BK_LIM(sys));

    /* Longwords at every even address that name one of the blocks. */
    auto refs = [&](uint32_t from, uint32_t to, auto &&hit) {
        for(uint32_t a = (from + 1) & ~1u; a + 4 <= to; a += 2)
        {
            uint32_t v = READUL(a);
            if(v < zlo || v >= zhi)
                continue;
            if(const span *sp = find_span(spans, v))
                hit(sp->block);
        }
    };
    std::vector<int> work;
    uint32_t memtop = US_TO_SYN68K((void *)ROMlib_memtop);
    uint32_t at = 0;
    auto mark = [&](int i) {
        if(!blocks[i].kept)
        {
            blocks[i].kept = true;
            work.push_back(i);
        }
    };
    for(const span &sp : skip)
    {
        if(sp.lo > at)
            refs(at, std::min(sp.lo, memtop), mark);
        at = std::max(at, sp.hi);
        if(at >= memtop)
            break;
    }
    if(at < memtop)
        refs(at, memtop, mark);
    /* ... and what a kept block names is kept too */
    while(!work.empty())
    {
        int i = work.back();
        work.pop_back();
        if(blocks[i].len)
            refs(blocks[i].lo, blocks[i].lo + blocks[i].len, mark);
    }

    size_t freed = 0, kept = 0, freed_bytes = 0, kept_bytes = 0;
    for(const blk &b : blocks)
    {
        if(b.kept)
        {
            kept++;
            kept_bytes += b.len;
            kept_before[b.addr] = b.handle;
            continue;
        }
        freed++;
        freed_bytes += b.len;
        if(b.handle)
            DisposeHandle((Handle)SYN68K_TO_US(b.addr));
        else
            DisposePtr((Ptr)SYN68K_TO_US(b.addr));
    }
    ROMlib_full_heap_tracked = listed.size() + kept_before.size();
    fprintf(stderr, "[full heap] process exit: %zu fallback blocks (%zu kept at an earlier end): %zu freed (%zu K), "
                    "%zu kept (%zu K, referenced from outside it)%s; System heap %d K free\n",
            blocks.size(), earlier, freed, freed_bytes / 1024, kept, kept_bytes / 1024,
            gone ? " (some freed otherwise)" : "", (int)(ZONE_ZCB_FREE(sys) / 1024));
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

void Executor::ROMlib_full_heap_count(void *block, bool handle)
{
    listed[US_TO_SYN68K(block)] = { ROMlib_process_key(), handle };
    ROMlib_full_heap_tracked = listed.size() + kept_before.size();
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

