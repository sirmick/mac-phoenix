/* syn68k_host.h on POSIX: lazily committed anonymous mappings. */
#include "../../syn68k_host.h"

#include <sys/mman.h>

extern "C" char _etext, _end;  /* the binary's data+bss (and rodata) */

void *syn68k_host::reserve(size_t size)
{
    void *p = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
}

size_t syn68k_host::pool_size()
{
    return size_t(1) << 30;
}

void syn68k_host::data_range(uintptr_t *lo, uintptr_t *hi)
{
    *lo = (uintptr_t)&_etext & ~(uintptr_t)0xFFF;
    *hi = (uintptr_t)&_end;
}
