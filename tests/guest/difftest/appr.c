/* appr: the Appearance Manager (AppearanceLib), differential
   (tools/difftest/run.py appr): Gestalt, clients, colours, fonts, metrics,
   styles, the shapes of windows, buttons, tracks and tabs, the theme
   collection, the private calls the 8.6 Appearance control panel makes,
   and every drawing primitive drawn offscreen at 8 and 32 bits.
   Successor of tests/guest/appearance/ApprProbe. */
#include "probe.h"
#include <Gestalt.h>
#include <Appearance.h>
#include <Processes.h>
#include <Fonts.h>
#include <Collections.h>
#include <Controls.h>
#include <Menus.h>

#define SET(x) memset(&(x), SENTINEL, sizeof(x))

static RgnHandle r;
static const short depths[] = { 32, 8, 1 };

static void colours(void)
{
    int d, i;
    char k[64];
    OSStatus err;
    if(SECTION("color"))
    {
        for(d = 0; d < 3; d++)
        {
            sprintf(k, "IsThemeInColor.%d", depths[d]);
            out(k, "color %d mono %d", IsThemeInColor(depths[d], true), IsThemeInColor(depths[d], false));
        }
        for(d = 0; d < 3; d++)
            for(i = -3; i <= 60; i++)
            {
                RGBColor c;
                SET(c);
                err = GetThemeBrushAsColor(i, depths[d], true, &c);
                sprintf(k, "brush.%d.%d", i, depths[d]);
                out(k, "err %d rgb %04x %04x %04x", (int)err, c.red, c.green, c.blue);
            }
        for(d = 0; d < 3; d++)
            for(i = -3; i <= 55; i++)
            {
                RGBColor c;
                SET(c);
                err = GetThemeTextColor(i, depths[d], true, &c);
                sprintf(k, "text.%d.%d", i, depths[d]);
                out(k, "err %d rgb %04x %04x %04x", (int)err, c.red, c.green, c.blue);
            }
        {
            CTabHandle ct;
            SET(ct);
            err = GetThemeAccentColors(&ct);
            out("accent.err", "%d", (int)err);
            if(!err)
                out_handle("accent", (Handle)ct);
        }
    }
}

static void fonts(void)
{
    int i;
    char k[64];
    if(!SECTION("font"))
        return;
    for(i = 0; i <= 210; i++)
    {
        Str255 name;
        SInt16 size;
        Style style;
        OSStatus err;
        if((i > 8 && i < 100) || (i > 108 && i != 200))
            continue;
        SET(name); SET(size); SET(style);
        name[0] = 0;
        err = GetThemeFont(i, smSystemScript, name, &size, &style);
        sprintf(k, "%d", i);
        out(k, "err %d size %d style %d", (int)err, size, style);
        sprintf(k, "%d.name", i);
        if(!err) out_str(k, name);
    }
    {
        typedef OSStatus (*GetSmallSystemFont_t)(ScriptCode, StringPtr);
        GetSmallSystemFont_t f = (GetSmallSystemFont_t)probe_bind("AppearanceLib", "GetSmallSystemFont");
        if(f)
        {
            Str255 name;
            OSStatus err;
            SET(name);
            err = f(-1, name);
            out("GetSmallSystemFont.err", "%d", (int)err);
            out_str("GetSmallSystemFont.name", name);
        }
    }
}

static void metrics(void)
{
    SInt16 h, w;
    OSStatus err;
    int i;
    char k[64];
    if(!SECTION("metric"))
        return;
    SET(h); err = GetThemeMenuBarHeight(&h); out("MenuBarHeight", "%d %d", (int)err, h);
    SET(h); err = GetThemeMenuSeparatorHeight(&h); out("MenuSeparatorHeight", "%d %d", (int)err, h);
    for(i = 0; i <= 3; i++)
    {
        SET(h); SET(w);
        err = GetThemeMenuItemExtra(i, &h, &w);
        sprintf(k, "MenuItemExtra.%d", i);
        out(k, "err %d h %d w %d", (int)err, h, w);
    }
    SET(w); err = GetThemeMenuTitleExtra(&w, false); out("MenuTitleExtra", "%d %d", (int)err, w);
    SET(w); err = GetThemeMenuTitleExtra(&w, true); out("MenuTitleExtra.squished", "%d %d", (int)err, w);
    {
        ThemeScrollBarThumbStyle ts;
        ThemeScrollBarArrowStyle as;
        ThemeCheckBoxStyle cs;
        SET(ts); SET(as); SET(cs);
        out("ThumbStyle", "%d %d", (int)GetThemeScrollBarThumbStyle(&ts), ts);
        out("ArrowStyle", "%d %d", (int)GetThemeScrollBarArrowStyle(&as), as);
        out("CheckBoxStyle", "%d %d", (int)GetThemeCheckBoxStyle(&cs), cs);
    }
    {
        /* Appearance 1.1's metrics (8.5) */
        typedef OSStatus (*GetThemeMetric_t)(UInt32, SInt32 *);
        GetThemeMetric_t f = (GetThemeMetric_t)probe_bind("AppearanceLib", "GetThemeMetric");
        if(f)
            for(i = 0; i <= 60; i++)
            {
                SInt32 v;
                SET(v);
                err = f(i, &v);
                sprintf(k, "ThemeMetric.%d", i);
                out(k, "err %d %ld", (int)err, (long)v);
            }
    }
}

