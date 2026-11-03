/* menu: the Menu Manager with 8.x's calls (InterfaceLib, MenusLib,
   AppearanceLib), differential (tools/difftest/run.py menu): item text
   with and without metacharacters, insertion, marks, styles, command
   keys and glyphs, icons by number and by handle, the menu's size
   (CalcMenuSize: font metrics), hierarchical items, and MenusLib's 8.5
   calls bound by name (IsMenuItemEnabled, SetSubmenuParentChoosable, the
   menu bar clock). */
#include "probe.h"
#include <Menus.h>

#define SET(x) memset(&(x), SENTINEL, sizeof(x))

static void dump_items(const char *what, MenuHandle m)
{
    int i, n = CountMItems(m);
    char k[64];
    sprintf(k, "%s.count", what);
    out(k, "%d", n);
    for(i = 1; i <= n; i++)
    {
        Str255 t;
        CharParameter mark, cmd;
        Style style[2];
        short icon;
        SET(t);
        GetMenuItemText(m, i, t);
        sprintf(k, "%s.%d.text", what, i);
        out_str(k, t);
        GetItemMark(m, i, &mark);
        GetItemCmd(m, i, &cmd);
        memset(style, SENTINEL, sizeof style);
        GetItemStyle(m, i, style);
        GetItemIcon(m, i, &icon);
        sprintf(k, "%s.%d.attrs", what, i);
        out(k, "mark %d cmd %d style %02x %02x icon %d", (int)mark, (int)cmd, (unsigned char)style[0], (unsigned char)style[1], icon);
    }
    /* the menu's data from enableFlags on (not the definition's handle) */
    sprintf(k, "%s.size", what);
    out(k, "%ld", GetHandleSize((Handle)m));
    sprintf(k, "%s.data", what);
    HLock((Handle)m);
    out_hex(k, (char *)*m + 10, GetHandleSize((Handle)m) - 10);
    HUnlock((Handle)m);
}

