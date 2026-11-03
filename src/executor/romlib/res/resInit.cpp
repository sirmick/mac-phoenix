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
#include <rsys/extensions.h>
#include <FileMgr.h>
#include <AliasMgr.h>

#include <algorithm>
#include <cstring>

using namespace Executor;

/* true if there is a version skew between the system file version and
   the required system version.  set by `InitResources ()', and used
   by `InitWindows ()' */
bool Executor::system_file_version_skew_p = false;

/* MacPhoenix: open a folder's files of the given types read-only and chain
   their maps directly below the System file's, in catalog order (so the
   first file ends up last, as on a real boot). */
static void open_below_system(OSType folder, std::initializer_list<OSType> types)
{
    GUEST<INTEGER> vref;
    GUEST<LONGINT> dirid;
    if(FindFolder(-32768 /* kOnSystemDisk */, folder, false, &vref, &dirid) != noErr)
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
        if(std::find(types.begin(), types.end(), type) == types.end())
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

/* MacPhoenix: 8.5 and 8.6 keep a second resource file in the System
   file's data fork, after its code fragments ('zsyr' 0 in the resource
   fork: its offset and length): what 9.0 moved to "System Resources",
   Navigation Services' strings, menus and dialogs among them (BBEdit Lite
   6.1's Save). Its map goes directly below the System file's, read-only. */
static void open_system_data_fork_resources()
{
    INTEGER saved = LM(CurMap);
    LM(CurMap) = LM(SysMap);
    Handle h = Get1Resource("zsyr"_4, 0);
    LM(CurMap) = saved;
    if(!h || GetHandleSize(h) < 8)
        return;
    uint32_t base = *(GUEST<uint32_t> *)*h;
    uint32_t length = *(GUEST<uint32_t> *)(*h + 4);
    ReleaseResource(h);

    HParamBlockRec pb = {};
    pb.ioParam.ioNamePtr = LM(SysResName);
    pb.ioParam.ioVRefNum = LM(BootDrive);
    pb.ioParam.ioPermssn = fsRdPerm;
    if(PBHOpen(&pb, false) != noErr)
        return;
    INTEGER f = pb.ioParam.ioRefNum;
    resmaphand map = ROMlib_read_res_map(f, base, length);
    if(!map)
    {
        FSClose(f);
        ROMlib_setreserr(noErr);
        return;
    }
    (*map)->resfn = f;
    (*map)->resfatr = (*map)->resfatr | mapReadOnly;
    INTEGER i, j;
    typref *tr;
    resref *rr;
    WALKTANDR(map, i, tr, j, rr)
    rr->rhand = 0;
    rr->ratr &= ~resChanged;
    EWALKTANDR(tr, rr)
    resmaphand sys = (resmaphand)LM(SysMapHndl);
    (*map)->nextmap = (*sys)->nextmap;
    (*sys)->nextmap = (Handle)map;
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
    /* as System 7.1 and later: the font files in the Fonts folder */
    open_below_system("font"_4, { "FFIL"_4, "ffil"_4, "tfil"_4 });
    /* 8.5 and later: "System Resources" in the System Folder, which holds
       the icons Icon Services shows (the badges, 8.5's colour icons), the
       Navigation Services and Appearance dialogs, ... */
    open_below_system("macs"_4, { "zsyr"_4 });
    open_system_data_fork_resources();
    ROMlib_install_system_packs(); /* MacPhoenix: on Executor's own System */

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
