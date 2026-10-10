/*
 * modules/lib/osdlib.h - MacPhoenix stand-in for the part of MAME's OSD
 * library the recompiler uses: executable memory for the code cache.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace osd {

/* A block of virtual memory whose protection can change page by page:
 * the recompiler's code cache. mmap-backed (shim/osdlib.cpp). */
class virtual_memory_allocation
{
public:
    enum : unsigned
    {
        NONE = 0x00,
        READ = 0x01,
        WRITE = 0x02,
        EXECUTE = 0x04,
        READ_WRITE = READ | WRITE,
        READ_EXECUTE = READ | EXECUTE,
        READ_WRITE_EXECUTE = READ | WRITE | EXECUTE
    };

    virtual_memory_allocation(virtual_memory_allocation const &) = delete;
    virtual_memory_allocation &operator=(virtual_memory_allocation const &) = delete;

    virtual_memory_allocation() noexcept {}
    virtual_memory_allocation(std::initializer_list<std::size_t> blocks, unsigned intent) noexcept
    {
        m_memory = do_alloc(blocks, intent, m_size, m_page_size);
    }
    virtual_memory_allocation(virtual_memory_allocation &&that) noexcept
        : m_memory(that.m_memory), m_size(that.m_size), m_page_size(that.m_page_size)
    {
        that.m_memory = nullptr;
        that.m_size = that.m_page_size = 0U;
    }
    ~virtual_memory_allocation()
    {
        if(m_memory)
            do_free(m_memory, m_size);
    }

    explicit operator bool() const noexcept { return bool(m_memory); }
    void *get() noexcept { return m_memory; }
    std::size_t size() const noexcept { return m_size; }
    std::size_t page_size() const noexcept { return m_page_size; }

    bool set_access(std::size_t start, std::size_t size, unsigned access) noexcept
    {
        if((start % m_page_size) || (size % m_page_size) || (start > m_size) || ((m_size - start) < size))
            return false;
        return do_set_access(reinterpret_cast<std::uint8_t *>(m_memory) + start, size, access);
    }

    virtual_memory_allocation &operator=(virtual_memory_allocation &&that) noexcept
    {
        if(m_memory)
            do_free(m_memory, m_size);
        m_memory = that.m_memory;
        m_size = that.m_size;
        m_page_size = that.m_page_size;
        that.m_memory = nullptr;
        that.m_size = that.m_page_size = 0U;
        return *this;
    }

private:
    static void *do_alloc(std::initializer_list<std::size_t> blocks, unsigned intent, std::size_t &size,
                          std::size_t &page_size) noexcept;
    static void do_free(void *start, std::size_t size) noexcept;
    static bool do_set_access(void *start, std::size_t size, unsigned access) noexcept;

    void *m_memory = nullptr;
    std::size_t m_size = 0U, m_page_size = 0U;
};

bool invalidate_instruction_cache(void const *start, std::size_t size) noexcept;

}  // namespace osd
