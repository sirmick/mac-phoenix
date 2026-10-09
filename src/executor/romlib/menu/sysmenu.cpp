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
#include <rsys/process.h>

#include <MenuMgr.h>
#include <ResourceMgr.h>
#include <MemoryMgr.h>
#include <FileMgr.h>
#include <Iconutil.h>
#include <SegmentLdr.h>
#include <ToolboxUtil.h>
#include <OSUtil.h>
#include <FontMgr.h>
#include <IntlUtil.h>
#include <AppleEvents.h>
#include <ProcessMgr.h>

#include <menu/menu.h>
#include <mman/mman.h>

#include <algorithm>
#include <cstring>
#include <vector>

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
INTEGER app_menu_fixed;  /* items from the resource, divider included */
/* The processes' icon suites, item by item after the fixed ones. */
std::vector<Handle> app_item_suites;
std::vector<std::vector<uint8_t>> app_menu_texts; /* their text, with ^0 */
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

/* The Application menu: one for the whole system, as on 7.5.5.  Its items
   name the current application (^0), then list every process. */
void make_app_menu()
{
    MenuHandle mh = GetMenu(kApplicationMenuID);
    if(!mh)
        return;
    DetachResource((Handle)mh);
    app_menu_fixed = CountMItems(mh);
    app_menu_texts.clear();
    for(INTEGER i = 1; i <= app_menu_fixed; i++)
    {
        Str255 text;
        GetMenuItemText(mh, i, text);
        app_menu_texts.emplace_back(text, text + text[0] + 1);
    }
    app_menu = mh;
}
}

Handle Executor::ROMlib_app_icon_suite()
{
    TheZoneGuard guard(LM(SysZone));
    return app_icon_suite();
}

void Executor::ROMlib_app_menu_update()
{
    if(!app_menu)
        return;
    TheZoneGuard guard(LM(SysZone));
    std::vector<ROMlib_process_entry> procs = ROMlib_process_entries();
    const ROMlib_process_entry *cur = nullptr;
    for(const ROMlib_process_entry &e : procs)
        if(e.current)
            cur = &e;

    Str255 app = { 0 };
    if(cur)
        memcpy(app, cur->name, cur->name[0] + 1);
    for(INTEGER i = 1; i <= app_menu_fixed; i++)
    {
        const std::vector<uint8_t> &text = app_menu_texts[i - 1];
        Str255 out_text;
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
        SetMenuItemText(app_menu, i, out_text);
    }
    /* Hide/Show are not implemented yet. */
    for(INTEGER i = 1; i <= 3 && i <= app_menu_fixed; i++)
        DisableItem(app_menu, i);

    while(CountMItems(app_menu) > app_menu_fixed)
        DeleteMenuItem(app_menu, CountMItems(app_menu));
    /* Each process: its small icon, then its name (the two placeholder
       bytes the menu definition draws the icon over, as in the Apple
       menu). */
    app_item_suites.clear();
    for(const ROMlib_process_entry &e : procs)
    {
        AppendMenu(app_menu, (StringPtr) "\001 ");
        INTEGER n = CountMItems(app_menu);
        Str255 text;
        int len = std::min<int>(e.name[0], 253);
        text[0] = len + 2;
        text[1] = text[2] = 0;
        memcpy(text + 3, e.name + 1, len);
        SetMenuItemText(app_menu, n, text);
        app_item_suites.push_back(e.icon);
        if(e.current)
            SetItemMark(app_menu, n, checkMark);
    }
    if(cur && cur->icon)
        set_icon_title(app_menu, cur->icon);
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
        /* none before an application is running */
        if(!app_menu && LM(CurApRefNum) > 0)
            make_app_menu();
        ROMlib_app_menu_update();
    }
    for(MenuHandle mh : { help_menu, app_menu })
        if(mh && ROMlib_mentosix((*mh)->menuID) == -1)
            InsertMenu(mh, 0);
}