static void shapes(void)
{
    Rect cont, o;
    OSStatus err;
    int i, c;
    char k[96];
    if(SECTION("winregion"))
    {
        static const short codes[] = { 0, 1, 2, 3, 5, 6, 7, 8, 32, 33 };
        SetRect(&cont, 100, 100, 400, 300);
        for(i = 0; i <= 10; i++)
            for(c = 0; c < (int)(sizeof codes / sizeof codes[0]); c++)
            {
                ThemeWindowMetrics wm = { sizeof(ThemeWindowMetrics), 0, 0, 0, 0, 0 };
                SetEmptyRgn(r);
                err = GetThemeWindowRegion(i, &cont, kThemeStateActive, &wm,
                                           kThemeWindowHasGrow | kThemeWindowHasCloseBox | kThemeWindowHasFullZoom
                                           | kThemeWindowHasCollapseBox | kThemeWindowHasTitleText,
                                           codes[c], r);
                sprintf(k, "%d.%d", i, codes[c]);
                out(k, "err %d", (int)err);
                sprintf(k, "%d.%d.rgn", i, codes[c]);
                out_rgn(k, r);
            }
    }
    if(SECTION("button"))
        for(i = 0; i <= 20; i++)
        {
            Rect b;
            ThemeButtonDrawInfo info = { kThemeStateActive, kThemeButtonOff, kThemeAdornmentNone };
            SetRect(&b, 20, 20, 100, 40);
            SetEmptyRgn(r);
            err = GetThemeButtonRegion(&b, i, &info, r);
            sprintf(k, "%d.region", i); out(k, "err %d", (int)err);
            sprintf(k, "%d.region.rgn", i); out_rgn(k, r);
            SET(o); err = GetThemeButtonContentBounds(&b, i, &info, &o);
            sprintf(k, "%d.content", i); out(k, "err %d %d,%d,%d,%d", (int)err, o.top, o.left, o.bottom, o.right);
            SET(o); err = GetThemeButtonBackgroundBounds(&b, i, &info, &o);
            sprintf(k, "%d.background", i); out(k, "err %d %d,%d,%d,%d", (int)err, o.top, o.left, o.bottom, o.right);
            info.adornment = kThemeAdornmentDefault;
            SetEmptyRgn(r);
            err = GetThemeButtonRegion(&b, i, &info, r);
            sprintf(k, "%d.default", i); out(k, "err %d", (int)err);
            sprintf(k, "%d.default.rgn", i); out_rgn(k, r);
        }
    if(SECTION("track"))
    {
        int t;
        for(t = 0; t < 3; t++)
        {
            ThemeTrackDrawInfo d;
            memset(&d, 0, sizeof d);
            d.kind = t == 2 ? kThemeSlider : kThemeScrollBar;
            if(t == 1) SetRect(&d.bounds, 0, 0, 200, 16); else if(t == 0) SetRect(&d.bounds, 0, 0, 16, 200); else SetRect(&d.bounds, 0, 0, 150, 18);
            d.min = 0; d.max = 100; d.value = 25;
            d.attributes = t ? kThemeTrackHorizontal : 0;
            d.enableState = kThemeTrackActive;
            d.trackInfo.scrollbar.viewsize = 50;
            if(t == 2) d.trackInfo.slider.thumbDir = kThemeThumbDownward;
            SET(o); err = GetThemeTrackBounds(&d, &o);
            sprintf(k, "%d.bounds", t); out(k, "err %d %d,%d,%d,%d", (int)err, o.top, o.left, o.bottom, o.right);
            SetEmptyRgn(r); err = GetThemeTrackThumbRgn(&d, r);
            sprintf(k, "%d.thumb", t); out(k, "err %d", (int)err);
            sprintf(k, "%d.thumb.rgn", t); out_rgn(k, r);
            SET(o); err = GetThemeTrackDragRect(&d, &o);
            sprintf(k, "%d.drag", t); out(k, "err %d %d,%d,%d,%d", (int)err, o.top, o.left, o.bottom, o.right);
            {
                SInt32 lv;
                SET(lv); err = GetThemeTrackLiveValue(&d, 40, &lv);
                sprintf(k, "%d.live40", t); out(k, "err %d %ld", (int)err, (long)lv);
            }
        }
    }
    if(SECTION("misc"))
    {
        Rect tb;
        Point p = { 100, 100 };
        SetRect(&tb, 10, 10, 30, 90);
        for(i = 0; i < 4; i++)
        {
            SetEmptyRgn(r);
            err = GetThemeTabRegion(&tb, i, kThemeTabNorth, r);
            sprintf(k, "tab.%d", i); out(k, "err %d", (int)err);
            sprintf(k, "tab.%d.rgn", i); out_rgn(k, r);
        }
        for(i = 0; i < 4; i++)
        {
            SET(o); err = GetThemeStandaloneGrowBoxBounds(p, i, false, &o);
            sprintf(k, "growbox.%d", i); out(k, "err %d %d,%d,%d,%d", (int)err, o.top, o.left, o.bottom, o.right);
            SET(o); err = GetThemeStandaloneGrowBoxBounds(p, i, true, &o);
            sprintf(k, "growbox.small.%d", i); out(k, "err %d %d,%d,%d,%d", (int)err, o.top, o.left, o.bottom, o.right);
        }
        SetRect(&tb, 20, 50, 200, 250);
        for(i = 0; i < 4; i++)
        {
            SetEmptyRgn(r);
            err = GetThemeMenuBackgroundRegion(&tb, i, r);
            sprintf(k, "menubg.%d", i); out(k, "err %d", (int)err);
            sprintf(k, "menubg.%d.rgn", i); out_rgn(k, r);
        }
    }
}

