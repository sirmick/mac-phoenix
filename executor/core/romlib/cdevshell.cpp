/* cdevshell.cpp — the control panel shell (LaunchControlPanel, Processes.h,
 * InterfaceLib 9.0). MacPhoenix.
 *
 * On 9.0 the Finder opens a control panel that is a 'cdev' with
 * LaunchControlPanel: the Process Manager runs it in an application shell
 * of its own, as it runs a desk accessory in the DA Handler. Measured on the
 * G3 (9.0.4, General Controls): a process named after the control panel,
 * the DA Handler's menus ("About General Controls…", File: Close and Quit,
 * Edit), and the panel's dialog with the panel's name for its title, its
 * content the 'nrct' rectangles' union inset by 2 (System 6's Control
 * Panel frame), the port's origin there, so the 'DITL' items keep their
 * System 6 coordinates; where the panel's 'wloc' -16474 says, which the
 * shell writes when it closes, (100, 100) the first time; white (General
 * Controls' grey is its own 'wctb').
 *
 * The cdev protocol is Inside Macintosh: More Macintosh Toolbox, chapter 8,
 * and Technote 215 (cursorDev): the 'mach' resource says whether the panel
 * runs here or to ask macDev first; initDev, then hitDev, nulDev, updateDev,
 * activDev/deactivDev, keyEvtDev, the Edit menu's messages, cursorDev and
 * closeDev, the value each call returns passed to the next. Measured on the
 * G3 (9.0.4, tests/guest/cdev, ctest cdev_shell_executor): an error code
 * (cdevGenErr, cdevMemErr, cdevResErr) neither closes the panel nor puts up
 * an alert, unlike System 7's Finder (IM 8-38): it is passed back as the
 * next value; a nulDev follows initDev at once, before the activate event;
 * with a 'CURS' -4064 each idle sends cursorDev, then nulDev, wherever the
 * cursor is; a panel that can't run here gets the System's stop alert.
 */

#include <base/common.h>
#include <DialogMgr.h>
#include <DialogMgr8.h>
#include <EventMgr.h>
#include <FileMgr.h>
#include <MenuMgr.h>
#include <OSEvent.h>
#include <ProcessMgr.h>
#include <QuickDraw.h>
#include <ResourceMgr.h>
#include <ToolboxEvent.h>
#include <ToolboxUtil.h>
#include <WindowMgr.h>
#include <FontMgr.h>
#include <TextEdit.h>
#include <MemoryMgr.h>
#include <SegmentLdr.h>
#include <DeskMgr.h>
#include <SysErr.h>
#include <Appearance.h>
#include <OSUtil.h>
#include <LowMem.h>
#include <mman/mman.h>
#include <quickdraw/cquick.h>

#include <rsys/desk.h>
#include <rsys/process.h>

#include <algorithm>
#include <base/functions.impl.h>

using namespace Executor;

