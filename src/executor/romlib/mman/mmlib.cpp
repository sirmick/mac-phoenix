/* mmlib.cpp — the "Memory Manager" library of Mac OS 8.6 and 9: named
 * heaps, which the Finder makes its objects in ("Finder Object Heap")
 * (multiversal/defs/MemoryManagerLib.yaml; docs/executor/MacOS8-9.md).
 *
 * The forms are from the Finder's calls, not from Apple's code. A heap is
 * a record of Executor's; its blocks come from temporary memory (the
 * Process Manager's zone), where a large heap of a small partition lives on
 * a Mac too, else the current zone, and MMFree disposes of them wherever
 * they are. The free space is that zone's: total free and largest block.
 */

#include <base/common.h>
#include <MemoryMgr.h>
#include <MemoryManagerLib.h>
#include <rsys/process.h>
#include <mman/mman.h>
#include <base/traps.h>

#include <cstring>

using namespace Executor;

struct Executor::MMHeap
{
    GUEST_STRUCT;
    GUEST<uint32_t> magic;
    GUEST<uint32_t> kind;
    char name[32];
};

namespace
{
constexpr uint32_t kMagic = 0x4D4D6870; /* 'MMhp' */

THz heap_zone()
{
    return ROMlib_pm_zone ? ROMlib_pm_zone : (THz)LM(TheZone);
}

MMHeapRef new_heap(uint32_t kind, const char *name)
{
    MMHeapRef h = (MMHeapRef)NewPtrSysClear(sizeof(MMHeap));
    if(!h)
        return nullptr;
    h->magic = kMagic;
    h->kind = kind;
    if(name)
        strncpy(h->name, name, sizeof h->name - 1);
    return h;
}

Ptr allocate(Size size, bool clear)
{
    Ptr p;
    {
        TheZoneGuard guard(heap_zone());
        p = clear ? NewPtrClear(size) : NewPtr(size);
    }
    if(!p)
        p = clear ? NewPtrClear(size) : NewPtr(size);
    return p;
}
}

MMHeapRef Executor::C_MMNewHeap(uint32_t kind, Size initialSize, Size growSize, const char *name)
{
    return new_heap(kind, name);
}

MMHeapRef Executor::C_MMGetDefaultHeap()
{
    static MMHeapRef def;
    if(!def)
        def = new_heap(0, "default");
    return def;
}

Ptr Executor::C_MMAllocateIn(Size size, MMHeapRef)
{
    return allocate(size, false);
}

Ptr Executor::C_MMAllocateClearIn(Size size, MMHeapRef)
{
    return allocate(size, true);
}

OSErr Executor::C_MMFree(Ptr block)
{
    if(block)
        DisposePtr(block);
    return noErr;
}

OSErr Executor::C_MMGetFreeSpace(MMHeapRef, GUEST<Size> *total, GUEST<Size> *contiguous)
{
    TheZoneGuard guard(heap_zone());
    if(total)
        *total = FreeMem();
    if(contiguous)
        *contiguous = MaxBlock();
    return noErr;
}