/* The theme as a collection, and the private theme and desktop calls. */
static void theme(void)
{
    OSStatus err;
    if(SECTION("theme"))
    {
        Collection c = NewCollection();
        SInt32 n, i;
        err = GetTheme(c);
        out("GetTheme.err", "%d", (int)err);
        n = CountCollectionItems(c);
        out("GetTheme.count", "%ld", (long)n);
        for(i = 1; i <= n; i++)
        {
            CollectionTag tag;
            SInt32 id, size;
            char k[64];
            Handle h = NewHandle(0);
            if(GetIndexedCollectionItemInfo(c, i, &tag, &id, &size, NULL) == noErr
               && GetCollectionItemHdl(c, tag, id, h) == noErr)
            {
                sprintf(k, "item.%.4s.%ld", (char *)&tag, (long)id);
                (void)size;
                out_handle(k, h);
            }
            DisposeHandle(h);
        }
        DisposeCollection(c);
    }
    if(SECTION("private"))
    {
        typedef OSStatus (*GetThemeVariantList_t)(ConstStringPtr, Handle *);
        typedef Boolean (*Bool_t)(void);
        typedef OSStatus (*GetThemeSoundMask_t)(UInt32 *);
        typedef OSStatus (*GetDesktopPixPat_t)(PixPatHandle *, StringPtr);
        typedef OSStatus (*GetDesktopPattern_t)(Handle *, StringPtr);
        typedef OSStatus (*GetDesktopPictureForDevice_t)(GDHandle, FSSpec *, UInt32 *);
        typedef UInt32 (*U32_t)(void);
        void *f;
        if((f = probe_bind("AppearanceLib", "GetThemeVariantList")))
        {
            Handle h;
            SET(h);
            err = ((GetThemeVariantList_t)f)("\pnonexistent theme", &h);
            out("GetThemeVariantList.missing", "err %d out %08lx", (int)err, (long)h);
        }
        if((f = probe_bind("AppearanceLib", "AreThemeSoundsEnabled")))
            out("AreThemeSoundsEnabled", "%d", ((Bool_t)f)());
        if((f = probe_bind("AppearanceLib", "IsThemeCreditDialogAvailable")))
            out("IsThemeCreditDialogAvailable", "%d", ((Bool_t)f)());
        if((f = probe_bind("AppearanceLib", "GetThemeSoundMask")))
        {
            UInt32 m[2];
            SET(m);
            err = ((GetThemeSoundMask_t)f)(m);
            out("GetThemeSoundMask", "err %d %08lx %08lx", (int)err, (long)m[0], (long)m[1]);
        }
        if((f = probe_bind("WindowsLib", "GetDesktopPixPat")))
        {
            PixPatHandle pp;
            Str255 name;
            SET(pp); SET(name);
            err = ((GetDesktopPixPat_t)f)(&pp, name);
            out("GetDesktopPixPat.err", "%d", (int)err);
            out_str("GetDesktopPixPat.name", name);
            if(!err && pp != (PixPatHandle)0xA5A5A5A5)
            {
                out("GetDesktopPixPat.type", "%d", (**pp).patType);
                DisposePixPat(pp);
            }
        }
        if((f = probe_bind("WindowsLib", "GetDesktopPattern")))
        {
            Handle h;
            Str255 name;
            SET(h); SET(name);
            err = ((GetDesktopPattern_t)f)(&h, name);
            out("GetDesktopPattern.err", "%d", (int)err);
            out_str("GetDesktopPattern.name", name);
            if(!err)
            {
                out_handle("GetDesktopPattern", h);
                if(h != (Handle)0xA5A5A5A5) DisposeHandle(h);
            }
        }
        if((f = probe_bind("WindowsLib", "GetDesktopPictureForDevice")))
        {
            FSSpec spec;
            UInt32 placement;
            SET(spec); SET(placement);
            err = ((GetDesktopPictureForDevice_t)f)(GetMainDevice(), &spec, &placement);
            out("GetDesktopPictureForDevice.err", "%d", (int)err);
        }
        if((f = probe_bind("WindowsLib", "GetWindowDoubleClickAction")))
            out("GetWindowDoubleClickAction", "%lu", (unsigned long)((U32_t)f)());
    }
}

