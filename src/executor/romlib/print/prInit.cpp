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
#include <AliasMgr.h>
#include <LowMem.h>
#include <print/print.h>

using namespace Executor;

INTEGER Executor::ROMlib_printresfile = -1;

/* The driver the Chooser chose (STR -8192 names it), opened where System 7
   keeps drivers, the Extensions folder, or in the System Folder. */
static INTEGER open_printer_driver(ConstStringPtr name)
{
    static const OSType folders[] = { kExtensionFolderType, kSystemFolderType };
    for(OSType folder : folders)
    {
        GUEST<INTEGER> vref;
        GUEST<LONGINT> dirid;
        if(FindFolder(0, folder, false, &vref, &dirid) != noErr)
            continue;
        INTEGER rn = HOpenResFile(vref, dirid, name, fsCurPerm);
        if(rn != -1)
            return rn;
    }
    return -1;
}

/* As a 7.6.1 Quadra does it (a probe): the driver open and the current
   resource file, its reference number in PrRefNum; fnfErr when no printer
   was ever chosen (no STR -8192) or its driver isn't there. */
void Executor::C_PrOpen()
{
    StringHandle h;

    /* a NULL handle reads guest address 0, mapped on the UAE core but
       not on mame-68k */
    if(ROMlib_printresfile == -1 && (h = GetString(-8192)) && *h)
    {
        Str255 name;
        str255assign(name, *h);
        ROMlib_printresfile = open_printer_driver(name);
        if(ROMlib_printresfile != -1)
            LM(PrRefNum) = ROMlib_printresfile;
    }
    else if(ROMlib_printresfile != -1)
        UseResFile(ROMlib_printresfile);
    LM(PrintErr) = ROMlib_printresfile == -1 ? (OSErr)fnfErr : (OSErr)noErr;
}

/* The driver closed, the resource file before it current again; PrRefNum
   keeps the number (a 7.6.1 Quadra). */
void Executor::C_PrClose()
{
    if(ROMlib_printresfile != -1)
    {
        CloseResFile(ROMlib_printresfile);
        ROMlib_printresfile = -1;
    }
    LM(PrintErr) = noErr;
}
