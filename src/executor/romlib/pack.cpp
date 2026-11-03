/* Copyright 1986, 1988, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in Package.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <Package.h>
#include <error/error.h>
#include <base/cpu.h>
#include <MemoryMgr.h>
#include <ResourceMgr.h>
#include <OSUtil.h>
#include <rsys/extensions.h>
#include <mman/mman.h>
#include <string.h>

using namespace Executor;

void Executor::C_InitPack(INTEGER packid)
{
    /* NOP */
}

void Executor::C_InitAllPacks()
{
    /* NOP */
}

RAW_68K_IMPLEMENTATION(Pack1)
{
    // For ResEdit, which uses PACK 1 to implement
    // its bitmap image editor.
    Handle pack1 = GetResource("PACK"_4, 1);
    gui_assert(pack1);
    HLock(pack1);
    
    // Install the PACK as the new implementation for this trap
    auto addr = ptr_to_longint(*pack1);
    ROMlib_destroy_blocks(addr, GetHandleSize(pack1), false);
    NSetTrapAddress((ProcPtr)*pack1, 0xA9E8, kToolboxTrapType);

    return addr;    // forward to the PACK
}

/* MacPhoenix: the packages a System file holds, on Executor's own System.
   Programs look for 'PACK' resources to see whether a package is there
   (ResEdit's pixel editor failed with memFullErr without them; upstream
   answered any missing PACK with an unrelated ALRT). A 7.6.1 Quadra's
   System file has PACK 0, 2, 3, 6, 8, 9 and 11-15 (4, 5 and 7 are in ROM,
   GetResource doesn't find them; 1 and 10 aren't there). Each stand-in is
   Apple's header (BRA.S; 'PACK'; id; version), then the package's trap
   and RTS, as ROMlib_install_app_packs's. Apple's System files ('boot'
   resources) have their own. */
void Executor::ROMlib_install_system_packs()
{
    static const struct
    {
        int16_t id;
        uint16_t trap;
    } packs[] = {
        { 0, 0xA9E7 }, { 2, 0xA9E9 }, { 3, 0xA9EA }, { 6, 0xA9ED },
        { 8, 0xA816 }, { 9, 0xA82B }, { 11, 0xA82D }, { 12, 0xA82E },
        { 13, 0xA82F }, { 14, 0xA830 }, { 15, 0xA831 },
    };
    if(!LM(SysMapHndl))
        return;
    INTEGER saved = CurResFile();
    UseResFile(LM(SysMap));
    if(Count1Resources("boot"_4) == 0)
    {
        TheZoneGuard guard(LM(SysZone));
        for(auto p : packs)
        {
            if(Get1Resource("PACK"_4, p.id))
                continue;
            Handle h = NewHandle(16);
            if(!h)
                break;
            uint8_t *b = (uint8_t *)*h;
            const uint8_t header[12] = { 0x60, 0x0A, 0, 0, 'P', 'A', 'C', 'K',
                                         0, (uint8_t)p.id, 0, 1 };
            memcpy(b, header, sizeof header);
            b[12] = p.trap >> 8;
            b[13] = p.trap & 0xFF;
            b[14] = 0x4E; /* RTS */
            b[15] = 0x75;
            AddResource(h, "PACK"_4, p.id, (StringPtr) "");
        }
    }
    UseResFile(saved);
}
