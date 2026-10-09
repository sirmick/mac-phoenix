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
    unmap(guest, size);
    intptr_t delta = (intptr_t)host - (intptr_t)guest;
    for(uint64_t p = guest >> PAGE_SHIFT; p < ((uint64_t)guest + size) >> PAGE_SHIFT; p++)
        delta_[p] = delta;
    windows_.push_back({ guest, size, host });
}

void GuestMemory::unmap(uint32_t guest, uint64_t size)
{
    for(uint64_t p = guest >> PAGE_SHIFT; p < ((uint64_t)guest + size) >> PAGE_SHIFT; p++)
        delta_[p] = UNMAPPED;
    /* Drop or trim the windows it overlaps (whole windows in practice). */
    uint64_t end = (uint64_t)guest + size;
    std::vector<Window> kept;
    for(const Window &w : windows_)
    {
        uint64_t wend = (uint64_t)w.guest + w.size;
        if(wend <= guest || w.guest >= end)
            kept.push_back(w);
        else
        {
            if(w.guest < guest)
                kept.push_back({ w.guest, guest - w.guest, w.host });
            if(wend > end)
                kept.push_back({ (uint32_t)end, wend - end, w.host + (end - w.guest) });
        }
    }
    windows_.swap(kept);
}

bool GuestMemory::guest(const void *host, uint32_t *addr) const
{
    const uint8_t *p = (const uint8_t *)host;
    for(const Window &w : windows_)
        if(p >= w.host && (uint64_t)(p - w.host) < w.size)
        {
            *addr = w.guest + (uint32_t)(p - w.host);
            return true;
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
