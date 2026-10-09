/* Copyright 1986-1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <rsys/desk.h>
#include <menu/menu.h>

#include <base/common.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <EventMgr.h>
#include <WindowMgr.h>
#include <DeviceMgr.h>
#include <DeskMgr.h>
#include <MenuMgr.h>
#include <QuickDraw.h>
#include <OSEvent.h>
#include <ToolboxEvent.h>
#include <OSUtil.h>

#include <quickdraw/cquick.h>
#include <wind/wind.h>
#include <rsys/hook.h>
#include <rsys/aboutbox.h>
#include <rsys/prefpanel.h>
#include <base/cpu.h>
#include <util/macstrings.h>

#include <FileMgr.h>
#include <ResourceMgr.h>
#include <MemoryMgr.h>
#include <SegmentLdr.h>
#include <FontMgr.h>
#include <TextEdit.h>
#include <DialogMgr.h>
#include <rsys/device.h>
#include <rsys/process.h>
#include <rsys/launch.h>
#include <SysErr.h>
#include <LowMem.h>

#include <algorithm>

using namespace Executor;

AppleMenuEntry::AppleMenuEntry(const char32_t* n, void (*f)())
    : function(f)
{
    assignPString(name, toMacRoman(n), 31);
}

const std::vector<AppleMenuEntry>& Executor::appleMenuEntries()
{
    static std::vector<AppleMenuEntry> entries {
        { U"About Executor 2000...", &do_about_box },
        { U"Emulation Settings...", &dopreferences }
    };

    return entries;
}


/* MacPhoenix: the desk accessories the current process opened.  The unit
   table is system-wide, but a DA lives in its process's partition and
   layer, so only that process gives it time and menu choices. */
namespace
{
struct DaUnits
{
    INTEGER count;
    INTEGER refnum[16];
    INTEGER opened_count; /* every unit it opened, closed or not */
    INTEGER opened[16];
    bool handler; /* the process is a DA Handler (its menus are below) */
};
DaUnits da_units;

DaUnits &das()
{
    static bool registered;
    if(!registered)
    {
        registered = true;
        ROMlib_process_register_state(&da_units, sizeof da_units);
    }
    return da_units;
}

bool da_is_open(INTEGER rn)
{
    DCtlHandle h = GetDCtlEntry(rn);
    return h && *h && ((*h)->dCtlFlags & DRIVEROPENBIT);
}

void da_add(INTEGER rn)
{
    DaUnits &d = das();
    for(int i = 0; i < d.count; i++)
        if(d.refnum[i] == rn)
            return;
    if(d.count < (int)std::size(d.refnum))
        d.refnum[d.count++] = rn;
    for(int i = 0; i < d.opened_count; i++)
        if(d.opened[i] == rn)
            return;
    if(d.opened_count < (int)std::size(d.opened))
        d.opened[d.opened_count++] = rn;
}

/* Drop the ones that closed. */
void da_prune()
{
    DaUnits &d = das();
    int n = 0;
    for(int i = 0; i < d.count; i++)
        if(da_is_open(d.refnum[i]))
            d.refnum[n++] = d.refnum[i];
    d.count = n;
}

/* The DA Handler's menus: the System's MENU -16473..-16471, as 7.5.5's
   Process Manager uses them; their menu IDs are positive. */
enum
{
    kDAAppleMenu = 16473, /* "About ^0…" */
    kDAFileMenu = 16472,  /* Close, Quit */
    kDAEditMenu = 16471,  /* Undo, -, Cut, Copy, Paste, Clear */
};
bool da_handler_menu_p(INTEGER id)
{
    return id >= kDAEditMenu && id <= kDAAppleMenu;
}
bool da_quit;

void da_close_all()
{
    da_prune();
    DaUnits &d = das();
    while(d.count)
    {
        CloseDeskAcc(d.refnum[0]);
        if(da_is_open(d.refnum[0]))
            break; /* won't close */
        da_prune();
    }
}

void da_handler_menu(LONGINT choice)
{
    INTEGER id = choice >> 16, item = choice & 0xFFFF;
    switch(id)
    {
        case kDAAppleMenu:
            if(item > 2)
            {
                Str255 name;
                GetMenuItemText(GetMenuHandle(id), item, name);
                OpenDeskAcc(name);
            }
            break;
        case kDAFileMenu:
            if(item == 1)
            {
                WindowPeek w = (WindowPeek)FrontWindow();
                if(w && WINDOW_KIND(w) < 0)
                    CloseDeskAcc(WINDOW_KIND(w));
            }
            else if(item == 2)
                da_quit = true;
            break;
        case kDAEditMenu:
            /* Undo is item 1, Cut..Clear items 3..6: undoCmd, cutCmd... */
            SystemEdit(item - 1);
            break;
    }
    HiliteMenu(0);
}
}

