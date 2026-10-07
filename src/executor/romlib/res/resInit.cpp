/* Copyright 1986 - 2000 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ResourceMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>

#include <ResourceMgr.h>
#include <QuickDraw.h>
#include <MemoryMgr.h>

#include <res/resource.h>
#include <file/file.h>
#include <mman/mman.h>
#include <commandline/flags.h>
#include <rsys/version.h>
#include <rsys/appearance.h>

using namespace Executor;

/* true if there is a version skew between the system file version and
   the required system version.  set by `InitResources ()', and used
   by `InitWindows ()' */
bool Executor::system_file_version_skew_p = false;

INTEGER Executor::C_InitResources()
{
    TheZoneGuard guard(LM(SysZone));

    LM(TopMapHndl) = nullptr;

    ROMlib_setreserr(noErr);
    str255assign(LM(SysResName), SYSMACNAME);
    LM(SysMap) = OpenRFPerm((StringPtr)SYSMACNAME, LM(BootDrive),
                           fsCurPerm);

    if(LM(SysMap) == -1)
    {
        fprintf(stderr, "Could not open System file: OpenRFPerm (\"%.*s\", 0x%x, fsCurPerm) failed\n",
                SYSMACNAME[0], &SYSMACNAME[1], (uint16_t)LM(BootDrive));

        exit(1);
    }

    LM(SysMapHndl) = LM(TopMapHndl);
    ROMlib_resTypesChanged();
    SetResLoad(true);

    /*
    TODO: decide whether to re-instate a system file version check
        (or whether to handle the system file differently)
    Handle versh = GetResource("vers"_4, 1);
    int32_t versnum = extract_vers_num(versh);
    if(versnum < MINIMUM_SYSTEM_FILE_NEEDED)
        system_file_version_skew_p = true;
    */

    ROMlib_set_appearance();

    return LM(SysMap);
}

void Executor::C_RsrcZoneInit() /* no op */
{
}