/* Each primitive drawn on white in a 120x60 GWorld. */
static GWorldPtr gw;
static CGrafPtr save_port;
static GDHandle save_dev;

static void begin(int depth)
{
    Rect b;
    SetRect(&b, 0, 0, 120, 60);
    NewGWorld(&gw, depth, &b, NULL, NULL, 0);
    GetGWorld(&save_port, &save_dev);
    SetGWorld(gw, NULL);
    LockPixels(GetGWorldPixMap(gw));
    ForeColor(blackColor);
    BackColor(whiteColor);
    EraseRect(&b);
}

static void end(const char *key)
{
    SetGWorld(save_port, save_dev);
    probe_pixels(key, gw);
    DisposeGWorld(gw);
}

/* QuickDraw's arithmetic transfer modes (blend .. adMin), PaintRect with
   white and with a mid red over the platinum dialog grey: the 8.6 control
   panels wash their titles with white in blend mode */
static void modes(void)
{
    static const RGBColor grey = { 0xDDDD, 0xDDDD, 0xDDDD }, white = { 0xFFFF, 0xFFFF, 0xFFFF },
                          red = { 0xCCCC, 0x3333, 0x3333 }, op = { 0x4000, 0x4000, 0x4000 };
    Rect b, r;
    char k[96];
    int d, m, c;
    if(!SECTION("modes"))
        return;
    for(d = 0; d < 2; d++)
        for(m = blend; m <= adMin + 1; m++)
            for(c = 0; c < 2; c++)
            {
                begin(depths[d]);
                SetRect(&b, 0, 0, 120, 60);
                RGBForeColor(&grey);
                PaintRect(&b);
                SetRect(&r, 10, 10, 110, 50);
                if(m > adMin)
                    OpColor(&op);
                PenMode(m > adMin ? blend : m);
                RGBForeColor(c ? &red : &white);
                PaintRect(&r);
                PenNormal();
                sprintf(k, "%d.%s.%s", depths[d], m > adMin ? "blend-op" : (const char *[]){ "blend", "addPin", "addOver", "subPin", "transparent", "addMax", "subOver", "adMin" }[m - blend], c ? "red" : "white");
                end(k);
            }
    /* the same wash on the screen through a basic GrafPort and a colour
       one (OpenPort/OpenCPort: the whole screen visible, no window
       layering), the pixels copied out */
    for(c = 0; c < 2; c++)
    {
        GrafPort port;
        CGrafPort cport;
        GrafPtr p = c ? (GrafPtr)&cport : &port, save;
        GetPort(&save);
        if(c)
            OpenCPort(&cport);
        else
            OpenPort(&port);
        SetPort(p);
        SetRect(&b, 480, 380, 600, 440);
        RGBForeColor(&grey);
        PaintRect(&b);
        SetRect(&r, 490, 390, 590, 430);
        PenMode(blend);
        RGBForeColor(&white);
        PaintRect(&r);
        PenNormal();
        ForeColor(blackColor);
        SetPort(save);
        if(c)
            CloseCPort(&cport);
        else
            ClosePort(&port);
        /* read through a colour port's pixmap */
        OpenCPort(&cport);
        begin(32);
        SetRect(&r, 0, 0, 120, 60);
        CopyBits((BitMap *)*cport.portPixMap, (BitMap *)*GetGWorldPixMap(gw), &b, &r, srcCopy, NULL);
        sprintf(k, "win.%s", c ? "color" : "basic");
        end(k);
        /* and through the screen as a BitMap (qd.screenBits), as older
           programs copy from it */
        if(c)
        {
            begin(32);
            CopyBits(&qd.screenBits, (BitMap *)*GetGWorldPixMap(gw), &b, &r, srcCopy, NULL);
            end("screenbits");
        }
        CloseCPort(&cport);
        SetPort(save);
    }
}

/* how the 8.6 control panels (General Controls) draw a control's title:
   theme background and text colour, a white wash in blend mode over the
   title's box, the state restored, the string in srcOr */