INTEGER Executor::C_OpenDeskAcc(ConstStringPtr acc) /* IMI-440 */
{
    // THINK Reference says that OpenDeskAcc's return value
    // is undefined on error. This returns zero - otherwise
    // we get a compiler warning that might hide real bugs.
    INTEGER retval = 0;
    GUEST<INTEGER> retval_s;
    DCtlHandle dctlh;
    WindowPtr wp;

    if(ROMlib_apple_menu_open(acc))
        return 0;

    const auto& entries = appleMenuEntries();

    if(auto it = std::find_if(entries.begin(), entries.end(),
            [acc](const AppleMenuEntry& e) { return EqualString(e.name, acc, true, true); });
        it != entries.end())
    {
        it->function();
    }
    else if(OpenDriver(acc, &retval_s) == noErr)
    {
        retval = retval_s;
        da_add(retval);
        dctlh = GetDCtlEntry(retval);
        if(dctlh)
        {
            wp = (*dctlh)->dCtlWindow;
            if(wp)
            {
                ShowWindow(wp);
                SelectWindow(wp);
            }
        }
    }

    LM(SEvtEnb) = true;
    return retval;
}

void Executor::C_CloseDeskAcc(INTEGER rn)
{
    CloseDriver(rn);
}

void Executor::C_SystemClick(EventRecord *evp, WindowPtr wp)
{
    Point p;
    LONGINT pointaslong, val;
    Rect bounds;
    GUEST<LONGINT> templ;

    if(wp)
    {
        p.h = evp->where.h;
        p.v = evp->where.v;
        if(PtInRgn(p, WINDOW_STRUCT_REGION(wp)))
        {
            pointaslong = ((LONGINT)p.v << 16) | (unsigned short)p.h;
            val = WINDCALL((WindowPtr)wp, wHit, pointaslong);
            switch(val)
            {
                case wInContent:
                    if(WINDOW_HILITED(wp))
                    {
                        templ = guest_cast<LONGINT>(evp);
                        Control(WINDOW_KIND(wp), accEvent, (Ptr)&templ);
                    }
                    else
                        SelectWindow(wp);
                    break;
                case wInDrag:
                    bounds.top = LM(MBarHeight) + 4;
                    bounds.left = GD_BOUNDS(LM(TheGDevice)).left + 4;
                    bounds.bottom = GD_BOUNDS(LM(TheGDevice)).bottom - 4;
                    bounds.right = GD_BOUNDS(LM(TheGDevice)).right - 4;
                    DragWindow(wp, p, &bounds);
                    break;
                case wInGoAway:
                    if(TrackGoAway(wp, p))
                        CloseDeskAcc(WINDOW_KIND(wp));
                    break;
            }
        }
        else
        {
            if(LM(DeskHook))
            {
                ROMlib_hook(desk_deskhooknumber);
                EM_D0 = -1;
                EM_A0 = US_TO_SYN68K(evp);
                execute68K((syn68k_addr_t)(guest_cast<LONGINT>(LM(DeskHook))));
            }
        }
    }
}

Boolean Executor::C_SystemEdit(INTEGER editcmd)
{
    WindowPeek wp;
    Boolean retval;

    wp = (WindowPeek)FrontWindow();
    if(!wp)
        retval = false;
    else if((retval = WINDOW_KIND(wp) < 0))
        Control(WINDOW_KIND(wp), editcmd + accUndo, (Ptr)0);
    return retval;
}

#define rntodctlh(rn) (LM(UTableBase)[-((rn) + 1)])
#define itorn(i) ((-i) - 1)

