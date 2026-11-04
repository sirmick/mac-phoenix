/* qd.c — QuickDraw: CopyBits between the window and offscreen GWorlds of
 * 1, 8 and 32 bits, PaintRect, FrameOval, DrawString (a bitmap font and,
 * if the System has one, TrueType), region operations. In a window over
 * the screen below the menu bar (640x460 on a 640x480 screen). */

#include "bench.h"

#include <Fonts.h>
#include <Gestalt.h>
#include <Menus.h>
#include <QDOffscreen.h>
#include <Quickdraw.h>
#include <Windows.h>
#include <stdio.h>

#pragma segment Qd

extern QDGlobals qd;

static WindowPtr win;
static Rect area;               /* the window's port rectangle */

static long need_window(void)
{
    long qdv;
    Rect r;

    if(Gestalt(gestaltQuickdrawVersion, &qdv) != noErr || qdv < gestalt32BitQD)
    {
        bench_skip("no 32-bit QuickDraw");
        return 1;
    }
    if(!win)
    {
        r = qd.screenBits.bounds;
        r.top += GetMBarHeight();
        if(r.right - r.left > 640)
            r.right = r.left + 640;
        if(r.bottom - r.top > 460)
            r.bottom = r.top + 460;
        win = NewCWindow(NULL, &r, "\pMacPhoenixBench", true, plainDBox, (WindowPtr)-1, false, 0);
        if(!win)
        {
            bench_skip("no window");
            return 1;
        }
        SetPort(win);
        area = win->portRect;
        EraseRect(&area);
    }
    SetPort(win);
    ForeColor(blackColor);
    BackColor(whiteColor);
    PenNormal();
    return 0;
}

/* ---- copybits_N_to (window -> GWorld), copybits_N_from (GWorld ->
 * window): 50 copies of the whole area ---- */

static GWorldPtr gw;

static long gw_setup(short depth)
{
    PixMapHandle pm;

    if(need_window())
        return 1;
    if(NewGWorld(&gw, depth, &area, NULL, NULL, 0) != noErr || !gw)
    {
        gw = NULL;
        bench_skip("NewGWorld failed");
        return 1;
    }
    pm = GetGWorldPixMap(gw);
    LockPixels(pm);
    return 0;
}

static long gw_setup1(void) { return gw_setup(1); }
static long gw_setup8(void) { return gw_setup(8); }
static long gw_setup32(void) { return gw_setup(32); }

static long gw_done(void)
{
    UnlockPixels(GetGWorldPixMap(gw));
    DisposeGWorld(gw);
    gw = NULL;
    return 0;
}

static long copy_to(void)
{
    int i;

    for(i = 0; i < 50; i++)
        CopyBits(&((GrafPtr)win)->portBits, (BitMap *)*GetGWorldPixMap(gw), &area, &area, srcCopy, NULL);
    return 0;
}

static long copy_from(void)
{
    int i;

    for(i = 0; i < 50; i++)
        CopyBits((BitMap *)*GetGWorldPixMap(gw), &((GrafPtr)win)->portBits, &area, &area, srcCopy, NULL);
    return 0;
}

/* ---- shapes ---- */

static long paintrect(void)
{
    unsigned long seed = 1;
    Rect r;
    int i, x, y;

    for(i = 0; i < 2000; i++)
    {
        seed = seed * 1103515245ul + 12345;
        x = (int)((seed >> 16) % (area.right - 64));
        y = (int)((seed >> 4) % (area.bottom - 48));
        SetRect(&r, x, y, x + 64, y + 48);
        if(i & 1)
            PaintRect(&r);
        else
            EraseRect(&r);
    }
    return 0;
}

static long frameoval(void)
{
    unsigned long seed = 2;
    Rect r;
    int i, x, y;

    for(i = 0; i < 1000; i++)
    {
        seed = seed * 1103515245ul + 12345;
        x = (int)((seed >> 16) % (area.right - 80));
        y = (int)((seed >> 4) % (area.bottom - 60));
        SetRect(&r, x, y, x + 80, y + 60);
        FrameOval(&r);
    }
    EraseRect(&area);
    return 0;
}

/* ---- text: 1,000 DrawString of 40 characters: Monaco 9 (a bitmap);
 * Times 13 (TrueType if the System has Times as an outline: no bitmap at
 * 13, so RealFont says whether it is) ---- */