bool Executor::ROMlib_system_menu_select(INTEGER mid, INTEGER item)
{
    if(mid == kApplicationMenuID)
    {
        /* A process: it comes to the front.  Hide/Show: not yet. */
        if(item > app_menu_fixed)
        {
            std::vector<ROMlib_process_entry> procs = ROMlib_process_entries();
            if(item - app_menu_fixed <= (int)procs.size())
            {
                ProcessSerialNumber psn = procs[item - app_menu_fixed - 1].psn;
                SetFrontProcess(&psn);
            }
        }
        return true;
    }
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

/* The Apple menu (Process Manager, System 7). Finder hands over the Apple
   Menu Items folder one item at a time (OSDispatch $31/$32); AppendResMenu
   of 'DRVR' on an application's Apple menu, which on 7.5.5 lists these
   items instead of desk accessories, marks that menu as theirs, and the
   menu is brought up to date before it is pulled down. Item text is the
   name behind two placeholder bytes, as on a real boot; the menu
   definition draws the item's small icon in their place (18-pixel rows,
   text 20 pixels in). */
namespace
{
struct AppleItem
{
    std::vector<uint8_t> text; /* Str255: [len][0][0][name] */
    int16_t group;
    Handle suite;
    int32_t key;
};
std::vector<AppleItem> apple_items;
/* The items are the system's; each process has its own Apple menu, built
   from them whenever they changed since (Process Manager state). */
struct AppleMenuState
{
    MenuHandle menu;
    INTEGER base; /* items before the Apple Menu Items ones */
    uint32_t built; /* apple_generation it shows */
};
AppleMenuState apple_state;
uint32_t apple_generation = 1;
#define apple_menu apple_state.menu
#define apple_base apple_state.base
}

void Executor::ROMlib_apple_menu_add(StringPtr name, int16_t group, Handle suite, int32_t key)
{
    apple_items.erase(std::remove_if(apple_items.begin(), apple_items.end(),
                                     [key](const AppleItem& i) { return i.key == key; }),
                      apple_items.end());
    AppleItem item;
    int len = std::min(name[0], (uint8_t)253);
    item.text.assign(len + 3, 0);
    item.text[0] = len + 2;
    memcpy(&item.text[3], name + 1, len);
    item.group = group;
    item.suite = suite;
    item.key = key;
    auto pos = std::upper_bound(apple_items.begin(), apple_items.end(), item,
        [](const AppleItem& a, const AppleItem& b) {
            if(a.group != b.group)
                return a.group < b.group;
            return IUCompString((StringPtr)a.text.data(), (StringPtr)b.text.data()) < 0;
        });
    apple_items.insert(pos, std::move(item));
    ++apple_generation;
}

bool Executor::ROMlib_apple_menu_remove(int32_t key)
{
    auto n = apple_items.size();
    apple_items.erase(std::remove_if(apple_items.begin(), apple_items.end(),
                                     [key](const AppleItem& i) { return !key || i.key == key; }),
                      apple_items.end());
    ++apple_generation;
    return !key || n != apple_items.size();
}

void Executor::ROMlib_apple_menu_attach(MenuHandle mh)
{
    static bool registered;
    if(!registered)
    {
        registered = true;
        ROMlib_process_register_state(&apple_state, sizeof apple_state);
    }
    apple_menu = mh;
    apple_base = CountMItems(mh);
    apple_state.built = 0;
}

void Executor::ROMlib_apple_menu_update()
{
    if(apple_state.built == apple_generation || !apple_menu
       || ROMlib_mentosix((*apple_menu)->menuID) == -1)
        return;
    apple_state.built = apple_generation;
    while(CountMItems(apple_menu) > apple_base)
        DeleteMenuItem(apple_menu, CountMItems(apple_menu));
    for(const AppleItem& item : apple_items)
    {
        INTEGER n = CountMItems(apple_menu) + 1;
        AppendMenu(apple_menu, (StringPtr) "\001x");
        SetMenuItemText(apple_menu, n, (StringPtr)item.text.data());
    }
}

Handle Executor::ROMlib_apple_menu_icon(MenuHandle mh, INTEGER item)
{
    if(mh == app_menu && app_menu && item > app_menu_fixed
       && item - app_menu_fixed <= (INTEGER)app_item_suites.size())
        return app_item_suites[item - app_menu_fixed - 1];
    if(mh != apple_menu || item <= apple_base
       || item - apple_base > (INTEGER)apple_items.size())
        return nullptr;
    return apple_items[item - apple_base - 1].suite;
}

/* OpenDeskAcc of an Apple Menu Items entry: as 7.5.5's Process Manager
   does (scod -16463 +9c96), post the item's owner an 'aevt'/'amis'
   high-level event whose message is the 28-byte Apple event stream
   'aevt' 1 1 ';;;;' then parameter 'amis' 'long' 4 key. Finder reads the
   key with its own parser of that stream and opens the object. */
bool Executor::ROMlib_apple_menu_open(ConstStringPtr name)
{
    auto it = std::find_if(apple_items.begin(), apple_items.end(),
        [name](const AppleItem& i) {
            return i.text[0] == name[0] && !memcmp(&i.text[1], name + 1, name[0]);
        });
    if(it == apple_items.end())
        return false;

    struct
    {
        GUEST<OSType> signature;
        GUEST<int16_t> major, minor;
        GUEST<OSType> marker;
        GUEST<OSType> keyword, type;
        GUEST<int32_t> size, key;
    } msg = { "aevt"_4, 1, 1, ";;;;"_4, "amis"_4, "long"_4, 4, it->key };
    static_assert(sizeof msg == 28);

    EventRecord evt = {};
    evt.what = kHighLevelEvent;
    evt.message = "aevt"_4;
    GUEST<uint32_t> id = "amis"_4;
    memcpy(&evt.where, &id, sizeof id);
    /* To Finder, which handed over the items (from a desk accessory's
       process too). */
    ProcessSerialNumber psn;
    if(!ROMlib_process_with_signature("MACS"_4, &psn))
        GetCurrentProcess(&psn);
    PostHighLevelEvent(&evt, (Ptr)&psn, 0, (Ptr)&msg, sizeof msg, 0x8000 /* receiverIDisPSN */);
    return true;
}