void Executor::C_SystemTask()
{
    /* MacPhoenix: the current process's desk accessories (see DaUnits). */
    da_prune();
    DaUnits &d = das();
    for(int i = 0; i < d.count; i++)
    {
        DCtlHandle dctlh = GetDCtlEntry(d.refnum[i]);
        if(((*dctlh)->dCtlFlags & NEEDTIMEBIT) && TickCount() >= (*dctlh)->dCtlCurTicks)
        {
            Control(d.refnum[i], accRun, (Ptr)0);
            dctlh = GetDCtlEntry(d.refnum[i]);
            if(dctlh && *dctlh)
                (*dctlh)->dCtlCurTicks = TickCount() + (*dctlh)->dCtlDelay;
        }
    }
}

Boolean Executor::C_SystemEvent(EventRecord *evp)
{
    Boolean retval;
    WindowPeek wp;
    INTEGER rn;
    DCtlHandle dctlh;
    GUEST<LONGINT> templ;

    if(LM(SEvtEnb))
    {
        wp = 0;
        switch(evp->what)
        {
            default:
            case nullEvent:
            case mouseDown:
            case networkEvt:
            case driverEvt:
            case app1Evt:
            case app2Evt:
            case app3Evt:
            case app4Evt:
                break;
            case keyDown:
            case autoKey:
                /* Command keys go to the application (MenuKey, then
                   SystemEdit for the Edit menu). */
                if(evp->modifiers & cmdKey)
                    break;
                /* fall through */
            case mouseUp:
            case keyUp:
                wp = (WindowPeek)FrontWindow();
                break;
            case updateEvt:
            case activateEvt:
                wp = guest_cast<WindowPeek>(evp->message);
                break;
            case diskEvt:
                /* NOTE:  I think the code around toolevent.c:277 should
		      really be here.  I'm not going to get all excited
		      about it right now though. */
                break;
        }
        if(wp)
        {
            rn = WINDOW_KIND(wp);
            if((retval = rn < 0))
            {
                dctlh = rntodctlh(rn);
                if((*dctlh)->dCtlEMask & (1 << evp->what))
                {
                    templ = guest_cast<LONGINT>(evp);
                    Control(rn, accEvent, (Ptr)&templ);
                }
            }
        }
        else
            retval = false;
    }
    else
        retval = false;
    return retval;
}

void Executor::C_SystemMenu(LONGINT menu)
{
    INTEGER id = menu >> 16;
    GUEST<LONGINT> menu_s;

    if(das().handler && da_handler_menu_p(id))
    {
        da_handler_menu(menu);
        return;
    }
    da_prune();
    DaUnits &d = das();
    for(int i = 0; i < d.count; i++)
    {
        DCtlHandle dctlh = GetDCtlEntry(d.refnum[i]);
        if((*dctlh)->dCtlMenu == id || (*dctlh)->dCtlMenu == LM(MBarEnable))
        {
            menu_s = menu;
            Control(d.refnum[i], accMenu, (Ptr)&menu_s);
            break;
        }
    }
}

/* ── DA Handler ─────────────────────────────────────────────────────────
 * MacPhoenix.  On 7.5.5 a desk accessory opened from its file runs in a
 * process of its own, named after it, whose menu bar is the System's DA
 * Handler menus (Apple, File: Close/Quit, Edit) plus any menu the DA adds.
 * The process ends when its last DA closes.  This is our C++ version of
 * that application. */
