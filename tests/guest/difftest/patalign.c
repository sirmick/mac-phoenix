/* patalign: where a pattern starts, differential (tools/difftest/run.py
   patalign): a 50% grey painted in GWorlds whose bounds start at even and
   odd points, after SetOrigin, in an old-style port on such a bitmap, and
   in windows whose content starts at an even or odd point of the screen.
   Each answer is the painted area's first two rows, '#' black, '.' white.
   Microsoft Works 4's spreadsheet grid is dotted one pixel off on Executor
   against the G3, in a window whose content starts at x 1. */
#include "probe.h"

static const Pattern grey = { { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 } };

/* rows 0 and 1, 8 pixels, of an 8-bit GWorld (index 0 white) */
static void rows(const char *key, GWorldPtr gw)
{
    PixMapHandle pm = GetGWorldPixMap(gw);
    long rb = (**pm).rowBytes & 0x3FFF;
    unsigned char *p;
    char s[20];
    int x, y, n = 0;
    LockPixels(pm);
    p = (unsigned char *)GetPixBaseAddr(pm);
    for(y = 0; y < 2; y++)
    {
        for(x = 0; x < 8; x++)
            s[n++] = p[y * rb + x] ? '#' : '.';
        s[n++] = ' ';
    }
    s[n - 1] = 0;
    out(key, "%s", s);
    UnlockPixels(pm);
}

/* paint grey over gw's whole port, report it */
static void paint_gworld(const char *key, short depth, short left, short top, short orgh, short orgv)
{
    GWorldPtr gw = NULL, rd = NULL;
    CGrafPtr save_port;
    GDHandle save_dev;
    Rect b, z;
    GetGWorld(&save_port, &save_dev);
    SetRect(&b, left, top, left + 16, top + 16);
    if(NewGWorld(&gw, depth, &b, NULL, NULL, 0) != noErr)
    {
        out(key, "NewGWorld failed");
        return;
    }
    SetRect(&z, 0, 0, 16, 16);
    NewGWorld(&rd, 8, &z, NULL, NULL, 0);
    SetGWorld(gw, NULL);
    LockPixels(GetGWorldPixMap(gw));
    if(orgh || orgv)
        SetOrigin(orgh, orgv);
    EraseRect(&gw->portRect);
    PenPat(&grey);
    PaintRect(&gw->portRect);
    SetGWorld(rd, NULL);
    LockPixels(GetGWorldPixMap(rd));
    ForeColor(blackColor);
    BackColor(whiteColor);
    CopyBits((BitMap *)*GetGWorldPixMap(gw), (BitMap *)*GetGWorldPixMap(rd),
             &gw->portRect, &z, srcCopy, NULL);
    SetGWorld(save_port, save_dev);
    rows(key, rd);
    DisposeGWorld(rd);
    DisposeGWorld(gw);
}

/* a window with its content at (h, v) on the screen */
static void paint_window(const char *key, short h, short v, short orgh)
{
    WindowPtr w;
    GWorldPtr rd = NULL;
    CGrafPtr save_port;
    GDHandle save_dev;
    Rect b, z, src;
    GetGWorld(&save_port, &save_dev);
    SetRect(&b, h, v, h + 64, v + 32);
    w = NewCWindow(NULL, &b, "\ppatalign", true, plainDBox, (WindowPtr)-1, false, 0);
    SetPort(w);
    if(orgh)
        SetOrigin(orgh, 0);
    PenPat(&grey);
    PaintRect(&w->portRect);
    SetRect(&z, 0, 0, 16, 16);
    src = z;
    OffsetRect(&src, w->portRect.left, w->portRect.top);
    NewGWorld(&rd, 8, &z, NULL, NULL, 0);
    SetGWorld(rd, NULL);
    LockPixels(GetGWorldPixMap(rd));
    ForeColor(blackColor);
    BackColor(whiteColor);
    CopyBits(&w->portBits, (BitMap *)*GetGWorldPixMap(rd), &src, &z, srcCopy, NULL);
    SetGWorld(save_port, save_dev);
    rows(key, rd);
    DisposeGWorld(rd);
    DisposeWindow(w);
}

/* an old-style port on a 1-bit bitmap whose bounds start at (left, 0) */
static void paint_bitmap(const char *key, short left)
{
    GrafPort port;
    BitMap bm;
    static unsigned short bits[16];
    GrafPtr save;
    char s[20];
    int x, y, n = 0;
    memset(bits, 0, sizeof bits);
    bm.baseAddr = (Ptr)bits;
    bm.rowBytes = 2;
    SetRect(&bm.bounds, left, 0, left + 16, 8);
    GetPort(&save);
    OpenPort(&port);
    SetPortBits(&bm);
    SetRect(&port.portRect, left, 0, left + 16, 8);
    PenPat(&grey);
    PaintRect(&port.portRect);
    for(y = 0; y < 2; y++)
    {
        for(x = 0; x < 8; x++)
            s[n++] = (bits[y] & (0x8000 >> x)) ? '#' : '.';
        s[n++] = ' ';
    }
    s[n - 1] = 0;
    out(key, "%s", s);
    ClosePort(&port);
    SetPort(save);
}

int main(void)
{
    probe_open("patalign");
    if(SECTION("gworld"))
    {
        paint_gworld("d8.0.0", 8, 0, 0, 0, 0);
        paint_gworld("d8.1.0", 8, 1, 0, 0, 0);
        paint_gworld("d8.0.1", 8, 0, 1, 0, 0);
        paint_gworld("d8.3.5", 8, 3, 5, 0, 0);
        paint_gworld("d8.-1.0", 8, -1, 0, 0, 0);
        paint_gworld("d1.1.0", 1, 1, 0, 0, 0);
        paint_gworld("d32.1.0", 32, 1, 0, 0, 0);
    }
    if(SECTION("origin"))
    {
        paint_gworld("d8.org.1.0", 8, 0, 0, 1, 0);
        paint_gworld("d8.org.0.1", 8, 0, 0, 0, 1);
        paint_gworld("d8.org.3.5", 8, 0, 0, 3, 5);
        paint_gworld("d1.org.1.0", 1, 0, 0, 1, 0);
    }
    if(SECTION("bitmap"))
    {
        paint_bitmap("b.0", 0);
        paint_bitmap("b.1", 1);
        paint_bitmap("b.3", 3);
    }
    if(SECTION("window"))
    {
        paint_window("w.100.60", 100, 60, 0);
        paint_window("w.101.60", 101, 60, 0);
        paint_window("w.100.61", 100, 61, 0);
        paint_window("w.101.61", 101, 61, 0);
        paint_window("w.100.60.org1", 100, 60, 1);
        paint_window("w.101.60.org1", 101, 60, 1);
    }
    probe_close();
    return 0;
}