static void title(void)
{
    static const RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF }, grey = { 0xDDDD, 0xDDDD, 0xDDDD };
    Rect b, r;
    char k[32];
    int d, f;
    if(!SECTION("title"))
        return;
    for(d = 0; d < 2; d++)
        for(f = 0; f < 2; f++)
        {
            ThemeDrawingState st;
            begin(depths[d]);
            SetRect(&b, 0, 0, 120, 60);
            RGBForeColor(&grey);
            PaintRect(&b);
            ForeColor(blackColor);
            TextFont(f ? 0 : 3);
            TextSize(f ? 0 : 9);
            SetThemeBackground(16, depths[d], true);
            SetThemeTextColor(21, depths[d], true);
            GetThemeDrawingState(&st);
            SetRect(&r, 8, 20, 112, 33);
            PenMode(blend);
            RGBForeColor(&white);
            PaintRect(&r);
            SetThemeDrawingState(st, true);
            TextMode(srcOr);
            MoveTo(10, 30);
            DrawString("\pShow Desktop when");
            sprintf(k, "%d.%s", depths[d], f ? "system" : "geneva9");
            end(k);
        }
    /* the same on the screen, through a colour port (OpenCPort) */
    {
        CGrafPort cport;
        GrafPtr save;
        ThemeDrawingState st;
        GetPort(&save);
        OpenCPort(&cport);
        SetPort((GrafPtr)&cport);
        SetRect(&b, 480, 380, 600, 440);
        RGBForeColor(&grey);
        PaintRect(&b);
        ForeColor(blackColor);
        TextFont(3);
        TextSize(9);
        SetThemeBackground(16, 8, true);
        SetThemeTextColor(21, 8, true);
        GetThemeDrawingState(&st);
        SetRect(&r, 488, 400, 592, 413);
        PenMode(blend);
        RGBForeColor(&white);
        PaintRect(&r);
        SetThemeDrawingState(st, true);
        TextMode(srcOr);
        MoveTo(490, 410);
        DrawString("\pShow Desktop when");
        begin(32);
        SetRect(&r, 0, 0, 120, 60);
        CopyBits((BitMap *)*cport.portPixMap, (BitMap *)*GetGWorldPixMap(gw), &b, &r, srcCopy, NULL);
        end("screen");
        SetPort(save);
        CloseCPort(&cport);
    }
}

