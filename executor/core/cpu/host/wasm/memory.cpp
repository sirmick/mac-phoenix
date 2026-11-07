/* syn68k_host.h in WebAssembly: linear memory, committed as it is
 * allocated, so windows are sized to what is used. */
#include "../../syn68k_host.h"

#include <cstdlib>
#include <cstring>

/* wasm-ld's: the end of the static data and the stack (Emscripten puts
 * the stack between them and the heap). */
extern "C" char __heap_base;

void *syn68k_host::reserve(size_t size)
{
    /* 64KB-aligned, as the windows' pages want. */
    size = (size + 65535) & ~size_t(65535);
    void *p = std::aligned_alloc(65536, size);
    if(p)
        memset(p, 0, size);
    return p;
}

size_t syn68k_host::pool_size()
{
    /* Coroutine stacks (4MB each: the processes' and threads') and the
       framebuffer. */
    return size_t(64) << 20;
}

/* Window 3 is all of linear memory below the heap: static data and the
 * C stack, so host locals the tests hand the guest are addressable too. */
void syn68k_host::data_range(uintptr_t *lo, uintptr_t *hi)
{
    *lo = 0;
    *hi = (uintptr_t)&__heap_base;
}