namespace
{
using cdevfuncp = UPP<LONGINT(INTEGER message, INTEGER item, INTEGER numItems, INTEGER cPanelID,
                              EventRecord *theEvent, LONGINT cdevValue, DialogPtr cpDialog)>;

enum
{
    initDev = 0, hitDev, closeDev, nulDev, updateDev, activDev, deactivDev,
    keyEvtDev, macDev, undoDev, cutDev, copyDev, pasteDev, clearDev,
    cursorDev,
};
constexpr LONGINT cdevUnset = 3;
constexpr INTEGER kCPanelID = -4064;
/* the System's STR# of the shell's messages; 1: the control panel "^0"
   can not be used on this Machine */
constexpr INTEGER kShellStrings = -16474;
/* in the panel's file: where its window was, the content's top left
   (v, h); the shell writes it when the panel closes */
constexpr INTEGER kWhereID = -16474;

/* Where the panel's content goes on the screen: its 'wloc', else (100, 100)
   (the G3, 9.0.4). */
Point panel_where()
{
    Point where = { 100, 100 };
    Handle h = Get1Resource("wloc"_4, kWhereID);
    if(h && *h && GetHandleSize(h) >= 4)
    {
        where.v = ((GUEST<INTEGER> *)*h)[0];
        where.h = ((GUEST<INTEGER> *)*h)[1];
    }
    return where;
}

void save_where(DialogPtr dialog)
{
    SetPort(dialog);
    GUEST<Point> pt;
    pt.v = dialog->portRect.top;
    pt.h = dialog->portRect.left;
    LocalToGlobal(&pt);
    Point where = pt.get();
    INTEGER save = CurResFile();
    UseResFile(LM(CurApRefNum));
    Handle h = Get1Resource("wloc"_4, kWhereID);
    if(!h)
    {
        h = NewHandle(4);
        if(!h)
        {
            UseResFile(save);
            return;
        }
        AddResource(h, "wloc"_4, kWhereID, (StringPtr) "");
    }
    else
    {
        SetHandleSize(h, 4);
        ChangedResource(h);
    }
    if(GetHandleSize(h) >= 4)
    {
        ((GUEST<INTEGER> *)*h)[0] = where.v;
        ((GUEST<INTEGER> *)*h)[1] = where.h;
    }
    UpdateResFile(LM(CurApRefNum));
    UseResFile(save);
}

/* The DA Handler's menu IDs (desk.cpp): File and Edit. */
constexpr INTEGER kFileMenu = 16472, kEditMenu = 16471, kAppleMenu = 16473;

struct Panel
{
    Handle code = nullptr;
    DialogPtr dialog = nullptr;
    LONGINT value = cdevUnset;
    bool open = false;

    /* One message to the cdev; what it answers is the next call's value,
       an error code too (9.0.4's shell: the panel stays open). */
    LONGINT send(INTEGER message, INTEGER item, EventRecord *evt)
    {
        HLockGuard guard(code);
        cdevfuncp fn = (cdevfuncp)*code;
        EventRecord null_event = {};
        /* the panel's file the current resource file at every message (IM:
           More Macintosh Toolbox 8-26), whatever the last one left (Text
           calls UseResFile(0) and then GetPixPat(-4048) in the next); the
           port is left as it is, as the G3's shell does */
        UseResFile(LM(CurApRefNum));
        LONGINT r = fn(message, item, 0, kCPanelID, evt ? evt : &null_event, value, dialog);
        if(message != macDev)
            value = r;
        return r;
    }

    void close()
    {
        if(!open)
            return;
        open = false;
        send(closeDev, 0, nullptr);
        save_where(dialog);
        DisposeDialog(dialog);
        dialog = nullptr;
    }
};

/* Whether the panel runs here, from its 'mach' resource: a soft and a hard
   mask (IM: More Macintosh Toolbox, 8-20 and Table 8-5). $0000FFFF asks the
   cdev with macDev; otherwise the soft mask must cover ROM85 and the hard
   mask's bits be set in HWCfgFlags. Measured on the G3 (9.0.4: ROM85
   $3FFF, HWCfgFlags $EC00): $FFFF0000, $3FFF0000 (Macintosh II) and
   $7FFF0400 (ADB) run, $00000000 doesn't. */
enum class Mach
{
    run, ask, refuse
};
Mach mach_check()
{
    Handle m = Get1Resource("mach"_4, kCPanelID);
    if(!m || !*m || GetHandleSize(m) < 4)
        return Mach::run;
    uint16_t soft = ((GUEST<uint16_t> *)*m)[0], hard = ((GUEST<uint16_t> *)*m)[1];
    if(soft == 0 && hard == 0xFFFF)
        return Mach::ask;
    uint16_t rom85 = LM(ROM85), hw = LM(HWCfgFlags);
    return (rom85 & soft) == rom85 && (hw & hard) == hard ? Mach::run : Mach::refuse;
}

/* The System's stop alert for a panel that can't run here. */
void cannot_run(ConstStringPtr name)
{
    Str255 tmpl, msg;
    GetIndString(tmpl, kShellStrings, 1);
    msg[0] = 0;
    for(int i = 1; i <= tmpl[0]; i++)
    {
        if(tmpl[i] == '^' && i < tmpl[0] && tmpl[i + 1] == '0')
        {
            for(int j = 1; j <= name[0] && msg[0] < 255; j++)
                msg[++msg[0]] = name[j];
            i++;
        }
        else if(msg[0] < 255)
            msg[++msg[0]] = tmpl[i];
    }
    GUEST<INTEGER> hit;
    StandardAlert(kAlertStopAlert, msg, nullptr, nullptr, &hit);
}

/* The union of the 'nrct' rectangles (a count, then the rects). */
bool panel_rect(Rect *r)
{
    Handle h = Get1Resource("nrct"_4, kCPanelID);
    if(!h || !*h || GetHandleSize(h) < 10)
        return false;
    int n = *(GUEST<INTEGER> *)*h;
    Rect *rects = (Rect *)(*h + 2);
    *r = rects[0];
    for(int i = 1; i < n && 2 + (i + 1) * (int)sizeof(Rect) <= GetHandleSize(h); i++)
        UnionRect(r, &rects[i], r);
    return true;
}
}

