/* MacPhoenix: System 7's system menus, Help and Application.
 *
 * They sit at the end of the menu list (InsertMenu puts application menus
 * before them), with an icon suite for a title: the title string is $01
 * followed by the suite's handle, as on a real 7.5.5 boot. The menu bar
 * definition right-aligns them (stdmbdf.cpp). Both come from the System
 * file: MENU -16490 with the balloon icon (ics# -16490), and MENU -16489,
 * whose "^0" becomes the application's name and which lists the processes.
 * Choosing one of their own items is handled here; items an application
 * adds to the Help menu are returned to it. */

#include <base/common.h>

#include <MenuMgr.h>
#include <ResourceMgr.h>
#include <MemoryMgr.h>
#include <FileMgr.h>
#include <Iconutil.h>
#include <SegmentLdr.h>
#include <ToolboxUtil.h>
#include <OSUtil.h>
#include <FontMgr.h>

#include <menu/menu.h>
#include <mman/mman.h>

#include <cstring>

using namespace Executor;

namespace
{
enum
{
    kHMHelpMenuID = -16490,
    kApplicationMenuID = -16489,
};

MenuHandle help_menu;
MenuHandle app_menu;
INTEGER app_menu_refnum; /* the application app_menu was built for */
INTEGER help_system_items; /* items before the application's own */

/* title = [5][$01][suite handle] */
void set_icon_title(MenuHandle mh, Handle suite)
{
    uint8_t title[6] = { 5, 1 };
    uint32_t h = (uint32_t)(uintptr_t)suite;
    title[2] = h >> 24;
    title[3] = h >> 16;
    title[4] = h >> 8;
    title[5] = h;
    int oldlen = (*mh)->menuData[0];
    Munger((Handle)mh, offsetof(MenuInfo, menuData), nullptr, oldlen + 1,
           (Ptr)title, sizeof title);
}

/* A system-heap copy of the members of an icon suite. */
Handle copy_suite(Handle suite)
{
    Handle copy;
    if(NewIconSuite(out(copy)) != noErr)
        return nullptr;
    static const ResType types[] = { "ics#"_4, "ics4"_4, "ics8"_4,
                                     "ICN#"_4, "icl4"_4, "icl8"_4 };
    for(ResType t : types)
    {
        Handle h;
        if(GetIconFromSuite(out(h), suite, t) != noErr || !h)
            continue;
        if(!*h)
            LoadResource(h);
        GUEST<Handle> c = h;
        if(*h && HandToHand(&c) == noErr)
            AddIconToSuite(c, copy, t);
    }
    return copy;
}

/* BNDL: sig(4) sigID(2) ntypes-1(2) { type(4) n-1(2) { local(2) id(2) } } */
bool bndl_lookup(Handle bndl, ResType type, INTEGER local, INTEGER *id)
{
    uint8_t *p = (uint8_t *)*bndl, *end = p + GetHandleSize(bndl);
    if(end - p < 8)
        return false;
    int ntypes = *(GUEST<INTEGER> *)(p + 6) + 1;
    p += 8;
    for(int t = 0; t < ntypes && p + 6 <= end; t++)
    {
        ResType rt = *(GUEST<OSType> *)p;
        int n = *(GUEST<INTEGER> *)(p + 4) + 1;
        p += 6;
        for(int k = 0; k < n && p + 4 <= end; k++, p += 4)
            if(rt == type && *(GUEST<INTEGER> *)p == local)
            {
                *id = *(GUEST<INTEGER> *)(p + 2);
                return true;
            }
    }
    return false;
}

/* The ICN# family a file's bundle gives files of this creator and type:
   BNDL (signature = creator) -> FREF for the type -> local icon -> ICN#.
   Looks only in the current resource file. */
bool bundle_icon(OSType creator, OSType type, INTEGER *icon_id)
{
    for(INTEGER i = 1; i <= Count1Resources("BNDL"_4); i++)
    {
        Handle bndl = Get1IndResource("BNDL"_4, i);
        if(!bndl || GetHandleSize(bndl) < 8 || *(GUEST<OSType> *)*bndl != creator)
            continue;
        for(INTEGER local = 0; local < 256; local++)
        {
            INTEGER fref_id;
            if(!bndl_lookup(bndl, "FREF"_4, local, &fref_id))
                continue;
            Handle fref = Get1Resource("FREF"_4, fref_id);
            if(!fref || GetHandleSize(fref) < 6 || *(GUEST<OSType> *)*fref != type)
                continue;
            if(bndl_lookup(bndl, "ICN#"_4, *(GUEST<INTEGER> *)(*fref + 4), icon_id))
                return true;
        }
    }
    return false;
}

/* The current application's icon: from its own bundle, else from the
   System file's (Finder's lives there, under 'MACS'). */
Handle app_icon_suite()
{
    Handle suite = nullptr;
    INTEGER refnum = LM(CurApRefNum);
    FCBPBRec fcb;
    Str255 name;
    memset(&fcb, 0, sizeof fcb);
    fcb.ioNamePtr = name;
    fcb.ioRefNum = refnum;
    OSType type = 0, creator = 0;
    if(PBGetFCBInfo(&fcb, false) == noErr)
    {
        HParamBlockRec pb;
        memset(&pb, 0, sizeof pb);
        pb.fileParam.ioNamePtr = name;
        pb.fileParam.ioVRefNum = fcb.ioFCBVRefNum;
        pb.fileParam.ioDirID = fcb.ioFCBParID;
        if(PBHGetFInfo(&pb, false) == noErr)
        {
            type = pb.fileParam.ioFlFndrInfo.fdType;
            creator = pb.fileParam.ioFlFndrInfo.fdCreator;
        }
    }

    INTEGER saved = CurResFile();
    for(INTEGER file : { refnum, (INTEGER)LM(SysMap) })
    {
        UseResFile(file);
        INTEGER icon_id;
        Handle found;
        if(bundle_icon(creator, type, &icon_id)
           && GetIconSuite(out(found), icon_id, svAllSmallData | svAllLargeData) == noErr)
        {
            suite = copy_suite(found);
            DisposeIconSuite(found, false);
            break;
        }
    }
    UseResFile(saved);
    return suite;
}

void make_help_menu()
{
    if(help_menu)
        return;
    MenuHandle mh = GetMenu(kHMHelpMenuID);
    if(!mh)
        return;
    DetachResource((Handle)mh);
    help_system_items = CountMItems(mh);
    Handle suite;
    if(GetIconSuite(out(suite), kHMHelpMenuID, svAllSmallData) == noErr)
        set_icon_title(mh, suite);
    help_menu = mh;
}

void make_app_menu()
{
    MenuHandle mh = GetMenu(kApplicationMenuID);
    if(!mh)
        return;
    DetachResource((Handle)mh);

    Str255 app;
    memcpy(app, LM(CurApName), LM(CurApName)[0] + 1);
    for(INTEGER i = 1; i <= CountMItems(mh); i++)
    {
        Str255 text, out_text;
        GetMenuItemText(mh, i, text);
        int o = 0;
        for(int k = 1; k <= text[0]; k++)
        {
            if(text[k] == '^' && k < text[0] && text[k + 1] == '0')
            {
                for(int a = 1; a <= app[0] && o < 255; a++)
                    out_text[++o] = app[a];
                k++;
            }
            else if(o < 255)
                out_text[++o] = text[k];
        }
        out_text[0] = o;
        SetMenuItemText(mh, i, out_text);
    }
    /* the process list after the resource's divider: this world runs one
       application, so there is nothing to hide or show (as on a real boot
       with only Finder running) */
    AppendMenu(mh, (StringPtr) "\001 ");
    INTEGER n = CountMItems(mh);
    SetMenuItemText(mh, n, app);
    SetItemMark(mh, n, checkMark);
    for(INTEGER i = 1; i <= 3; i++)
        DisableItem(mh, i);

    if(Handle suite = app_icon_suite())
        set_icon_title(mh, suite);
    app_menu = mh;
    app_menu_refnum = LM(CurApRefNum);
}
}

