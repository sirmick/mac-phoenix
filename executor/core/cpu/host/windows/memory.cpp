/* syn68k_host.h on Windows: VirtualAlloc'd windows, committed when
 * reserved (Windows has no overcommit: committed pages count against the
 * commit limit but take no memory until touched), so the pool is sized
 * to what is used rather than to the whole 1GB window. */
#include "../../syn68k_host.h"

#include <windows.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;  /* the executable's own image */

void *syn68k_host::reserve(size_t size)
{
    /* zeroed, 64KB-aligned (VirtualAlloc's granularity) */
    return VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

size_t syn68k_host::pool_size()
{
    /* Coroutine stacks (4MB each: the processes' and threads') and the
       framebuffer. */
    return size_t(256) << 20;
}

/* From the first section after the code (.rdata, .data, .bss, ...) to the
 * end of the image: what _etext.._end is on ELF. */
void syn68k_host::data_range(uintptr_t *lo, uintptr_t *hi)
{
    auto *base = (uint8_t *)&__ImageBase;
    auto *nt = (IMAGE_NT_HEADERS *)(base + __ImageBase.e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    uintptr_t start = (uintptr_t)base + nt->OptionalHeader.SizeOfImage;
    for(unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if(!(sec[i].Characteristics & IMAGE_SCN_CNT_CODE))
        {
            uintptr_t at = (uintptr_t)base + sec[i].VirtualAddress;
            if(at < start)
                start = at;
        }
    *lo = start & ~(uintptr_t)0xFFF;
    *hi = (uintptr_t)base + nt->OptionalHeader.SizeOfImage;
}