/* MacPhoenix: LaunchControlPanel's process (process.cpp) runs this. */
void Executor::ROMlib_run_control_panel(const FSSpec *file_arg)
{
    /* Guest code sees these: keep them on this (low) stack. */
    FSSpec file = *file_arg;

    WDPBRec wd = {};
    wd.ioVRefNum = file.vRefNum;
    wd.ioWDDirID = file.parID;
    wd.ioWDProcID = "Xctr"_4;
    PBOpenWD(&wd, false);
    SetVol(nullptr, wd.ioVRefNum);
    LM(CurApName)[0] = std::min<int>(file.name[0], 31);
    memcpy(LM(CurApName) + 1, file.name + 1, LM(CurApName)[0]);

    process_layout_partition(file.name);
    LM(CurApRefNum) = OpenResFile(file.name);
    FInfo finfo = {};
    GetFInfo(file.name, 0, &finfo);
    process_create(false, finfo.fdType, finfo.fdCreator);

    EM_A5 = ptr_to_longint(LM(CurrentA5));
    EM_A7 = ptr_to_longint(LM(CurStackBase));
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
    ROMlib_install_da_handler_menus();
    DrawMenuBar();

    Panel panel;
    Rect nr;
    Handle items = Get1Resource("DITL"_4, kCPanelID);
    panel.code = Get1Resource("cdev"_4, kCPanelID);
    if(!panel.code || !items || !panel_rect(&nr))
        return;
    GUEST<Handle> items_s = items;
    if(HandToHand(&items_s) != noErr)
        return;
    items = items_s;

    Mach mach = mach_check();
    if(mach == Mach::refuse || (mach == Mach::ask && panel.send(macDev, 0, nullptr) == 0))
    {
        cannot_run(file.name);
        return;
    }
    bool cursor = Get1Resource("CURS"_4, kCPanelID) != nullptr;

    InsetRect(&nr, 2, 2);
    Point where = panel_where();
    Rect bounds;
    SetRect(&bounds, where.h, where.v, where.h + (nr.right - nr.left), where.v + (nr.bottom - nr.top));
    panel.dialog = NewColorDialog(nullptr, &bounds, file.name, false, noGrowDocProc,
                             (WindowPtr)-1, true, 0, items);
    if(!panel.dialog)
        return;
    SetPort(panel.dialog);
    SetOrigin(nr.left, nr.top);
    panel.open = true;
    ShowWindow(panel.dialog);
    SelectWindow(panel.dialog);

    panel.send(initDev, 0, nullptr);
    panel.send(nulDev, 0, nullptr);

    bool quit = false;
    EventRecord evt;
    while(panel.open && !quit)
    {
        SystemTask();
        if(!WaitNextEvent(everyEvent, &evt, 6, nullptr))
        {
            if(FrontWindow() == panel.dialog)
            {
                if(cursor)
                    panel.send(cursorDev, 0, &evt);
                panel.send(nulDev, 0, &evt);
            }
            continue;
        }
        GUEST<DialogPtr> hit_dialog;
        GUEST<INTEGER> hit_item;
        switch(evt.what)
        {
            case mouseDown:
            {
                GUEST<WindowPtr> wp;
                switch(FindWindow(evt.where, &wp))
                {
                    case inMenuBar:
                    {
                        LONGINT choice = MenuSelect(evt.where);
                        INTEGER id = choice >> 16, item = choice & 0xFFFF;
                        if(id == kFileMenu)
                            item == 1 ? panel.close() : (void)(quit = true);
                        else if(id == kEditMenu && item != 2)
                            panel.send(item == 1 ? undoDev : cutDev + item - 3, 0, &evt);
                        else if(id == kAppleMenu && item > 2)
                        {
                            Str255 name;
                            GetMenuItemText(GetMenuHandle(id), item, name);
                            OpenDeskAcc(name);
                        }
                        HiliteMenu(0);
                        break;
                    }
                    case inSysWindow:
                        SystemClick(&evt, wp);
                        break;
                    case inDrag:
                    {
                        Rect limit = qd->screenBits.bounds;
                        InsetRect(&limit, 4, 4);
                        DragWindow(wp, evt.where, &limit);
                        break;
                    }
                    case inGoAway:
                        if(TrackGoAway(wp, evt.where))
                            panel.close();
                        break;
                    case inContent:
                        if(wp != FrontWindow())
                            SelectWindow(wp);
                        else if(DialogSelect(&evt, &hit_dialog, &hit_item)
                                && hit_dialog == panel.dialog)
                            panel.send(hitDev, hit_item, &evt);
                        break;
                }
                break;
            }
            case keyDown:
            case autoKey:
                if(evt.modifiers & cmdKey)
                {
                    LONGINT choice = MenuKey(evt.message & charCodeMask);
                    INTEGER id = choice >> 16, item = choice & 0xFFFF;
                    if(id == kFileMenu)
                        item == 1 ? panel.close() : (void)(quit = true);
                    else if(id == kEditMenu && item != 2)
                        panel.send(item == 1 ? undoDev : cutDev + item - 3, 0, &evt);
                    HiliteMenu(0);
                    break;
                }
                /* The cdev first; it makes the event a null event to keep
                   it from the Dialog Manager. */
                panel.send(keyEvtDev, 0, &evt);
                if(evt.what == keyDown || evt.what == autoKey)
                    if(DialogSelect(&evt, &hit_dialog, &hit_item) && hit_dialog == panel.dialog)
                        panel.send(hitDev, hit_item, &evt);
                break;
            case updateEvt:
                if(ptr_from_longint<WindowPtr>(evt.message) == panel.dialog)
                {
                    BeginUpdate(panel.dialog);
                    DrawDialog(panel.dialog);
                    panel.send(updateDev, 0, &evt);
                    EndUpdate(panel.dialog);
                }
                break;
            case activateEvt:
                if(ptr_from_longint<WindowPtr>(evt.message) == panel.dialog)
                {
                    DialogSelect(&evt, &hit_dialog, &hit_item);
                    panel.send(evt.modifiers & activeFlag ? activDev : deactivDev, 0, &evt);
                }
                break;
            case kHighLevelEvent:
                /* Quit (the Process Manager's Quit All for a restart or
                   shut down) */
                if(evt.message == "aevt"_4 && *(GUEST<OSType> *)&evt.where == "quit"_4)
                    quit = true;
                break;
        }
    }
    panel.close();
}