bool Executor::ROMlib_system_menu_p(INTEGER mid)
{
    return mid >= -20480 && mid <= -16385; /* kLo/HiSystemMenuRange */
}

/* HMGetHelpMenuHandle: an application's items follow a divider the Help
   Manager adds the first time it asks. */
MenuHandle Executor::ROMlib_help_menu()
{
    TheZoneGuard guard(LM(SysZone));
    make_help_menu();
    if(help_menu && CountMItems(help_menu) == help_system_items)
        AppendMenu(help_menu, (StringPtr) "\002(-");
    return help_menu;
}

void Executor::ROMlib_install_system_menus()
{
    if(!LM(MenuList))
        return;
    {
        TheZoneGuard guard(LM(SysZone));
        make_help_menu();
        /* the Application menu belongs to the current application; none
           before one is running */
        if(app_menu && app_menu_refnum != LM(CurApRefNum))
        {
            /* the old application's Help items go too */
            while(help_menu && CountMItems(help_menu) > help_system_items)
                DeleteMenuItem(help_menu, CountMItems(help_menu));
            if(ROMlib_mentosix((*app_menu)->menuID) != -1)
                DeleteMenu((*app_menu)->menuID);
            DisposeMenu(app_menu);
            app_menu = nullptr;
        }
        if(!app_menu && LM(CurApRefNum) > 0)
            make_app_menu();
    }
    for(MenuHandle mh : { help_menu, app_menu })
        if(mh && ROMlib_mentosix((*mh)->menuID) == -1)
            InsertMenu(mh, 0);
}

bool Executor::ROMlib_system_menu_select(INTEGER mid, INTEGER item)
{
    if(mid == kApplicationMenuID)
        return true; /* hide/show and switching: one process, nothing to do */
    if(mid == kHMHelpMenuID)
        return item <= help_system_items; /* balloons aren't implemented */
    return false;
}

bool Executor::ROMlib_icon_title_p(MenuHandle mh)
{
    return (*mh)->menuData[0] == 5 && (*mh)->menuData[1] == 1;
}

Handle Executor::ROMlib_icon_title_suite(MenuHandle mh)
{
    const uint8_t *t = (const uint8_t *)(*mh)->menuData + 2;
    return (Handle)(uintptr_t)((uint32_t)t[0] << 24 | t[1] << 16 | t[2] << 8 | t[3]);
}