void Executor::ROMlib_run_desk_accessory(const FSSpec *file_arg, ConstStringPtr name_arg)
{
    /* Guest code sees these: keep them on this (low) stack. */
    FSSpec file = *file_arg;
    Str255 drvr;
    drvr[0] = 0;
    if(name_arg)
        memcpy(drvr, name_arg, name_arg[0] + 1);

    das();
    da_units.handler = true;
    da_quit = false;

    bool own_file = file.name[0] != 0;
    if(own_file)
    {
        WDPBRec wd = {};
        wd.ioVRefNum = file.vRefNum;
        wd.ioWDDirID = file.parID;
        wd.ioWDProcID = "Xctr"_4;
        PBOpenWD(&wd, false);
        SetVol(nullptr, wd.ioVRefNum);
        LM(CurApName)[0] = std::min<int>(file.name[0], 31);
        memcpy(LM(CurApName) + 1, file.name + 1, LM(CurApName)[0]);
    }
    else
    {
        /* A DRVR in the System file: named after it, without the
           leading null of a desk accessory's name. */
        int skip = drvr[0] && drvr[1] == 0 ? 1 : 0;
        LM(CurApName)[0] = std::min<int>(drvr[0] - skip, 31);
        memcpy(LM(CurApName) + 1, drvr + 1 + skip, LM(CurApName)[0]);
    }

    process_layout_partition(own_file ? file.name : (ConstStringPtr) "\0");
    FInfo finfo = {};
    if(own_file)
    {
        LM(CurApRefNum) = OpenResFile(file.name);
        GetFInfo(file.name, 0, &finfo);
        if(!drvr[0])
            if(Handle h = Get1IndResource("DRVR"_4, 1))
            {
                GUEST<INTEGER> id;
                GUEST<ResType> type;
                GetResInfo(h, &id, &type, drvr);
            }
    }
    process_create(true, finfo.fdType, finfo.fdCreator);

    EM_A5 = ptr_to_longint(LM(CurrentA5));
    EM_A7 = ptr_to_longint(LM(CurStackBase)) - 4096;
    LM(TheZone) = LM(ApplZone);
    LM(QDExist) = LM(WWExist) = EXIST_NO;

    QDGlobals *qd = (QDGlobals *)NewPtrClear(sizeof(QDGlobals));
    InitGraf(&qd->thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(nullptr);
    InitCursor();

    if(MenuHandle apple = GetMenu(-kDAAppleMenu))
    {
        /* "About ^0…": ^0 is the DA's name. */
        Str255 text;
        GetMenuItemText(apple, 1, text);
        std::string t((char *)text + 1, text[0]);
        auto at = t.find("^0");
        if(at != std::string::npos)
        {
            t.replace(at, 2, std::string((char *)LM(CurApName) + 1, LM(CurApName)[0]));
            text[0] = std::min<size_t>(t.size(), 255);
            memcpy(text + 1, t.data(), text[0]);
            SetMenuItemText(apple, 1, text);
        }
        AppendResMenu(apple, "DRVR"_4);
        InsertMenu(apple, 0);
    }
    if(MenuHandle m = GetMenu(-kDAFileMenu))
        InsertMenu(m, 0);
    if(MenuHandle m = GetMenu(-kDAEditMenu))
        InsertMenu(m, 0);
    DrawMenuBar();

    if(drvr[0])
        OpenDeskAcc(drvr);

    EventRecord evt;
    for(;;)
    {
        da_prune();
        if(!da_units.count)
            break;
        if(da_quit)
        {
            da_close_all();
            break;
        }
        SystemTask();
        if(!WaitNextEvent(everyEvent, &evt, 6, nullptr))
            continue;
        switch(evt.what)
        {
            case mouseDown:
            {
                GUEST<WindowPtr> wp;
                switch(FindWindow(evt.where, &wp))
                {
                    case inMenuBar:
                        if(LONGINT choice = MenuSelect(evt.where))
                            SystemMenu(choice); /* ours or a DA's */
                        HiliteMenu(0);
                        break;
                    case inSysWindow:
                        SystemClick(&evt, wp);
                        break;
                }
                break;
            }
            case keyDown:
            case autoKey:
                if(evt.modifiers & cmdKey)
                {
                    if(LONGINT choice = MenuKey(evt.message & charCodeMask))
                        SystemMenu(choice);
                    HiliteMenu(0);
                }
                break;
        }
    }

    /* The DAs' code and globals are in our partition: their unit table
       entries go with it. */
    da_close_all();
    for(int i = 0; i < da_units.opened_count; i++)
    {
        INTEGER unit = -da_units.opened[i] - 1;
        if(DCtlHandle h = LM(UTableBase)[unit])
        {
            TheZoneGuard guard(LM(SysZone));
            DisposeHandle((Handle)h);
        }
        LM(UTableBase)[unit] = nullptr;
    }
    da_units.count = da_units.opened_count = 0;
}