static void drawing(void)
{
    static const ThemeDrawState states[] = { kThemeStateActive, kThemeStateInactive, kThemeStatePressed };
    static const char *sn[] = { "active", "inactive", "pressed" };
    Rect b;
    char k[96];
    int d, s, i;
    for(d = 0; d < 2; d++)
    {
        char sec[32];
        sprintf(sec, "draw%d", depths[d]);
        if(!SECTION(sec))
            continue;
        for(s = 0; s < 3; s++)
        {
            for(i = 0; i <= 20; i++)
            {
                ThemeButtonDrawInfo info = { states[s], kThemeButtonOff, kThemeAdornmentNone };
                begin(depths[d]);
                SetRect(&b, 10, 10, 100, 34);
                DrawThemeButton(&b, i, &info, NULL, NULL, NULL, 0);
                sprintf(k, "button.%d.%s", i, sn[s]);
                end(k);
            }
            SetRect(&b, 8, 8, 112, 52);
#define PRIM(NAME, CALL) begin(depths[d]); CALL; sprintf(k, "%s.%s", NAME, sn[s]); end(k);
            PRIM("primarygroup", DrawThemePrimaryGroup(&b, states[s]))
            PRIM("secondarygroup", DrawThemeSecondaryGroup(&b, states[s]))
            PRIM("edittext", DrawThemeEditTextFrame(&b, states[s]))
            PRIM("listbox", DrawThemeListBoxFrame(&b, states[s]))
            PRIM("well", DrawThemeGenericWell(&b, states[s], false))
            PRIM("placard", DrawThemePlacard(&b, states[s]))
            PRIM("tabpane", DrawThemeTabPane(&b, states[s]))
            PRIM("windowheader", DrawThemeWindowHeader(&b, states[s]))
            PRIM("tickmark", DrawThemeTickMark(&b, states[s]))
            {
                Rect sep;
                SetRect(&sep, 30, 10, 32, 50);
                PRIM("separator", DrawThemeSeparator(&sep, states[s]))
            }
            for(i = 0; i < 4; i++)
            {
                Rect tb;
                SetRect(&tb, 10, 10, 30, 100);
                begin(depths[d]);
                DrawThemeTab(&tb, i, kThemeTabNorth, NULL, 0);
                sprintf(k, "tab.%d.%s", i, sn[s]);
                end(k);
            }
            for(i = 0; i < 2; i++)
            {
                Rect pa;
                SetRect(&pa, 20, 20, 40, 40);
                begin(depths[d]);
                DrawThemePopupArrow(&pa, kThemeArrowDown, i, states[s], NULL, 0);
                sprintf(k, "popuparrow.%d.%s", i, sn[s]);
                end(k);
            }
            {
                Point p = { 30, 30 };
                PRIM("growbox", DrawThemeStandaloneGrowBox(p, kThemeGrowRight | kThemeGrowDown, false, states[s]))
            }
        }
        SetRect(&b, 8, 8, 112, 52);
        begin(depths[d]); DrawThemeFocusRect(&b, true); end("focusrect");
        begin(depths[d]); DrawThemeMenuBarBackground(&b, kThemeMenuBarNormal, 0); end("menubar");
        for(i = 0; i < 4; i++)
        {
            begin(depths[d]); DrawThemeMenuBackground(&b, i); sprintf(k, "menubg.%d", i); end(k);
        }
        begin(depths[d]); DrawThemeMenuSeparator(&b); end("menuseparator");
        {
            /* a 20-pixel bar as the MBDF has it: selected, and with a
               title in each state; a menu's item in each state */
            static const ThemeMenuState ms[] = { kThemeMenuActive, kThemeMenuSelected, kThemeMenuDisabled };
            static const char *mn[] = { "active", "selected", "disabled" };
            Rect bar, t, m, it;
            SetRect(&bar, 8, 8, 112, 28);
            begin(depths[d]); DrawThemeMenuBarBackground(&bar, kThemeMenuBarSelected, 0); end("menubar.selected");
            for(i = 0; i < 3; i++)
            {
                SetRect(&t, 30, 8, 70, 28);
                begin(depths[d]);
                DrawThemeMenuBarBackground(&bar, kThemeMenuBarNormal, 0);
                DrawThemeMenuTitle(&bar, &t, ms[i], 0, NULL, 0);
                sprintf(k, "menutitle.%s", mn[i]);
                end(k);
                SetRect(&m, 8, 8, 112, 52);
                SetRect(&it, 9, 20, 111, 38);
                begin(depths[d]);
                DrawThemeMenuBackground(&m, kThemeMenuTypePullDown);
                DrawThemeMenuItem(&m, &it, 8, 52, ms[i], kThemeMenuItemPlain, NULL, 0);
                sprintf(k, "menuitem.%s", mn[i]);
                end(k);
                begin(depths[d]);
                DrawThemeMenuBackground(&m, kThemeMenuTypePullDown);
                DrawThemeMenuItem(&m, &it, 8, 52, ms[i], kThemeMenuItemHierarchical, NULL, 0);
                sprintf(k, "menuitem.hier.%s", mn[i]);
                end(k);
            }
        }
        for(i = 0; i <= 10; i++)
        {
            Rect cont;
            OSStatus err;
            SetRect(&cont, 10, 25, 110, 55);
            begin(depths[d]);
            err = DrawThemeWindowFrame(i, &cont, kThemeStateActive, NULL,
                                       kThemeWindowHasCloseBox | kThemeWindowHasFullZoom | kThemeWindowHasCollapseBox,
                                       NULL, 0);
            sprintf(k, "windowframe.%d", i);
            end(k);
            sprintf(k, "windowframe.%d.err", i);
            out(k, "%d", (int)err);
        }
        /* ... with the metrics (title width and height as GetThemeWindowRegion
           takes them) */
        for(i = 0; i <= 10; i++)
        {
            Rect cont;
            OSStatus err;
            ThemeWindowMetrics wm = { sizeof(ThemeWindowMetrics), 0, 0, 0, 0, 0 };
            SetRect(&cont, 10, 25, 110, 55);
            begin(depths[d]);
            err = DrawThemeWindowFrame(i, &cont, kThemeStateActive, &wm,
                                       kThemeWindowHasCloseBox | kThemeWindowHasFullZoom | kThemeWindowHasCollapseBox,
                                       NULL, 0);
            sprintf(k, "windowframe.metrics.%d", i);
            end(k);
            sprintf(k, "windowframe.metrics.%d.err", i);
            out(k, "%d", (int)err);
        }
    }
}

/* Window frames as programs get them: windows of the classic and the
   Appearance definitions (procIDs) on the screen, active and then
   unhilited, the frame copied out. A plain dialog box behind them hides
   the desktop (its pattern isn't what is compared). Active, the close,
   zoom and collapse boxes pressed too: the window's definition called
   with wDraw and the part, as TrackBox does (and again, and the whole
   frame, to let go); and collapsed (CollapseWindow). */
static void frame_out(const char *key)
{
    static Rect from = { 50, 20, 150, 220 };   /* the frame and some white round it */
    static Rect to = { 0, 0, 100, 200 };
    CGrafPort screen;
    GWorldPtr g;
    GrafPtr save;
    GetPort(&save);
    OpenCPort(&screen);
    NewGWorld(&g, 32, &to, NULL, NULL, 0);
    LockPixels(GetGWorldPixMap(g));
    SetPort((GrafPtr)&screen);
    CopyBits((BitMap *)*screen.portPixMap, (BitMap *)*GetGWorldPixMap(g), &from, &to, srcCopy, NULL);
    SetPort(save);
    CloseCPort(&screen);
    probe_pixels(key, g);
    DisposeGWorld(g);
}

