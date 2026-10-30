/* ApprProbe: the Appearance Manager's answers on a real Mac (9.0.4 on the
   G3), as reference values for Executor's: Gestalt, clients, brushes, text
   colours, fonts, metrics, menu sizes, styles, and the shapes of windows,
   buttons, tracks and tabs. Host:appr_results.txt. */
#include <Types.h>
#include <Gestalt.h>
#include <Appearance.h>
#include <Processes.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <stdio.h>

static void rgn(const char *what, RgnHandle r)
{
    Rect b = (**r).rgnBBox;
    printf("%s bbox %d,%d,%d,%d size %d\n", what, b.top, b.left, b.bottom, b.right, (int)(**r).rgnSize);
}

int main(void)
{
    long v;
    int i, d;
    OSStatus err;
    static const short depths[] = { 32, 8, 1 };
    ProcessSerialNumber psn;
    RgnHandle r = NewRgn();
    Rect cont, out;

    freopen("Host:appr_results.txt", "w", stdout);
    err = Gestalt('appr', &v); printf("INFO gestalt appr %d %08lx\n", (int)err, v);
    err = Gestalt('apvr', &v); printf("INFO gestalt apvr %d %08lx\n", (int)err, v);

    GetCurrentProcess(&psn);
    printf("INFO IsAppearanceClient before %d\n", IsAppearanceClient(&psn));
    printf("INFO Register %d\n", (int)RegisterAppearanceClient());
    printf("INFO Register again %d\n", (int)RegisterAppearanceClient());
    printf("INFO IsAppearanceClient after %d\n", IsAppearanceClient(&psn));

    for(d = 0; d < 3; d++)
        printf("INFO IsThemeInColor %d color %d mono %d\n", depths[d], IsThemeInColor(depths[d], true), IsThemeInColor(depths[d], false));

    for(d = 0; d < 3; d++)
        for(i = -3; i <= 60; i++)
        {
            RGBColor c = { 1, 2, 3 };
            err = GetThemeBrushAsColor(i, depths[d], true, &c);
            printf("BRUSH %d depth %d err %d rgb %04x %04x %04x\n", i, depths[d], (int)err, c.red, c.green, c.blue);
        }
    for(d = 0; d < 3; d++)
        for(i = -3; i <= 55; i++)
        {
            RGBColor c = { 1, 2, 3 };
            err = GetThemeTextColor(i, depths[d], true, &c);
            printf("TEXT %d depth %d err %d rgb %04x %04x %04x\n", i, depths[d], (int)err, c.red, c.green, c.blue);
        }
    {
        CTabHandle ct = NULL;
        err = GetThemeAccentColors(&ct);
        printf("INFO GetThemeAccentColors %d", (int)err);
        if(!err && ct)
        {
            printf(" n %d:", (**ct).ctSize + 1);
            for(i = 0; i <= (**ct).ctSize; i++)
                printf(" %04x/%04x%04x%04x", (**ct).ctTable[i].value, (**ct).ctTable[i].rgb.red,
                       (**ct).ctTable[i].rgb.green, (**ct).ctTable[i].rgb.blue);
        }
        printf("\n");
    }
    for(i = 0; i <= 210; i++)
    {
        Str255 name;
        SInt16 size = -1;
        Style style = 0xFF;
        if(i > 8 && i < 100)
            continue;
        if(i > 108 && i != 200)
            continue;
        name[0] = 0;
        err = GetThemeFont(i, smSystemScript, name, &size, &style);
        printf("FONT %d err %d name %.*s size %d style %d\n", i, (int)err, name[0], name + 1, size, style);
    }
    {
        SInt16 h = -1, w = -1;
        err = GetThemeMenuBarHeight(&h); printf("INFO MenuBarHeight %d %d\n", (int)err, h);
        err = GetThemeMenuSeparatorHeight(&h); printf("INFO MenuSeparatorHeight %d %d\n", (int)err, h);
        for(i = 0; i <= 3; i++)
        {
            h = w = -1;
            err = GetThemeMenuItemExtra(i, &h, &w);
            printf("INFO MenuItemExtra %d err %d h %d w %d\n", i, (int)err, h, w);
        }
        err = GetThemeMenuTitleExtra(&w, false); printf("INFO MenuTitleExtra %d %d\n", (int)err, w);
        err = GetThemeMenuTitleExtra(&w, true); printf("INFO MenuTitleExtra squished %d %d\n", (int)err, w);
    }
    {
        ThemeScrollBarThumbStyle ts = 99;
        ThemeScrollBarArrowStyle as = 99;
        ThemeCheckBoxStyle cs = 99;
        printf("INFO ThumbStyle %d %d\n", (int)GetThemeScrollBarThumbStyle(&ts), ts);
        printf("INFO ArrowStyle %d %d\n", (int)GetThemeScrollBarArrowStyle(&as), as);
        printf("INFO CheckBoxStyle %d %d\n", (int)GetThemeCheckBoxStyle(&cs), cs);
    }

    /* windows: content 100,100 - 300,400 */
    SetRect(&cont, 100, 100, 400, 300);
    for(i = 0; i <= 10; i++)
    {
        ThemeWindowMetrics wm = { sizeof(ThemeWindowMetrics), 0, 0, 0, 0, 0 };
        static const short codes[] = { 0, 1, 2, 3, 5, 6, 7, 8, 32, 33 };
        int c;
        for(c = 0; c < (int)(sizeof codes / sizeof codes[0]); c++)
        {
            char what[96];
            SetEmptyRgn(r);
            err = GetThemeWindowRegion(i, &cont, kThemeStateActive, &wm,
                                       kThemeWindowHasGrow | kThemeWindowHasCloseBox | kThemeWindowHasFullZoom | kThemeWindowHasCollapseBox | kThemeWindowHasTitleText,
                                       codes[c], r);
            sprintf(what, "WINREGION type %d code %d err %d", i, codes[c], (int)err);
            rgn(what, r);
        }
        wm.titleHeight = 0;
        wm.titleWidth = 120;
        SetEmptyRgn(r);
        err = GetThemeWindowRegion(i, &cont, kThemeStateActive, &wm, kThemeWindowHasTitleText, 32, r);
        {
            char what[96];
            sprintf(what, "WINREGION type %d titleWidth120 struct err %d", i, (int)err);
            rgn(what, r);
        }
    }

    /* buttons in 20,20 - 40,100 */
    for(i = 0; i <= 20; i++)
    {
        Rect b;
        ThemeButtonDrawInfo info = { kThemeStateActive, kThemeButtonOff, kThemeAdornmentNone };
        char what[96];
        SetRect(&b, 20, 20, 100, 40);
        SetEmptyRgn(r);
        err = GetThemeButtonRegion(&b, i, &info, r);
        sprintf(what, "BUTTON %d region err %d", i, (int)err);
        rgn(what, r);
        SetRect(&out, 0, 0, 0, 0);
        err = GetThemeButtonContentBounds(&b, i, &info, &out);
        printf("BUTTON %d content err %d %d,%d,%d,%d\n", i, (int)err, out.top, out.left, out.bottom, out.right);
        SetRect(&out, 0, 0, 0, 0);
        err = GetThemeButtonBackgroundBounds(&b, i, &info, &out);
        printf("BUTTON %d background err %d %d,%d,%d,%d\n", i, (int)err, out.top, out.left, out.bottom, out.right);
        info.adornment = kThemeAdornmentDefault;
        SetEmptyRgn(r);
        err = GetThemeButtonRegion(&b, i, &info, r);
        sprintf(what, "BUTTON %d default region err %d", i, (int)err);
        rgn(what, r);
    }

    /* a vertical scroll bar 0,0 - 200,16, a horizontal one, a slider */
    {
        ThemeTrackDrawInfo t;
        char what[96];
        int k;
        for(k = 0; k < 3; k++)
        {
            t.kind = k == 2 ? kThemeSlider : kThemeScrollBar;
            if(k == 1) SetRect(&t.bounds, 0, 0, 200, 16); else if(k == 0) SetRect(&t.bounds, 0, 0, 16, 200); else SetRect(&t.bounds, 0, 0, 150, 18);
            t.min = 0; t.max = 100; t.value = 25; t.reserved = 0;
            t.attributes = k == 1 ? kThemeTrackHorizontal : (k == 2 ? kThemeTrackHorizontal : 0);
            t.enableState = kThemeTrackActive;
            t.filler1 = 0;
            t.trackInfo.scrollbar.viewsize = 50;
            t.trackInfo.scrollbar.pressState = 0;
            if(k == 2) { t.trackInfo.slider.thumbDir = kThemeThumbDownward; t.trackInfo.slider.pressState = 0; }
            SetRect(&out, 0, 0, 0, 0);
            err = GetThemeTrackBounds(&t, &out);
            printf("TRACK %d bounds err %d %d,%d,%d,%d\n", k, (int)err, out.top, out.left, out.bottom, out.right);
            SetEmptyRgn(r);
            err = GetThemeTrackThumbRgn(&t, r);
            sprintf(what, "TRACK %d thumb err %d", k, (int)err);
            rgn(what, r);
            SetRect(&out, 0, 0, 0, 0);
            err = GetThemeTrackDragRect(&t, &out);
            printf("TRACK %d drag err %d %d,%d,%d,%d\n", k, (int)err, out.top, out.left, out.bottom, out.right);
            {
                SInt32 lv = -1;
                err = GetThemeTrackLiveValue(&t, 40, &lv);
                printf("TRACK %d live(40) err %d %ld\n", k, (int)err, (long)lv);
            }
        }
        {
            Rect sb, tr;
            SetRect(&sb, 0, 0, 200, 16);
            err = GetThemeScrollBarTrackRect(&sb, kThemeTrackActive, 0, false, &tr);
            printf("INFO ScrollBarTrackRect vert err %d %d,%d,%d,%d\n", (int)err, tr.top, tr.left, tr.bottom, tr.right);
            SetRect(&sb, 0, 0, 16, 200);
            err = GetThemeScrollBarTrackRect(&sb, kThemeTrackActive, 0, true, &tr);
            printf("INFO ScrollBarTrackRect horiz err %d %d,%d,%d,%d\n", (int)err, tr.top, tr.left, tr.bottom, tr.right);
        }
    }
    {
        Rect tb;
        Point o = { 100, 100 };
        SetRect(&tb, 10, 10, 30, 90);
        for(i = 0; i < 4; i++)
        {
            char what[64];
            SetEmptyRgn(r);
            err = GetThemeTabRegion(&tb, i, kThemeTabNorth, r);
            sprintf(what, "TAB style %d err %d", i, (int)err);
            rgn(what, r);
        }
        for(i = 0; i < 4; i++)
        {
            SetRect(&out, 0, 0, 0, 0);
            err = GetThemeStandaloneGrowBoxBounds(o, i, false, &out);
            printf("GROWBOX dir %d err %d %d,%d,%d,%d\n", i, (int)err, out.top, out.left, out.bottom, out.right);
            err = GetThemeStandaloneGrowBoxBounds(o, i, true, &out);
            printf("GROWBOX small dir %d err %d %d,%d,%d,%d\n", i, (int)err, out.top, out.left, out.bottom, out.right);
        }
        SetRect(&tb, 20, 50, 200, 250);
        for(i = 0; i < 4; i++)
        {
            char what[64];
            SetEmptyRgn(r);
            err = GetThemeMenuBackgroundRegion(&tb, i, r);
            sprintf(what, "MENUBG type %d err %d", i, (int)err);
            rgn(what, r);
        }
    }
    printf("INFO Unregister %d\n", (int)UnregisterAppearanceClient());
    printf("INFO Unregister again %d\n", (int)UnregisterAppearanceClient());
    printf("PASS ran\nDONE 0\n---\n");
    return 0;
}
