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
#include <FileMgr.h>
#include <AliasMgr.h>

#include <cstring>

using namespace Executor;

/* true if there is a version skew between the system file version and
   the required system version.  set by `InitResources ()', and used
   by `InitWindows ()' */
bool Executor::system_file_version_skew_p = false;

/* MacPhoenix: as System 7.1 and later, open the font files in the Fonts
   folder read-only and chain their maps directly below the System file's,
   in catalog order (so the first file ends up last, as on a real boot). */
static void open_font_files()
{
    GUEST<INTEGER> vref;
    GUEST<LONGINT> dirid;
    if(FindFolder(-32768 /* kOnSystemDisk */, "font"_4, false, &vref, &dirid) != noErr)
        return;
    INTEGER saved = LM(CurMap);
    for(INTEGER i = 1;; i++)
    {
        Str255 name;
        CInfoPBRec pb;
        memset(&pb, 0, sizeof pb);
        pb.hFileInfo.ioNamePtr = name;
        pb.hFileInfo.ioVRefNum = vref;
        pb.hFileInfo.ioFDirIndex = i;
        pb.hFileInfo.ioDirID = dirid;
        if(PBGetCatInfo(&pb, false) != noErr)
            break;
        if(pb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR)
            continue;
        OSType type = pb.hFileInfo.ioFlFndrInfo.fdType;
        if(type != "FFIL"_4 && type != "ffil"_4 && type != "tfil"_4)
            continue;
        if(HOpenResFile(vref, dirid, name, fsRdPerm) == -1)
            continue;
        /* the new file is on top: move it below the System file */
        resmaphand top = (resmaphand)LM(TopMapHndl);
        resmaphand sys = (resmaphand)LM(SysMapHndl);
        if(top != sys)
        {
            LM(TopMapHndl) = (*top)->nextmap;
            (*top)->nextmap = (*sys)->nextmap;
            (*sys)->nextmap = (Handle)top;
        }
    }
    LM(CurMap) = saved;
    ROMlib_resTypesChanged();
}

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
    open_font_files();

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