static short text_font, text_size;

static long text_setup(ConstStr255Param name, short size, int outline)
{
    short f;
    char note[80];

    if(need_window())
        return 1;
    GetFNum(name, &f);
    if(f == 0)
    {
        bench_skip("no such font");
        return 1;
    }
    if(outline && !RealFont(f, size))
    {
        bench_skip("no outline font");
        return 1;
    }
    text_font = f;
    text_size = size;
    sprintf(note, "%.*s %d: font %d", name[0], (const char *)name + 1, size, f);
    bench_note(note);
    return 0;
}

static long text_setup_bitmap(void) { return text_setup("\pMonaco", 9, 0); }
static long text_setup_tt(void) { return text_setup("\pTimes", 13, 1); }

static long text_done(void)
{
    TextFont(0);
    TextSize(0);
    EraseRect(&area);
    return 0;
}

static long draw_text(void)
{
    int i, lines = (area.bottom - 4) / (text_size + 3);

    TextFont(text_font);
    TextSize(text_size);
    for(i = 0; i < 1000; i++)
    {
        MoveTo(4, 4 + text_size + (i % lines) * (text_size + 3));
        DrawString("\pThe quick brown fox jumps over 1,234 dogs");
    }
    return 0;
}

/* ---- regions: a region of 60 ovals; UnionRgn with itself moved, 200
 * times; InsetRgn of a copy, 100 times ---- */

static RgnHandle rgn_big, rgn_tmp, rgn_out;

static long rgn_setup(void)
{
    unsigned long seed = 3;
    RgnHandle o;
    Rect r;
    int i, x, y;

    if(need_window())
        return 1;
    rgn_big = NewRgn();
    rgn_tmp = NewRgn();
    rgn_out = NewRgn();
    o = NewRgn();
    for(i = 0; i < 60; i++)
    {
        seed = seed * 1103515245ul + 12345;
        x = (int)((seed >> 16) % (area.right - 90));
        y = (int)((seed >> 4) % (area.bottom - 70));
        SetRect(&r, x, y, x + 90, y + 70);
        OpenRgn();
        FrameOval(&r);
        CloseRgn(o);
        UnionRgn(rgn_big, o, rgn_big);
    }
    DisposeRgn(o);
    EraseRect(&area);
    return 0;
}

static long rgn_done(void)
{
    DisposeRgn(rgn_big);
    DisposeRgn(rgn_tmp);
    DisposeRgn(rgn_out);
    return 0;
}

static long rgn_union(void)
{
    int i;

    for(i = 0; i < 200; i++)
    {
        CopyRgn(rgn_big, rgn_tmp);
        OffsetRgn(rgn_tmp, i % 7, i % 5);
        UnionRgn(rgn_big, rgn_tmp, rgn_out);
    }
    return GetHandleSize((Handle)rgn_out);
}

static long rgn_inset(void)
{
    int i;

    for(i = 0; i < 100; i++)
    {
        CopyRgn(rgn_big, rgn_tmp);
        InsetRgn(rgn_tmp, 3, 2);
    }
    return GetHandleSize((Handle)rgn_tmp);
}

void add_qd_tests(void)
{
    bench_add("copybits_1_to", "50x_window", copy_to, gw_setup1, gw_done);
    bench_add("copybits_1_from", "50x_window", copy_from, gw_setup1, gw_done);
    bench_add("copybits_8_to", "50x_window", copy_to, gw_setup8, gw_done);
    bench_add("copybits_8_from", "50x_window", copy_from, gw_setup8, gw_done);
    bench_add("copybits_32_to", "50x_window", copy_to, gw_setup32, gw_done);
    bench_add("copybits_32_from", "50x_window", copy_from, gw_setup32, gw_done);
    bench_add("paintrect", "2000_64x48", paintrect, need_window, NULL);
    bench_add("frameoval", "1000_80x60", frameoval, need_window, NULL);
    bench_add("drawstring_bitmap", "1000x40ch", draw_text, text_setup_bitmap, text_done);
    bench_add("drawstring_tt", "1000x40ch", draw_text, text_setup_tt, text_done);
    bench_add("rgn_union", "200x60_ovals", rgn_union, rgn_setup, rgn_done);
    bench_add("rgn_inset", "100x60_ovals", rgn_inset, rgn_setup, rgn_done);
}
