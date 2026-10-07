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
