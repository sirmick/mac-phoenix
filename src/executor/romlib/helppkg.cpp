/* MacPhoenix: Apple's Help Manager, the System file's PACK 14, in place of
 * Executor's stubs (balloon.cpp).
 *
 * On a 7.5.5 boot the Pack14 trap ($A830) goes to a System routine that
 * enters the package through CallDetachedPackage ($668): A0 the package's
 * detached handle (low memory $BE0), A1 its lock count (ExpandMem+$130),
 * D0 the selector with the parameter size in words in the high byte. The
 * package and the System's patches share a 296-byte globals block at
 * ExpandMem+$78 (+$11E: balloons on; +$10C and +$110: the per-process
 * menu and dialog resource IDs, a count word then 12-byte records) that
 * the System's boot code sets up; its values here are a real boot's.
 *
 * Selector -3, which the Process Manager's init calls once, is the
 * System's own, not the package's: it loads the Help menu (MENU -16490),
 * gives it its icon title and inserts it in the system menu list (what
 * menu/sysmenu.cpp does). The Help menu's own items are the System's
 * MenuSelect patch, which is ROMlib_help_about and _toggle_balloons here.
 */
#include <base/common.h>
#include <rsys/helppkg.h>
#include <rsys/version.h>
#include <rsys/process.h>
#include <base/trapglue.h>
#include <base/functions.impl.h>
#include <menu/menu.h>
#include <mman/mman.h>

#include <AppleEvents.h>
#include <DialogMgr.h>
#include <HelpMgr.h>
#include <MemoryMgr.h>
#include <MenuMgr.h>
#include <QuickDraw.h>
#include <ResourceMgr.h>
#include <ToolboxUtil.h>

#include <cstdio>
#include <cstring>

using namespace Executor;

namespace
{
Handle package; /* detached, in the System heap */
Ptr code; /* 68k: the trap's entry and the thunks C++ calls through */

/* movea.l $BE0.w,a0; movea.l $2B6.w,a1; lea $130(a1),a1;
   move.l $668.w,-(sp); rts -- the System's handler, without its
   HMGetBalloons fast path (the package's selector 3 reads the same byte). */
const uint8_t entry_code[] = { 0x20, 0x78, 0x0B, 0xE0, 0x22, 0x78, 0x02, 0xB6,
                               0x43, 0xE9, 0x01, 0x30, 0x2F, 0x38, 0x06, 0x68,
                               0x4E, 0x75 };
/* move.w #sel,d0 then the entry: a Pascal routine of the selector's
   parameters for the UPP call. The call's return address must be right
   above the parameters when CallDetachedPackage re-stacks them, as it is
   with Apple's inline glue, so the thunk enters the package the way the
   trap would rather than taking the trap itself (which would push its
   own return address in between). */
const uint8_t thunk_head[] = { 0x30, 0x3C, 0x00, 0x00 };
enum
{
    thunk_size = sizeof thunk_head + sizeof entry_code,
    entry_off = 0,
    set_balloons_off = sizeof entry_code,
    idle_off = set_balloons_off + thunk_size,
    menu_balloon_off = idle_off + thunk_size,
    remove_balloon_off = menu_balloon_off + thunk_size,
    code_size = remove_balloon_off + thunk_size,
};

enum
{
    globals_size = 0x128,
    hmMenuResIDs = 0x10C, /* Handle */
    hmDialogResIDs = 0x110, /* Handle */
    hmBalloonsOn = 0x11E, /* Boolean */
    hmAboutUp = 0x120, /* Boolean: the About dialog is up */
};

uint8_t *globals()
{
    AE_info_t *em = LM(AE_info);
    return em ? (uint8_t *)(Ptr)em->emHelpGlobals : nullptr;
}

void put_thunk(size_t off, uint16_t selector)
{
    memcpy(code + off, thunk_head, sizeof thunk_head);
    code[off + 2] = selector >> 8;
    code[off + 3] = selector;
    memcpy(code + off + sizeof thunk_head, entry_code, sizeof entry_code);
}

bool load()
{
    TheZoneGuard guard(LM(SysZone));
    INTEGER saved = CurResFile();
    UseResFile(LM(SysMap));
    Handle h = Get1Resource("PACK"_4, 14);
    if(h)
    {
        LoadResource(h);
        if(*h)
        {
            DetachResource(h);
            HNoPurge(h);
        }
        else
            h = nullptr;
    }
    UseResFile(saved);
    if(!h)
        return false;

    code = NewPtrSys(code_size);
    if(!code)
    {
        DisposeHandle(h);
        return false;
    }
    memcpy(code + entry_off, entry_code, sizeof entry_code);
    put_thunk(set_balloons_off, 0x0104); /* HMSetBalloons */
    put_thunk(idle_off, 0x00FC); /* selector -4 */
    put_thunk(menu_balloon_off, 0x0E05); /* HMShowMenuBalloon */
    put_thunk(remove_balloon_off, 0x0002); /* HMRemoveBalloon */
    package = h;
    fprintf(stderr, "[Executor] Help Manager: the System's PACK 14 (%d bytes)\n",
            (int)GetHandleSize(h));
    return true;
}

/* The globals as the System's boot code leaves them. */
bool make_globals(AE_info_t *em)
{
    TheZoneGuard guard(LM(SysZone));
    Ptr g = NewPtrSysClear(globals_size);
    if(!g)
        return false;
    uint8_t *p = (uint8_t *)g;
    auto word = [p](int off, uint16_t v) {
        p[off] = v >> 8;
        p[off + 1] = v;
    };
    word(0x08, 0xFFFF);
    word(0xB2, 0xFFFF);
    word(0xBA, 10);
    word(0xDA, 5);
    p[0x121] = 1;
    p[0x123] = 1;
    word(0x124, 0x4000);
    word(0x126, 0x000C);
    *(GUEST<Handle> *)(p + hmMenuResIDs) = NewHandleSysClear(2);
    *(GUEST<Handle> *)(p + hmDialogResIDs) = NewHandleSysClear(2);
    em->emHelpGlobals = g;
    em->emHelpPackageLock = 0;
    return true;
}
}

