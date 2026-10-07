/* Copyright 1989, 1990, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in SegmentLdr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>

#include <FileMgr.h>
#include <SegmentLdr.h>
#include <MemoryMgr.h>
#include <ResourceMgr.h>
#include <StdFilePkg.h>
#include <SysErr.h>
#include <FontMgr.h>
#include <OSEvent.h>
#include <WindowMgr.h>

#include <hfs/hfs.h>
#include <base/cpu.h>
#include <wind/wind.h>
#include <rsys/segment.h>
#include <vdriver/vdriver.h>
#include <rsys/executor.h>
#include <commandline/flags.h>
#include <prefs/prefs.h>
#include <osevent/osevent.h>
#include <quickdraw/cquick.h>
#include <rsys/desk.h>
#include <rsys/launch.h>
#include <rsys/paths.h>
#include <util/macstrings.h>
#include <rsys/keyboard.h>

#include <ctype.h>
#include <algorithm>


namespace Executor
{
bool ROMlib_exit = false;

typedef finderinfo *finderinfoptr;

typedef GUEST<finderinfoptr> *finderinfohand;
}

using namespace Executor;

static Boolean argv_to_appfile(const char *uname, AppFile *ap)
{
    auto spec = cmdlinePathToFSSpec(uname);

    if(!spec)
        return false;

    CInfoPBRec cinfo;
    cinfo.hFileInfo.ioVRefNum = spec->vRefNum;
    cinfo.hFileInfo.ioDirID = spec->parID;
    cinfo.hFileInfo.ioFDirIndex = 0;
    cinfo.hFileInfo.ioNamePtr = spec->name;
    
    if(PBGetCatInfo(&cinfo, false) != noErr)
    {
        warning_unexpected("%s: unable to get info on `%s'\n", ROMlib_appname.c_str(),
                           uname);
        return false;
    }

    ap->fType = cinfo.hFileInfo.ioFlFndrInfo.fdType;
    ap->versNum = 0;
    str255assign(ap->fName, spec->name);

    WDPBRec wpb;
    wpb.ioNamePtr = spec->name;
    wpb.ioVRefNum = spec->vRefNum;
    wpb.ioWDProcID = "unix"_4;
    wpb.ioWDDirID = spec->parID;
    if(PBOpenWD(&wpb, false) == noErr)
        ap->vRefNum = wpb.ioVRefNum;
    else
        ap->vRefNum = spec->vRefNum;

    return true;
}

void Executor::InitAppFiles(const std::vector<std::string>& files)
{
    LM(CurApRefNum) = -1;
    assignPString(LM(CurApName), toMacRomanFilename(ROMlib_appname), sizeof(LM(CurApName)) - 1);

    GUEST<THz> saveZone = LM(TheZone);
    LM(TheZone) = LM(SysZone);
    finderinfohand fh = (finderinfohand)
        NewHandle((Size)sizeof(finderinfo) - sizeof(AppFile));
    LM(TheZone) = saveZone;

    LM(AppParmHandle) = (Handle)fh;
    (*fh)->count = 0;
    (*fh)->message = ROMlib_print ? appPrint : appOpen;

    for(const std::string& file : files)
    {
        AppFile appFile;
        if(argv_to_appfile(file.c_str(), &appFile))
        {
            ROMlib_exit = true;
            INTEGER newcount = (*fh)->count + 1;
            (*fh)->count = newcount;
            SetHandleSize((Handle)fh,
                            (char *)&(*fh)->files[newcount] - (char *)*fh);
            (*fh)->files[(*fh)->count - 1] = appFile;
        }
    }
}

void Executor::CountAppFiles(GUEST<INTEGER> *messagep,
                             GUEST<INTEGER> *countp) /* IMII-57 */
{
    if(LM(AppParmHandle))
    {
        if(messagep)
            *messagep = (*(finderinfohand)LM(AppParmHandle))->message;
        if(countp)
            *countp = (*(finderinfohand)LM(AppParmHandle))->count;
    }
    else
        *countp = 0;
}

