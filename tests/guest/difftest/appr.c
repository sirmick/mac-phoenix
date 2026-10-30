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
        for(i = 0; i <= 10; i++)
        {
            Rect cont;
            SetRect(&cont, 10, 25, 110, 55);
            begin(depths[d]);
            DrawThemeWindowFrame(i, &cont, kThemeStateActive, NULL,
                                 kThemeWindowHasCloseBox | kThemeWindowHasFullZoom | kThemeWindowHasCollapseBox,
                                 NULL, 0);
            sprintf(k, "windowframe.%d", i);
            end(k);
        }
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
    colours();
    fonts();
    metrics();
    shapes();
    theme();
    drawing();
    if(SECTION("unregister"))
    {
        RegisterAppearanceClient();   /* sections stand alone (a resumed run) */
        out("Unregister", "%d", (int)UnregisterAppearanceClient());
        out("Unregister.again", "%d", (int)UnregisterAppearanceClient());
    }
    probe_close();
    return 0;
}
