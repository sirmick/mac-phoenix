/* Copyright 1992-1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

//#define DEBUG

#include <base/common.h>

#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <ResourceMgr.h>
#include <SegmentLdr.h>
#include <MemoryMgr.h>
#include <StdFilePkg.h>
#include <EventMgr.h>
#include <VRetraceMgr.h>
#include <OSUtil.h>
#include <FontMgr.h>
#include <ScrapMgr.h>
#include <ToolboxUtil.h>
#include <FileMgr.h>
#include <ControlMgr.h>
#include <DeviceMgr.h>
#include <PrintMgr.h>
#include <CommTool.h>

#include <base/trapglue.h>
#include <quickdraw/cquick.h>
#include <file/file.h>
#include <prefs/prefs.h>
#include <rsys/segment.h>
#include <rsys/executor.h>
#include <hfs/hfs.h>
#include <base/trapname.h>

#include <prefs/options.h>
#include <util/string.h>

#include <algorithm>

using namespace Executor;


static void SFSaveDisk_Update(INTEGER vrefnum, Str255 filename)
{
    ParamBlockRec pbr;
    Str255 save_name;

    str255assign(save_name, filename);
    pbr.volumeParam.ioNamePtr = (StringPtr)save_name;
    pbr.volumeParam.ioVolIndex = -1;
    pbr.volumeParam.ioVRefNum = vrefnum;
    PBGetVInfo(&pbr, false);
    LM(SFSaveDisk) = -pbr.volumeParam.ioVRefNum;
}

void Executor::executor_main()
{
    QDGlobals quickbytes;
    LONGINT tmpA5;
    GUEST<INTEGER> mess;
    INTEGER count;
    INTEGER exevrefnum, toskip;
    AppFile thefile;
    Byte *p;
    WDPBRec wdpb;
    CInfoPBRec hpb;
    StringPtr fName;

    EM_A5 = US_TO_SYN68K(&tmpA5);
    LM(CurrentA5) = guest_cast<Ptr>(EM_A5);
    InitGraf(&quickbytes.thePort);
    InitFonts();
    InitCRM();
    FlushEvents(everyEvent, 0);
    InitWindows();
    TEInit();
    InitDialogs((ProcPtr)0);
    InitCursor();

    /* ROMlib_WriteWhen(WriteInOSEvent); */

    LM(FinderName)[0] = std::min(strlen(BROWSER_NAME), sizeof(LM(FinderName)) - 1);
    memcpy(LM(FinderName) + 1, BROWSER_NAME, LM(FinderName)[0]);

    CountAppFiles(&mess, out(count));
    if(count > 0)
    {
        GetAppFiles(1, &thefile);
    
        if(thefile.fType == "APPL"_4 || thefile.fType == "MPST"_4)
        {
            ClrAppFiles(1);
            Munger(LM(AppParmHandle), 2L * sizeof(INTEGER), (Ptr)0,
                (LONGINT)sizeof(AppFile), (Ptr) "", 0L);

            fName = thefile.fName;
        }
        else
        {
            ExitToShell();
            return;
        }
    }
    else
    {
        ExitToShell();
        return;
    }

    hpb.hFileInfo.ioNamePtr = &thefile.fName[0];
    hpb.hFileInfo.ioVRefNum = thefile.vRefNum;
    hpb.hFileInfo.ioFDirIndex = 0;
    hpb.hFileInfo.ioDirID = 0;
    PBGetCatInfo(&hpb, false);

    for(p = fName + fName[0] + 1;
        p > fName && *--p != ':';)
        ;
    toskip = p - fName;
    LM(CurApName)[0] = std::min(fName[0] - toskip, 31);
    BlockMoveData((Ptr)fName + 1 + toskip, (Ptr)LM(CurApName) + 1,
                  (Size)LM(CurApName)[0]);

    wdpb.ioVRefNum = hpb.hFileInfo.ioVRefNum;
    wdpb.ioWDDirID = hpb.hFileInfo.ioFlParID;
    SFSaveDisk_Update(hpb.hFileInfo.ioVRefNum, fName);
    LM(CurDirStore) = hpb.hFileInfo.ioFlParID;
    wdpb.ioWDProcID = "Xctr"_4;
    wdpb.ioNamePtr = 0;
    PBOpenWD(&wdpb, false);
    exevrefnum = wdpb.ioVRefNum;
    Launch(LM(CurApName), exevrefnum);
}
