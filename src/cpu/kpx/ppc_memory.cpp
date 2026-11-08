/*
 *  ppc_memory.cpp - PPC memory management (SheepMem, Mac allocators)
 *
 *  SheepMem is the shared memory region for Mac/host communication
 *  (thunks, procedures, data exchange). From SheepShaver main_unix.cpp.
 *
 *  Also provides Microseconds() and the ppc::InstallExtFS bridge.
 */

#include "sysdeps.h"
#include "kpx_cpu_emulation.h"
#include "macos_util.h"
#include "xlowmem.h"
#include "thunks.h"
#include "main.h"
#include "vm_alloc.h"
#include "platform.h"

#include <cstdio>
#include <ctime>
#include <sys/mman.h>
#include <unistd.h>

// Expose SheepMem statics for trace logging
uintptr SheepMem_base = 0, SheepMem_proc = 0, SheepMem_data = 0;

// SheepMem — shared memory region for Mac/host communication.
// From SheepShaver main_unix.cpp.
bool SheepMem::Init(void)
{
    page_size = getpagesize();

    // Allocate via vm_acquire (sequential from MAP_BASE = 0x10000000).
    // Must NOT use a fixed address — that breaks JIT code cache placement.
    void *adr = vm_acquire(size);
    if (adr == VM_MAP_FAILED)
        return false;

    proc = base = (uintptr)adr;

    // Zero page in the middle (read-only page of zeros)
    zero_page = proc + (size / 2);
    memset((void *)zero_page, 0, page_size);
    mprotect((void *)zero_page, page_size, PROT_READ);

    data = base + size;

    SheepMem_base = base;
    SheepMem_proc = proc;
    SheepMem_data = data;

    return true;
}

extern "C" bool kpx_sheep_mem_init(void) { return SheepMem::Init(); }

void SheepMem::Exit(void)
{
    if (data) {
        munmap((void *)base, size);
        data = 0;
    }
}

// Microseconds — 64-bit microsecond counter: host time since the first
// call, the clock the tick thread and the Time Manager run on. (It was
// instructions * 4 ns, a nominal 250 MIPS: under the JITs it ran ~5x fast,
// and QuickTime 3's movie clock with it — movies ended 5x early, their
// sound cut off.)
void Microseconds(uint32 &hi, uint32 &lo) {
    static const uint64 epoch = GetTicks_usec();
    uint64 usec = GetTicks_usec() - epoch;
    hi = (uint32)(usec >> 32);
    lo = (uint32)(usec & 0xFFFFFFFF);
}

// ExtFS bridge — delegate to the shared implementation in core/extfs.cpp
extern void InstallExtFS(void);
namespace ppc { void InstallExtFS(void) { ::InstallExtFS(); } }