void Executor::GetAppFiles(INTEGER index, AppFile *filep) /* IMII-58 */
{
    *filep = (*(finderinfohand)LM(AppParmHandle))->files[index - 1];
}

void Executor::ClrAppFiles(INTEGER index) /* IMII-58 */
{
    if((*(finderinfohand)LM(AppParmHandle))->files[index - 1].fType)
    {
        (*(finderinfohand)LM(AppParmHandle))->files[index - 1].fType = 0;
        (*(finderinfohand)LM(AppParmHandle))->count = (*(finderinfohand)LM(AppParmHandle))->count - 1;
    }
}

void Executor::C_GetAppParms(StringPtr namep, GUEST<INTEGER> *rnp,
                             GUEST<Handle> *aphandp) /* IMII-58 */
{
    str255assign(namep, LM(CurApName));
    *rnp = LM(CurApRefNum);
    *aphandp = LM(AppParmHandle);
}

static Boolean valid_browser(void)
{
    OSErr err;
    FInfo finfo;

    err = GetFInfo(LM(FinderName), LM(BootDrive), &finfo);
    return !ROMlib_nobrowser && err == noErr && finfo.fdType == "APPL"_4;
}

static void launch_browser(void)
{
    ResetToInitialDepth();
    Launch(LM(FinderName), LM(BootDrive));
}

void Executor::C_ExitToShell()
{
    static char beenhere = false;
    ALLOCABEGIN

    if(ROMlib_launch_failure)
    {
        fprintf(stderr, "launch failure: %d\n", ROMlib_launch_failure);
    }

#if 1

    Point pt;
    static GUEST<SFTypeList> applonly = { "APPL"_4 };
    SFReply reply;
    struct
    {
        QDGlobals qd;
        GUEST<LONGINT> tmpA5;
    } a5space;
    WindowPeek t_w;
    int i;

    if(ROMlib_GetKey(MKV_LEFTOPTION))
        ROMlib_exit = true;

    /* NOTE: closing drivers is a bad thing in the long run, but for now
     we're doing it.  We actually do it here *and* in reinitialize_things().
     We have to do it here since we want the window taken out of the
     windowlist properly, before we hack it out ourself, but we have to
     do it in reinitializethings because launch calls that, but doesn't
     call exittoshell */

    for(i = DESK_ACC_MIN; i <= DESK_ACC_MAX; ++i)
        CloseDriver(-i - 1);

    empty_timer_queues();

    if(LM(WWExist) == EXIST_YES)
    {
        /* remove global datastructures associated with each window
           remaining in the application's window list */
        for(t_w = LM(WindowList); t_w; t_w = WINDOW_NEXT_WINDOW(t_w))
            pm_window_closed((WindowPtr)t_w);
    }
    LM(WindowList) = 0;

    if(!ROMlib_exit
       && (!beenhere
           || strncmp((char *)LM(CurApName) + 1,
                      BROWSER_NAME, LM(CurApName)[0])
               != 0))
    {
        beenhere = true;

        /* if we call `InitWindows ()', we don't want it shouting
	   about 32bit uncleanliness, &c */
        size_info.application_p = false;

        if(LM(QDExist) == EXIST_NO)
        {
            EM_A5 = US_TO_SYN68K(&a5space.tmpA5);
            LM(CurrentA5) = guest_cast<Ptr>(EM_A5);
            InitGraf(&a5space.qd.thePort);
        }
        InitFonts();
        FlushEvents(everyEvent, 0);
        if(LM(WWExist) == EXIST_NO)
            InitWindows();
        if(LM(TEScrpHandle) == guest_cast<Handle>(-1) || LM(TEScrpHandle) == nullptr)
            TEInit();
        if(LM(DlgFont) == 0 || LM(DlgFont) == -1)
            InitDialogs((ProcPtr)0);
        InitCursor();

        if(valid_browser())
        {
            /* NOTE: much of the initialization done above isn't really
	             needed here, but I'd prefer to have the same environment
		     when we auto-launch browser as when we choose an app
		     using stdfile */
            launch_browser();
        }
        else
        {
            pt.h = 100;
            pt.v = 100;
            SFGetFile(pt, (StringPtr) "", nullptr, 1,
                      applonly, nullptr, &reply);

            if(!reply.good)
                ROMlib_exit = true;
            else
            {
                LM(CurApName)[0] = std::min<uint8_t>(reply.fName[0], 31);
                BlockMoveData((Ptr)reply.fName + 1, (Ptr)LM(CurApName) + 1,
                              (Size)LM(CurApName)[0]);
                Launch(LM(CurApName), reply.vRefNum);
            }
        }
    }

#endif

    CloseResFile(0);
    ROMlib_OurClose();

    throw ExitToShellException();
}


