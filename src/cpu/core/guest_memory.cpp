/*
 * guest_memory.cpp - the page table behind cpu::GuestMemory.
 */
#include "cpu_core.h"

#include <cstdio>
#include <cstdlib>

namespace cpu {

GuestMemory::GuestMemory()
{
    for(auto &d : delta_)
        d = UNMAPPED;
}

void GuestMemory::map(uint32_t guest, uint64_t size, uint8_t *host)
{
    if((guest & (PAGE_SIZE - 1)) || (size & (PAGE_SIZE - 1)) || (uint64_t)guest + size > (1ull << 32))
    {
        fprintf(stderr, "GuestMemory: bad window %08x + %llx\n", guest, (unsigned long long)size);
        abort();
    }
    intptr_t delta = (intptr_t)host - (intptr_t)guest;
    for(uint64_t p = guest >> PAGE_SHIFT; p < ((uint64_t)guest + size) >> PAGE_SHIFT; p++)
        delta_[p] = delta;
}

void GuestMemory::unmap(uint32_t guest, uint64_t size)
{
    for(uint64_t p = guest >> PAGE_SHIFT; p < ((uint64_t)guest + size) >> PAGE_SHIFT; p++)
        delta_[p] = UNMAPPED;
}

bool GuestMemory::guest(const void *host, uint32_t *addr) const
{
    /* Windows are few and large: try each distinct delta. */
    intptr_t last = UNMAPPED;
    for(uint32_t p = 0; p < NUM_PAGES; p++)
    {
        intptr_t d = delta_[p];
        if(d == UNMAPPED || d == last)
            continue;
        last = d;
        intptr_t g = (intptr_t)host - d;
        if(g >= 0 && g <= (intptr_t)0xFFFFFFFF && delta_[(uint32_t)g >> PAGE_SHIFT] == d)
        {
            *addr = (uint32_t)g;
            return true;
        }
    }
    return false;
}

bool GuestMemory::uniform(intptr_t *delta) const
{
    intptr_t found = UNMAPPED;
    for(intptr_t d : delta_)
    {
        if(d == UNMAPPED)
            continue;
        if(found != UNMAPPED && d != found)
            return false;
        found = d;
    }
    if(found == UNMAPPED)
        return false;
    *delta = found;
    return true;
}

uint32_t GuestMemory::io_read(uint32_t a, int size) const
{
    return io_ ? io_->read(a, size) : 0;
}

void GuestMemory::io_write(uint32_t a, uint32_t v, int size)
{
    if(io_)
        io_->write(a, v, size);
}

uint32_t GuestMemory::slow_read(uint32_t a, int size) const
{
    /* Wholly unmapped: one I/O access. Otherwise byte by byte. */
    if(!host(a) && !host(a + size - 1))
        return io_read(a, size);
    uint32_t v = 0;
    for(int i = 0; i < size; i++)
        v = v << 8 | read8(a + i);
    return v;
}

void GuestMemory::slow_write(uint32_t a, uint32_t v, int size)
{
    if(!host(a) && !host(a + size - 1))
    {
        io_write(a, v, size);
        return;
    }
    for(int i = size - 1; i >= 0; i--, v >>= 8)
        write8(a + i, (uint8_t)v);
}

}  // namespace cpu
