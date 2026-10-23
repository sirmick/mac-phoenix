/*
 * syn68k_host.h - the CPU facade's host edge (docs/executor/DIRECTION.md,
 * D3): memory for the guest windows (syn68k_public.h), one .cpp per
 * platform (host/posix/memory.cpp), picked by CMake.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace syn68k_host
{
/* size bytes the guest may address, zeroed, readable and writable, or
 * null. Pages may be committed as they are first touched (posix:
 * MAP_NORESERVE), so a window can be reserved at its full 1GB. */
void *reserve(size_t size);

/* How much of window 1, the syn68k_alloc_low pool (stacks of processes
 * and threads, the framebuffer), to reserve: all of it where pages are
 * committed lazily; less where reserving commits (wasm). */
size_t pool_size();

/* The binary's own data the guest addresses (callback cells, trap
 * tables, GUEST<> globals, string literals): window 3. */
void data_range(uintptr_t *lo, uintptr_t *hi);
}
