/* Copyright 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in PrintMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <PrintMgr.h>
#include <ResourceMgr.h>
#include <FileMgr.h>
#include <ToolboxUtil.h>
#include <MemoryMgr.h>
#include <print/print.h>

using namespace Executor;

INTEGER Executor::ROMlib_printresfile = -1;

void Executor::C_PrOpen()
{
    StringHandle h;

    /* STR -8192 names the printer driver chosen in the Chooser; a System
     * that never had one chosen has none (a NULL handle reads guest
     * address 0, mapped on the UAE core but not on mame-68k). */
    if(ROMlib_printresfile == -1 && (h = GetString(-8192)) && *h)
    {
        HLock((Handle)h);
        ROMlib_printresfile = OpenRFPerm(*h, LM(BootDrive), fsCurPerm);
        HUnlock((Handle)h);
    }
    LM(PrintErr) = ROMlib_printresfile == -1 ? (OSErr)fnfErr : (OSErr)noErr;
}

void Executor::C_PrClose()
{
    if(ROMlib_printresfile != -1)
    {
        CloseResFile(ROMlib_printresfile);
        ROMlib_printresfile = -1;
    }
    LM(PrintErr) = noErr;
}
