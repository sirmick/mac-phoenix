/*
 * osdlib.cpp - executable memory for the recompiler's code cache, on mmap.
 * Apple Silicon needs MAP_JIT and the write-protect toggle around emission;
 * that comes with the arm64 back-end (docs/cpu/PLAN.md, C2d).
 */
#include "modules/lib/osdlib.h"

#include <sys/mman.h>
#include <unistd.h>

namespace osd {

namespace {
int prot_for(unsigned access)
{
    int prot = 0;
    if(access & virtual_memory_allocation::READ)
        prot |= PROT_READ;
    if(access & virtual_memory_allocation::WRITE)
        prot |= PROT_WRITE;
    if(access & virtual_memory_allocation::EXECUTE)
        prot |= PROT_EXEC;
    return prot;
}
}  // namespace

void *virtual_memory_allocation::do_alloc(std::initializer_list<std::size_t> blocks, unsigned intent,
                                          std::size_t &size, std::size_t &page_size) noexcept
{
    long const page = sysconf(_SC_PAGESIZE);
    if(page <= 0)
        return nullptr;
    std::size_t total = 0;
    for(std::size_t b : blocks)
        total += (b + page - 1) / page * page;
    if(!total)
        return nullptr;
    void *p = mmap(nullptr, total, prot_for(intent), MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(p == MAP_FAILED)
        return nullptr;
    size = total;
    page_size = page;
    return p;
}

void virtual_memory_allocation::do_free(void *start, std::size_t size) noexcept
{
    munmap(start, size);
}

bool virtual_memory_allocation::do_set_access(void *start, std::size_t size, unsigned access) noexcept
{
    return mprotect(start, size, prot_for(access)) == 0;
}

bool invalidate_instruction_cache(void const *start, std::size_t size) noexcept
{
    char *s = const_cast<char *>(static_cast<char const *>(start));
    __builtin___clear_cache(s, s + size);
    return true;
}

}  // namespace osd
