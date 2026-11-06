#if !defined(__TRAPGLUE__)
#define __TRAPGLUE__
#include <syn68k_public.h>
#include <stdint.h>

namespace Executor
{
#define NTOOLENTRIES (0x400)
#define NOSENTRIES (0x100)

/* MacPhoenix: the trap tables live in guest memory where a Mac keeps them,
   the OS table at $400 and the Toolbox table at $E00 (traps.cpp), so code
   that reads or patches them directly sees the real thing. Entries are
   guest addresses, big-endian. */
#define OSTRAPTABLE_ADDR 0x400
#define TOOLTRAPTABLE_ADDR 0xE00

/* One entry, as the guest sees it (GUEST<> is not declared yet when this
   header is reached through mactype.h). */
struct TrapTableEntry
{
    uint32_t raw;
    static uint32_t swap(uint32_t v)
    {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return __builtin_bswap32(v);
#else
        return v;
#endif
    }
    operator syn68k_addr_t() const { return swap(raw); }
    TrapTableEntry &operator=(syn68k_addr_t v)
    {
        raw = swap(v);
        return *this;
    }
};

extern TrapTableEntry *tooltraptable;
extern TrapTableEntry *ostraptable;

extern unsigned short mostrecenttrap;
}

#endif
