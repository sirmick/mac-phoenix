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

// Microseconds — 64-bit microsecond counter, matching legacy SheepShaver.
// Uses virtual PPC instruction clock (ppc_insn_counter * 4ns) to give
// time relative to first call, exactly as legacy's host_uptime() does.
void Microseconds(uint32 &hi, uint32 &lo) {
    extern uint64_t ppc_insn_counter;
    static uint64_t epoch_ns = 0;
    uint64_t virtual_ns = ppc_insn_counter * 4;
    if (epoch_ns == 0) epoch_ns = virtual_ns;
    uint64 usec = (virtual_ns - epoch_ns) / 1000;
    hi = (uint32)(usec >> 32);
    lo = (uint32)(usec & 0xFFFFFFFF);
}

// ExtFS bridge — delegate to the shared implementation in core/extfs.cpp
extern void InstallExtFS(void);
namespace ppc { void InstallExtFS(void) { ::InstallExtFS(); } }
