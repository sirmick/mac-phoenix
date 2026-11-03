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
 * System 6 coordinates, at (6, 46) on the screen, on the dialog grey.
 *
 * The cdev protocol is Inside Macintosh: More Macintosh Toolbox, chapter 8:
 * the 'mach' resource says whether to ask macDev first; initDev, then
 * hitDev, nulDev, updateDev, activDev/deactivDev, keyEvtDev, the Edit
 * menu's messages, and closeDev, the value each call returns passed to the
 * next (an error code instead closes the panel).
 */

#include <base/common.h>
#include <DialogMgr.h>
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
};
enum
{
    cdevGenErr = -1, cdevMemErr = 0, cdevResErr = 1, cdevUnset = 3,
};
constexpr INTEGER kCPanelID = -4064;

/* The DA Handler's menu IDs (desk.cpp): File and Edit. */
constexpr INTEGER kFileMenu = 16472, kEditMenu = 16471, kAppleMenu = 16473;

struct Panel
{
    Handle code = nullptr;
    DialogPtr dialog = nullptr;
    LONGINT value = cdevUnset;
    bool open = false;

    /* One message to the cdev; false when it answers an error, and the
       panel closes. */
    bool send(INTEGER message, INTEGER item, EventRecord *evt)
    {
        HLockGuard guard(code);
        cdevfuncp fn = (cdevfuncp)*code;
        EventRecord null_event = {};
        LONGINT r = fn(message, item, 0, kCPanelID, evt ? evt : &null_event, value, dialog);
        if(message == macDev)
            return r != 0;
        if(r == cdevGenErr || r == cdevMemErr || r == cdevResErr)
        {
            if(message != closeDev)
                close(false);
            return false;
        }
        value = r;
        return true;
    }

    void close(bool tell)
    {
        if(!open)
            return;
        open = false;
        if(tell)
            send(closeDev, 0, nullptr);
        DisposeDialog(dialog);
        dialog = nullptr;
    }
};

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

    /* 'mach' $0000FFFF: the cdev decides, with macDev. */
    if(Handle m = Get1Resource("mach"_4, kCPanelID))
        if(GetHandleSize(m) >= 4 && *(GUEST<uint32_t> *)*m == 0x0000FFFFu
           && !panel.send(macDev, 0, nullptr))
            return;

    InsetRect(&nr, 2, 2);
    Rect bounds;
    SetRect(&bounds, 6, 46, 6 + (nr.right - nr.left), 46 + (nr.bottom - nr.top));
    panel.dialog = NewColorDialog(nullptr, &bounds, file.name, false, noGrowDocProc,
                             (WindowPtr)-1, true, 0, items);
    if(!panel.dialog)
        return;
    SetPort(panel.dialog);
    SetOrigin(nr.left, nr.top);
    /* the dialog grey behind the panel, as the G3's shell has it */
    SetThemeWindowBackground(panel.dialog, kThemeBrushDialogBackgroundActive, false);
    panel.open = true;
    ShowWindow(panel.dialog);
    SelectWindow(panel.dialog);

    if(!panel.send(initDev, 0, nullptr))
        return;

    bool quit = false;
    EventRecord evt;
    while(panel.open && !quit)
    {
        SystemTask();
        if(!WaitNextEvent(everyEvent, &evt, 6, nullptr))
        {
            if(FrontWindow() == panel.dialog)
                panel.send(nulDev, 0, &evt);
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
                            item == 1 ? panel.close(true) : (void)(quit = true);
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
                            panel.close(true);
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
                        item == 1 ? panel.close(true) : (void)(quit = true);
                    else if(id == kEditMenu && item != 2)
                        panel.send(item == 1 ? undoDev : cutDev + item - 3, 0, &evt);
                    HiliteMenu(0);
                    break;
                }
                /* The cdev first; it makes the event a null event to keep
                   it from the Dialog Manager. */
                if(!panel.send(keyEvtDev, 0, &evt))
                    break;
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
    panel.close(true);
}