void Executor::ROMlib_install_help_package()
{
    if(!ROMlib_apple_system_file || !LM(SysMapHndl))
        return;
    if(!package && !load())
        return;
    AE_info_t *em = LM(AE_info);
    if(!em)
        return;
    if(!em->emHelpGlobals && !make_globals(em))
        return;
    LM(HelpPackage) = package;
    LM(SystemMenuList) = ROMlib_system_menu_list();
    tooltraptable[0xA830 & 0x3FF] = US_TO_SYN68K(code + entry_off);
}

bool Executor::ROMlib_help_package_p()
{
    return package != nullptr;
}

bool Executor::ROMlib_help_balloons_p()
{
    uint8_t *g = globals();
    return package && g && g[hmBalloonsOn];
}

void Executor::ROMlib_help_idle()
{
    if(!ROMlib_help_balloons_p() || !ROMlib_process_is_front())
        return;
    UPP<OSErr()> idle((void *)(code + idle_off));
    idle();
}

/* As Apple's MDEF 0 calls it: the menu's enable flags, no reserved
   value, the tip 8 pixels in from the item's right edge at its middle,
   the item's rectangle as the alternate, no tip procedure, proc and
   variant 0. */
void Executor::ROMlib_help_menu_balloon(MenuHandle mh, INTEGER item, const Rect *r)
{
    if(!ROMlib_help_balloons_p() || !mh || !item)
        return;
    Point tip;
    tip.h = r->right - 8;
    tip.v = (r->top + r->bottom) / 2;
    Rect alt = *r;
    UPP<OSErr(INTEGER, INTEGER, int32_t, int32_t, Point, Rect *, Ptr, INTEGER, INTEGER)>
        show((void *)(code + menu_balloon_off));
    show(item, (*mh)->menuID, (*mh)->enableFlags, 0, tip, &alt, nullptr, 0, 0);
}

void Executor::ROMlib_help_remove_balloon()
{
    if(!ROMlib_help_balloons_p())
        return;
    UPP<OSErr()> remove((void *)(code + remove_balloon_off));
    remove();
}

/* The System's: DLOG -5696 with item 1 as its default, the globals'
   flag up while it shows. */
void Executor::ROMlib_help_about()
{
    uint8_t *g = globals();
    if(!g)
        return;
    GUEST<GrafPtr> saved;
    GetPort(&saved);
    DialogPtr dp = GetNewDialog(-5696, nullptr, (WindowPtr)-1);
    if(dp)
    {
        SetDialogDefaultItem(dp, 1);
        SetPort(dp);
        g[hmAboutUp] = 1;
        GUEST<INTEGER> hit;
        do
            ModalDialog(nullptr, &hit);
        while(hit != 1);
        DisposeDialog(dp);
        g[hmAboutUp] = 0;
    }
    SetPort(saved);
}

void Executor::ROMlib_help_toggle_balloons()
{
    if(!package)
        return;
    bool on = !ROMlib_help_balloons_p();
    UPP<OSErr(Boolean)> set((void *)(code + set_balloons_off));
    OSErr err = set(on);
    fprintf(stderr, "[Executor] Help Manager: balloons %s (%d, flag %d)\n",
            on ? "on" : "off", err, globals() ? globals()[hmBalloonsOn] : -1);
    if(err == noErr)
        ROMlib_help_fix_menu_text();
}

/* Item 3 of the current Help menu: STR -5696 "Show Balloons" or -5695
   "Hide Balloons" (the resource's own "Show/Hide Balloons" without). */
void Executor::ROMlib_help_fix_menu_text()
{
    if(!package)
        return;
    MenuHandle mh = GetMenuHandle(-16490);
    if(!mh)
        return;
    StringHandle s = GetString(ROMlib_help_balloons_p() ? -5695 : -5696);
    if(s)
    {
        HLock((Handle)s);
        SetMenuItemText(mh, 3, *s);
        HUnlock((Handle)s);
    }
    else
        SetMenuItemText(mh, 3, (StringPtr) "\022Show/Hide Balloons");
}
