/* MacPhoenix: the Sound Manager's dispatch groups, as the ROM's
 * SoundDispatch ($A800) has them: the trap's handler takes D0's low word
 * as an offset into a table of group handlers (group 8 is the Sound
 * Manager, $C the Speech Manager, $10 MACE, $14 the Sound Input Manager,
 * $18 the sound preferences), swaps D0 and jumps there; group 0's selector
 * 4 installs a handler. Apple's Speech Manager INIT installs its own 68k
 * handler as group $C. Executor's dispatcher is C++, so the trap is a
 * stub doing the ROM's table jump; the groups Executor implements point
 * at a re-entry that swaps D0 back and jumps to the C++ dispatcher. */
#include <base/common.h>
#include <base/trapglue.h>
#include <rsys/snddispatch.h>
#include <mman/mman.h>
#include <MemoryMgr.h>
#include <SoundMgr.h>

#include <cstdio>
#include <cstring>

using namespace Executor;

namespace
{
enum
{
    ngroups = 16, /* $00..$3C */
    table_size = ngroups * 4,
    entry_off = table_size,
    reentry_off = entry_off + 14,
    block_size = reentry_off + 8,
};
Ptr block;
syn68k_addr_t original; /* Executor's C++ dispatcher */

void put32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

uint32_t base()
{
    return US_TO_SYN68K(block);
}
}

void Executor::ROMlib_install_sound_dispatch()
{
    if(!tooltraptable)
        return;
    if(!block)
    {
        original = tooltraptable[0xA800 & 0x3FF];
        TheZoneGuard guard(LM(SysZone));
        block = NewPtrSys(block_size);
        if(!block)
            return;
        uint8_t *p = (uint8_t *)block;
        for(int g = 0; g < ngroups; g++)
            put32(p + g * 4, base() + reentry_off);
        /* entry: movea.l #table,a0; movea.l (a0,d0.w),a0; swap d0; jmp (a0) */
        uint8_t *e = p + entry_off;
        e[0] = 0x20, e[1] = 0x7C;
        put32(e + 2, base());
        e[6] = 0x20, e[7] = 0x70, e[8] = 0x00, e[9] = 0x00;
        e[10] = 0x48, e[11] = 0x40;
        e[12] = 0x4E, e[13] = 0xD0;
        /* re-entry: swap d0; jmp original */
        uint8_t *r = p + reentry_off;
        r[0] = 0x48, r[1] = 0x40, r[2] = 0x4E, r[3] = 0xF9;
        put32(r + 4, original);
    }
    tooltraptable[0xA800 & 0x3FF] = base() + entry_off;
}

OSErr Executor::C_SndInstallDispatchGroup(INTEGER group, ProcPtr handler)
{
    if(!block || group < 0 || group >= table_size || (group & 3) || !handler)
        return paramErr;
    put32((uint8_t *)block + group, US_TO_SYN68K(handler));
    fprintf(stderr, "[Executor] sound: dispatch group $%X installed (68k handler)\n", group);
    return noErr;
}

OSErr Executor::C_SndRemoveDispatchGroup(INTEGER group)
{
    if(!block || group < 0 || group >= table_size || (group & 3))
        return paramErr;
    put32((uint8_t *)block + group, base() + reentry_off);
    return noErr;
}