static void wdef_draw(WindowRef w, long part)
{
    Handle def = ((WindowPeek)w)->windowDefProc;
    GrafPtr save;
    CGrafPtr wm;
    RgnHandle clip = NewRgn();
    GetPort(&save);
    GetCWMgrPort(&wm);
    SetPort((GrafPtr)wm);
    GetClip(clip);
    SetClip(GetGrayRgn());
    HLock(def);
    InvokeWindowDefUPP(GetWVariant(w), w, wDraw, part, (WindowDefUPP)*def);
    HUnlock(def);
    SetClip(clip);
    DisposeRgn(clip);
    SetPort(save);
}

static void frames(void)
{
    static const short procs[] = { 0, 1, 2, 3, 4, 5, 8, 16, 1024, 1031, 1040, 1041, 1042, 1043,
                                   1057, 1059, 1073, 1061, 1075 };
    static const short parts[] = { wInGoAway, wInZoomOut, wInCollapseBox };
    Rect back, b;
    WindowRef backing, w;
    char k[48];
    int i, j, active;
    OSStatus err;
    if(!SECTION("frames"))
        return;
    SetRect(&back, 0, 40, 400, 240);
    backing = NewCWindow(NULL, &back, "\p", true, plainDBox, (WindowRef)-1, false, 0);
    SetRect(&b, 40, 80, 200, 130);           /* content: 160 x 50 */
    for(i = 0; i < (int)(sizeof procs / sizeof procs[0]); i++)
    {
        w = NewCWindow(NULL, &b, "\pFrame", true, procs[i], (WindowRef)-1, true, 0);
        if(!w)
        {
            sprintf(k, "%d", procs[i]);
            out(k, "nil");
            continue;
        }
        for(active = 1; active >= 0; active--)
        {
            if(!active)
                HiliteWindow(w, false);
            sprintf(k, "%d.%s", procs[i], active ? "active" : "inactive");
            frame_out(k);
            if(!active)
                continue;
            for(j = 0; j < 3; j++)
            {
                wdef_draw(w, parts[j]);
                sprintf(k, "%d.pressed%d", procs[i], parts[j]);
                frame_out(k);
                wdef_draw(w, parts[j]);
                wdef_draw(w, 0);
            }
            /* collapsed to the title bar (the windows that can) and back */
            err = CollapseWindow(w, true);
            if(err == noErr)
            {
                sprintf(k, "%d.collapsed", procs[i]);
                frame_out(k);
                CollapseWindow(w, false);
            }
            else
            {
                sprintf(k, "%d.collapse", procs[i]);
                out(k, "%d", (int)err);
            }
        }
        DisposeWindow(w);
    }
    DisposeWindow(backing);
}

/* Controls as programs get them: real controls of the classic and the
   Appearance definitions (procIDs) in a window on the screen, active,
   inactive (hilite 255), pressed (the part a click hilites) and at their
   values (max 1 or 2: check boxes, radio buttons, bevels, triangles),
   copied out with 4 pixels round them; no titles (the glyphs
   aren't what is compared). Then DrawThemeTab at a tab's usual width. */
struct ctl
{
    short proc, w, h, min, max, value, part;
    const char *name;
};

static void copy_out(const char *key, const Rect *local)
{
    CGrafPort screen;
    GWorldPtr g;
    GrafPtr save;
    Rect from = *local, to;
    LocalToGlobal((Point *)&from.top);
    LocalToGlobal((Point *)&from.bottom);
    InsetRect(&from, -4, -4);
    SetRect(&to, 0, 0, from.right - from.left, from.bottom - from.top);
    GetPort(&save);
    OpenCPort(&screen);
    NewGWorld(&g, 32, &to, NULL, NULL, 0);
    LockPixels(GetGWorldPixMap(g));
    SetPort((GrafPtr)&screen);
    CopyBits((BitMap *)*screen.portPixMap, (BitMap *)*GetGWorldPixMap(g), &from, &to, srcCopy, NULL);
    SetPort(save);
    CloseCPort(&screen);
    probe_pixels(key, g);
    DisposeGWorld(g);
}