int main(void)
{
    MenuHandle m;
    char k[64];
    int i;
    probe_open("menu");
    if(SECTION("append"))
    {
        m = NewMenu(200, "\pProbe");
        AppendMenu(m, "\pPlain;Bold<B;Icon^1;Mark!*;Key/K;(Disabled;-");
        AppendMenu(m, "\pTwo/T;Three");
        dump_items("metachars", m);
        out("enable", "%08lx", (long)(**m).enableFlags);
        DisposeMenu(m);
    }
    if(SECTION("text"))
    {
        m = NewMenu(201, "\pText");
        AppendMenu(m, "\pa");
        SetMenuItemText(m, 1, "\pLonger text than before");
        SetMenuItemText(m, 1, "\pab");
        InsertMenuItem(m, "\pInserted;Second", 0);
        InsertMenuItem(m, "\pAfter2", 2);
        InsertMenuItem(m, "\pAtEnd", 99);
        DeleteMenuItem(m, 1);
        dump_items("text", m);
        out("AppendMenuItemText", "%d", (int)AppendMenuItemText(m, "\pLiteral;/X<B(!"));
        out("InsertMenuItemText", "%d", (int)InsertMenuItemText(m, "\p^0 meta", 1));
        dump_items("literal", m);
        DisposeMenu(m);
    }
    if(SECTION("size"))
    {
        m = NewMenu(202, "\pSize");
        AppendMenu(m, "\pShort;A much longer item title here;With key/W;-;Last");
        CalcMenuSize(m);
        out("size", "%d x %d", (**m).menuWidth, (**m).menuHeight);
        SetItemStyle(m, 2, bold);
        CalcMenuSize(m);
        out("size.bold", "%d x %d", (**m).menuWidth, (**m).menuHeight);
        out("MBarHeight", "%d", GetMBarHeight());
        DisposeMenu(m);
    }
    /* which calls recompute the size at once and which only mark it
       (-1, computed when next needed): PowerPlant fills a menu whose
       menuProc still holds the MDEF's ID, not a handle */
    if(SECTION("dirty"))
    {
        static const char *calls[] = { "SetItemCmd", "SetMenuItemText", "SetItemMark", "SetItemStyle",
                                       "SetItemIcon", "CheckItem", "DisableItem", "AppendMenu",
                                       "InsertMenuItem", "DeleteMenuItem" };
        int i;
        m = NewMenu(208, "\pDirty");
        AppendMenu(m, "\pOne;Two;Three");
        for(i = 0; i < (int)(sizeof calls / sizeof calls[0]); i++)
        {
            char k[64];
            CalcMenuSize(m);
            switch(i)
            {
                case 0: SetItemCmd(m, 1, 'W'); break;
                case 1: SetMenuItemText(m, 2, "\pA longer second item"); break;
                case 2: SetItemMark(m, 1, checkMark); break;
                case 3: SetItemStyle(m, 3, bold); break;
                case 4: SetItemIcon(m, 1, 1); break;
                case 5: CheckItem(m, 2, true); break;
                case 6: DisableItem(m, 3); break;
                case 7: AppendMenu(m, "\pFour"); break;
                case 8: InsertMenuItem(m, "\pZero", 0); break;
                case 9: DeleteMenuItem(m, 1); break;
            }
            sprintf(k, "%s", calls[i]);
            out(k, "%d x %d", (**m).menuWidth, (**m).menuHeight);
        }
        DisposeMenu(m);
    }
    if(SECTION("marks"))
    {
        m = NewMenu(203, "\pMarks");
        AppendMenu(m, "\pa;b;c;d");
        CheckMenuItem(m, 1, true);
        SetItemMark(m, 2, 0x13);
        SetItemCmd(m, 3, 0x1B);           /* hierarchical */
        SetItemMark(m, 3, 210);
        DisableItem(m, 4);
        DisableItem(m, 1);
        EnableItem(m, 4);
        dump_items("marks", m);
        out("enable", "%08lx", (long)(**m).enableFlags);
        {
            typedef Boolean (*IsMenuItemEnabled_t)(MenuHandle, UInt16);
            IsMenuItemEnabled_t f = (IsMenuItemEnabled_t)probe_bind("MenusLib", "IsMenuItemEnabled");
            if(f)
                for(i = 0; i <= 4; i++)
                {
                    sprintf(k, "IsMenuItemEnabled.%d", i);
                    out(k, "%d", f(m, i));
                }
        }
        DisposeMenu(m);
    }
    if(SECTION("glyphs"))
    {
        m = NewMenu(204, "\pGlyph");
        AppendMenu(m, "\pa/A;b");
        out("SetMenuItemKeyGlyph", "%d", (int)SetMenuItemKeyGlyph(m, 1, 0x17));
        out("SetMenuItemKeyGlyph.bad", "%d", (int)SetMenuItemKeyGlyph(m, 9, 0x17));
        {
            typedef OSErr (*GetMenuItemKeyGlyph_t)(MenuHandle, SInt16, SInt16 *);
            typedef OSErr (*GetMenuItemModifiers_t)(MenuHandle, SInt16, UInt8 *);
            typedef OSErr (*SetMenuItemModifiers_t)(MenuHandle, SInt16, UInt8);
            GetMenuItemKeyGlyph_t g = (GetMenuItemKeyGlyph_t)probe_bind("AppearanceLib", "GetMenuItemKeyGlyph");
            SetMenuItemModifiers_t sm = (SetMenuItemModifiers_t)probe_bind("AppearanceLib", "SetMenuItemModifiers");
            GetMenuItemModifiers_t gm = (GetMenuItemModifiers_t)probe_bind("AppearanceLib", "GetMenuItemModifiers");
            if(g)
            {
                SInt16 v;
                SET(v);
                out("GetMenuItemKeyGlyph", "%d %d", (int)g(m, 1, &v), v);
            }
            if(sm && gm)
            {
                UInt8 v;
                out("SetMenuItemModifiers", "%d", (int)sm(m, 1, 3));
                SET(v);
                out("GetMenuItemModifiers", "%d %d", (int)gm(m, 1, &v), v);
            }
        }
        DisposeMenu(m);
    }
    /* 8.5's submenu ID and text encoding of an item (Mercutio's MDEF calls
       them once Gestalt 'menu' says 8.5) */
    if(SECTION("item85"))
    {
        typedef OSErr (*GetHier_t)(MenuHandle, SInt16, SInt16 *);
        typedef OSErr (*SetHier_t)(MenuHandle, SInt16, SInt16);
        typedef OSErr (*GetEnc_t)(MenuHandle, SInt16, UInt32 *);
        typedef OSErr (*SetEnc_t)(MenuHandle, SInt16, UInt32);
        GetHier_t gh = (GetHier_t)probe_bind("AppearanceLib", "GetMenuItemHierarchicalID");
        SetHier_t sh = (SetHier_t)probe_bind("AppearanceLib", "SetMenuItemHierarchicalID");
        GetEnc_t ge = (GetEnc_t)probe_bind("AppearanceLib", "GetMenuItemTextEncoding");
        SetEnc_t se = (SetEnc_t)probe_bind("AppearanceLib", "SetMenuItemTextEncoding");
        m = NewMenu(209, "\pItem85");
        AppendMenu(m, "\pa;b;c");
        if(gh && sh)
        {
            SInt16 v;
            CharParameter cmd, mark;
            SET(v);
            out("GetMenuItemHierarchicalID.plain", "%d %d", (int)gh(m, 1, &v), v);
            SetItemCmd(m, 2, 0x1B);   /* hMenuCmd */
            SetItemMark(m, 2, 200);
            SET(v);
            out("GetMenuItemHierarchicalID.cmd", "%d %d", (int)gh(m, 2, &v), v);
            out("SetMenuItemHierarchicalID", "%d", (int)sh(m, 3, 201));
            GetItemCmd(m, 3, &cmd);
            GetItemMark(m, 3, &mark);
            SET(v);
            out("GetMenuItemHierarchicalID.set", "%d %d cmd %d mark %d", (int)gh(m, 3, &v), v, (int)cmd, (int)mark);
            out("SetMenuItemHierarchicalID.0", "%d", (int)sh(m, 3, 0));
            GetItemCmd(m, 3, &cmd);
            GetItemMark(m, 3, &mark);
            SET(v);
            out("GetMenuItemHierarchicalID.cleared", "%d %d cmd %d mark %d", (int)gh(m, 3, &v), v, (int)cmd, (int)mark);
            SET(v);
            out("GetMenuItemHierarchicalID.bad", "%d %d", (int)gh(m, 9, &v), v);
            out("SetMenuItemHierarchicalID.bad", "%d", (int)sh(m, 9, 202));
        }
        if(ge && se)
        {
            UInt32 v;
            SET(v);
            out("GetMenuItemTextEncoding.plain", "%d %ld", (int)ge(m, 1, &v), (long)v);
            out("SetMenuItemTextEncoding", "%d", (int)se(m, 1, 1));
            SET(v);
            out("GetMenuItemTextEncoding.set", "%d %ld", (int)ge(m, 1, &v), (long)v);
            SET(v);
            out("GetMenuItemTextEncoding.bad", "%d %ld", (int)ge(m, 9, &v), (long)v);
        }
        DisposeMenu(m);
    }
    if(SECTION("icons"))
    {
        Handle icon = NewHandleClear(128);
        UInt8 type;
        Handle h;
        m = NewMenu(205, "\pIcons");
        AppendMenu(m, "\pa;b");
        SetItemIcon(m, 1, 5);
        out("SetMenuItemIconHandle", "%d", (int)SetMenuItemIconHandle(m, 2, kMenuIconType, icon));
        SET(type); SET(h);
        out("GetMenuItemIconHandle.2", "%d type %d %s", (int)GetMenuItemIconHandle(m, 2, &type, &h), type,
            h == icon ? "same" : h ? "other" : "nil");
        SET(type); SET(h);
        out("GetMenuItemIconHandle.1", "%d type %d %s", (int)GetMenuItemIconHandle(m, 1, &type, &h), type,
            h == (Handle)0xA5A5A5A5 ? "unset" : h ? "set" : "nil");
        SET(type); SET(h);
        out("GetMenuItemIconHandle.bad", "%d", (int)GetMenuItemIconHandle(m, 7, &type, &h));
        dump_items("icons", m);
        DisposeMenu(m);
    }
    if(SECTION("hier"))
    {
        MenuHandle sub;
        m = NewMenu(206, "\pParent");
        sub = NewMenu(207, "\pSub");
        AppendMenu(m, "\pHas sub;Plain");
        AppendMenu(sub, "\pChild");
        InsertMenu(sub, -1);   /* hierMenu */
        SetItemCmd(m, 1, 0x1B);   /* hMenuCmd */
        SetItemMark(m, 1, 207);
        dump_items("hier", m);
        out("GetMenuHandle.207", "%s", GetMenuHandle(207) == sub ? "sub" : "other");
        {
            typedef OSErr (*SetSubmenuParentChoosable_t)(MenuHandle, Boolean);
            SetSubmenuParentChoosable_t f = (SetSubmenuParentChoosable_t)probe_bind("MenusLib", "SetSubmenuParentChoosable");
            if(f)
                out("SetSubmenuParentChoosable", "%d", (int)f(m, true));
        }
        DeleteMenu(207);
        out("GetMenuHandle.deleted", "%s", GetMenuHandle(207) ? "found" : "nil");
        DisposeMenu(sub);
        DisposeMenu(m);
    }
    if(SECTION("clock"))
    {
        typedef Boolean (*B_t)(void);
        typedef OSStatus (*S_t)(void);
        typedef OSStatus (*Cfg_t)(void *);
        void *f;
        unsigned char cfg[22];
        if((f = probe_bind("MenusLib", "IsMenuBarClockEnabled")))
            out("IsMenuBarClockEnabled", "%d", ((B_t)f)());
        if((f = probe_bind("MenusLib", "GetMenuBarClockConfig")))
        {
            memset(cfg, SENTINEL, sizeof cfg);
            cfg[0] = cfg[1] = 0;
            out("GetMenuBarClockConfig", "%d", (int)((Cfg_t)f)(cfg));
            out_hex("GetMenuBarClockConfig.data", cfg, sizeof cfg);
            cfg[0] = 0; cfg[1] = 1;
            out("GetMenuBarClockConfig.v1", "%d", (int)((Cfg_t)f)(cfg));
        }
        probe_bind("MenusLib", "EnableMenuBarClock");
        probe_bind("MenusLib", "DisableMenuBarClock");
        probe_bind("MenusLib", "RefreshMenuBarClock");
        probe_bind("MenusLib", "InvalidateAllMenuSizes");
        probe_bind("MenusLib", "InvalidateAllCachedMenuPixels");
        probe_bind("WindowsLib", "RecalculateAllWindowTitleWidths");
    }
    if(SECTION("bar"))
    {
        /* where the menu bar lays its titles out: an Apple menu, File, Edit
           (the MenuList's lastRight and each menu's left) */
        static const unsigned char *const titles[] = { "\p\024", "\pFile", "\pEdit" };
        MenuHandle bar[3];
        Handle list;
        short n, j;
        for(i = 0; i < 3; i++)
        {
            bar[i] = NewMenu(300 + i, titles[i]);
            AppendMenu(bar[i], "\pItem");
            InsertMenu(bar[i], 0);
        }
        DrawMenuBar();
        list = LMGetMenuList();
        n = *(short *)*list / 6;
        out("lastRight", "%d", ((short *)*list)[1]);
        for(j = 1; j <= n; j++)
        {
            unsigned char *e = (unsigned char *)*list + 6 * j;
            MenuHandle mh = *(MenuHandle *)e;
            short left = *(short *)(e + 4);
            /* the system menus, right-aligned, depend on the screen */
            if((**mh).menuID < 0)
                continue;
            sprintf(k, "left.%d", (int)(**mh).menuID);
            out(k, "%d", left);
        }
        for(i = 0; i < 3; i++)
        {
            DeleteMenu(300 + i);
            DisposeMenu(bar[i]);
        }
        DrawMenuBar();
    }
    probe_close();
    return 0;
}