#define JMPLINSTR 0x4EF9
#define MOVESPINSTR 0x3F3C
#define LOADSEGTRAP 0xA9F0

void Executor::C_LoadSeg(INTEGER segno)
{
    Handle newcode;
    unsigned short offbytes;
    INTEGER taboff, nentries, savenentries;
    GUEST<int16_t> *ptr, *saveptr;

    LM(ResLoad) = -1; /* CricketDraw III's behaviour suggested this */
    newcode = GetResource("CODE"_4, segno);
    HLock(newcode);
    taboff = ((GUEST<INTEGER> *)*newcode)[0];
    if((uint16_t)taboff == 0xA89F) /* magic compressed resource signature */
    {
        /* We are totally dead here.  We almost certainly can't use
	 * `system_error' to inform the user because the window
	 * manager probably is not yet running.
	 */

        ROMlib_launch_failure = (system_version >= 0x700 ? launch_compressed_ge7 : launch_compressed_lt7);
        C_ExitToShell();
    }
    savenentries = nentries = ((GUEST<INTEGER> *)*newcode)[1];

    saveptr = ptr = (GUEST<int16_t> *)((char *)SYN68K_TO_US(EM_A5) + taboff + LM(CurJTOffset));
    while(--nentries >= 0)
    {
        if(ptr[1] != JMPLINSTR)
        {
            offbytes = *ptr;
            *ptr++ = segno;
            *ptr++ = JMPLINSTR;
            *(GUEST<LONGINT> *)ptr = US_TO_SYN68K(*newcode) + offbytes + 4;
            ptr += 2;
        }
        else
            ptr += 4;
    }
    ROMlib_destroy_blocks(US_TO_SYN68K(saveptr), 8L * savenentries,
                          true);
}

#define SEGNOOFP(p) (((GUEST<INTEGER> *)p)[-1])

static void unpatch(Ptr segstart, Ptr p)
{
    GUEST<INTEGER> *ip;
    Ptr firstpc;

    ip = (GUEST<INTEGER> *)p;

    firstpc = *(GUEST<Ptr> *)(p + 2);
    ip[1] = ip[-1]; /* the segment number */
    ip[-1] = firstpc - segstart - 4;
    ip[0] = MOVESPINSTR;
    ip[2] = LOADSEGTRAP;
}

void Executor::C_UnloadSeg(void* addr)
{
    Ptr p, segstart;
    char *top, *bottom;

    Handle h;
    INTEGER segno;

    if(*(GUEST<INTEGER> *)addr == JMPLINSTR)
    {
        segno = SEGNOOFP(addr);
        h = GetResource("CODE"_4, segno);
        if(!*h)
            LoadResource(h);
        segstart = *h;
        for(p = Ptr(addr); SEGNOOFP(p) == segno; p += 8)
            unpatch(segstart, p);

        bottom = (char *)p;

        for(p = Ptr(addr) - 8; SEGNOOFP(p) == segno; p -= 8)
            unpatch(segstart, p);

        top = (char *)p + 6; /* +8 that we didn't zap, -2 that we went
					overboard on unpatch (see above) */
        ROMlib_destroy_blocks(US_TO_SYN68K(top),
                              bottom - top, false);

        HUnlock(h);
        HPurge(h);
    }
}