static void controls(void)
{
    static const struct ctl ctls[] = {
        { 0, 60, 20, 0, 0, 0, 10, "push" },
        { 1, 60, 16, 0, 2, 0, 11, "check" },
        { 2, 60, 16, 0, 2, 0, 12, "radio" },
        { 16, 16, 100, 0, 100, 30, 0, "vscroll" },
        { 16, 100, 16, 0, 100, 30, 0, "hscroll" },
        { 1008, 100, 20, 200, 0, 0, 0, "popup" },
        { 368, 60, 20, 0, 0, 0, 10, "push" },
        { 369, 60, 16, 0, 2, 0, 11, "check" },
        { 370, 60, 16, 0, 2, 0, 12, "radio" },
        { 384, 16, 100, 0, 100, 30, 0, "vscroll" },
        { 400, 100, 20, 200, 0, 0, 0, "popup" },
        { 32, 40, 30, 0, 1, 0, 10, "bevel" },
        { 33, 40, 30, 0, 1, 0, 10, "bevel" },
        { 34, 40, 30, 0, 1, 0, 10, "bevel" },
        { 48, 100, 20, 0, 10, 5, 129, "slider" },
        { 64, 12, 12, 0, 1, 0, 0, "triangle" },
        { 80, 100, 14, 0, 100, 50, 0, "progress" },
        { 96, 13, 23, 0, 10, 5, 20, "arrows" },
        { 144, 100, 2, 0, 0, 0, 0, "separator" },
        { 160, 100, 60, 0, 0, 0, 0, "group" },
        { 224, 100, 20, 0, 0, 0, 0, "placard" },
        { 336, 100, 20, 0, 0, 0, 0, "header" },
    };
    static const short scrollParts[] = { 20, 21, 22, 23, 129 };
    Rect back, b, port;
    WindowRef w;
    MenuHandle m;
    char k[48];
    int i, j;
    if(!SECTION("controls"))
        return;
    RegisterAppearanceClient();
    m = NewMenu(200, "\p");
    AppendMenu(m, "\p ");
    InsertMenu(m, -1);
    SetRect(&back, 0, 40, 400, 240);
    w = NewCWindow(NULL, &back, "\p", true, plainDBox, (WindowRef)-1, false, 0);
    SetPort((GrafPtr)w);
    port = ((GrafPtr)w)->portRect;
    BackColor(whiteColor);
    for(i = 0; i < (int)(sizeof ctls / sizeof ctls[0]); i++)
    {
        const struct ctl *c = &ctls[i];
        ControlHandle h;
        SetRect(&b, 20, 20, 20 + c->w, 20 + c->h);
        EraseRect(&port);
        h = NewControl(w, &b, "\p", true, c->value, c->min, c->max, c->proc, 0);
        sprintf(k, "%d.%s", c->proc, c->name);
        if(!h)
        {
            out(k, "nil");
            continue;
        }
        Draw1Control(h);
        sprintf(k, "%d.%s.active", c->proc, c->name);
        copy_out(k, &b);
        HiliteControl(h, 255);
        sprintf(k, "%d.%s.inactive", c->proc, c->name);
        copy_out(k, &b);
        HiliteControl(h, 0);
        if(c->proc == 16 || c->proc == 384)
            for(j = 0; j < (int)(sizeof scrollParts / sizeof scrollParts[0]); j++)
            {
                HiliteControl(h, scrollParts[j]);
                sprintf(k, "%d.%s.pressed%d", c->proc, c->name, scrollParts[j]);
                copy_out(k, &b);
                HiliteControl(h, 0);
            }
        else if(c->part)
        {
            HiliteControl(h, c->part);
            sprintf(k, "%d.%s.pressed", c->proc, c->name);
            copy_out(k, &b);
            HiliteControl(h, 0);
        }
        if(c->max >= 1 && c->max <= 2)
            for(j = 1; j <= c->max; j++)
            {
                SetControlValue(h, j);
                sprintf(k, "%d.%s.value%d", c->proc, c->name, j);
                copy_out(k, &b);
            }
        DisposeControl(h);
    }
    DisposeWindow(w);
    DeleteMenu(200);
    DisposeMenu(m);
    /* tabs 80 wide, every style */
    for(i = 0; i <= 5; i++)
    {
        Rect tb;
        SetRect(&tb, 10, 10, 90, 50);
        begin(32);
        DrawThemeTab(&tb, i, kThemeTabNorth, NULL, 0);
        sprintf(k, "tab80.%d", i);
        end(k);
    }
}

int main(void)
{
    long v;
    OSStatus err;
    ProcessSerialNumber psn;
    probe_open("appr");
    r = NewRgn();
    if(SECTION("gestalt"))
    {
        SET(v); err = Gestalt('appr', &v); out("appr", "%d %08lx", (int)err, v);
        SET(v); err = Gestalt('apvr', &v); out("apvr", "%d %08lx", (int)err, v);
    }
    if(SECTION("client"))
    {
        GetCurrentProcess(&psn);
        out("IsAppearanceClient.before", "%d", IsAppearanceClient(&psn));
        out("Register", "%d", (int)RegisterAppearanceClient());
        out("Register.again", "%d", (int)RegisterAppearanceClient());
        out("IsAppearanceClient.after", "%d", IsAppearanceClient(&psn));
    }
    /* controls first: NewControl's accelerated CDEFs (8.6's own 384/400)
       share Mixed Mode's prepared-fragment cache (mixed_mode.cpp) with
       everything drawing()/frames() exercises afterward; run while that
       cache is still fresh (docs/executor/DiffTest.md). */
    controls();
    colours();
    fonts();
    metrics();
    shapes();
    theme();
    drawing();
    frames();
    modes();
    title();
    if(SECTION("unregister"))
    {
        RegisterAppearanceClient();   /* sections stand alone (a resumed run) */
        out("Unregister", "%d", (int)UnregisterAppearanceClient());
        out("Unregister.again", "%d", (int)UnregisterAppearanceClient());
    }
    probe_close();
    return 0;
}
