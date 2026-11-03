/* clut: the main device's colour table, seeds and inverse table around
   SetEntries, differential (tools/difftest/run.py clut). Games fade the
   screen with SetEntries to all black, draw the next picture (CopyBits or
   DrawPicture of an 8-bit pixmap with its own colour table, srcCopy) and
   fade back in: what indexes the drawing gets depends on whether the
   device's ctSeed changed and the inverse table was rebuilt from the black
   table (EV Override's ship-entry screen came out in 2 colours on
   Executor). On the main device at 8 bits; each section sets the depth,
   draws on the screen through a colour port and reads the indexes back. */
#include "probe.h"

static GDHandle gd;
static ColorSpec saved[256];
static CTabHandle src_ct;
static GWorldPtr src_gw;

#define NBANDS 16
#define BAND_W 4
#define BAND_H 8
static const unsigned short band_rgb[NBANDS][3] = {
    { 0xFFFF, 0xFFFF, 0xFFFF }, { 0, 0, 0 }, { 0xAAAA, 0xAAAA, 0xAAAA }, { 0x5555, 0x5555, 0x5555 },
    { 0xFFFF, 0xFFFF, 0 }, { 0xFFFF, 0, 0 }, { 0, 0xFFFF, 0 }, { 0, 0, 0xFFFF },
    { 0, 0xFFFF, 0xFFFF }, { 0xFFFF, 0, 0xFFFF }, { 0xFFFF, 0x6666, 0 }, { 0x3333, 0x6666, 0x9999 },
    { 0x9999, 0x3333, 0x3333 }, { 0x1111, 0x1111, 0x1111 }, { 0xEEEE, 0xEEEE, 0xEEEE }, { 0x6666, 0x9999, 0x6666 } };

static CTabHandle dev_ct(void)
{
    return (**(**gd).gdPMap).pmTable;
}

static void set8(void)
{
    char k[32];
    gd = GetMainDevice();
    if((**(**gd).gdPMap).pixelSize != 8)
    {
        OSErr err = SetDepth(gd, 8, 1, 1);
        sprintf(k, "setdepth~");
        out(k, "%d", err);
    }
    out("depth", "%d", (**(**gd).gdPMap).pixelSize);
}

/* seeds: did the device table's seed change since `before`; is the
   inverse table built from the table as it is now */
static void out_seeds(const char *key, long before)
{
    char k[64];
    CTabHandle ct = dev_ct();
    ITabHandle it = (**gd).gdITable;
    sprintf(k, "%s.ctSeed", key);
    out(k, "%s", (**ct).ctSeed == before ? "same" : "changed");
    sprintf(k, "%s.itab", key);
    out(k, "%s", (**it).iTabSeed == (**ct).ctSeed ? "current" : "stale");
    sprintf(k, "%s.ctTable", key);
    out(k, "crc %08lx", probe_crc32((unsigned char *)(**ct).ctTable,
                                   ((**ct).ctSize + 1) * sizeof(ColorSpec), 0));
}

static void out_c2i(const char *key)
{
    char buf[NBANDS * 3 + 1];
    int i;
    for(i = 0; i < NBANDS; i++)
    {
        RGBColor c;
        c.red = band_rgb[i][0], c.green = band_rgb[i][1], c.blue = band_rgb[i][2];
        sprintf(buf + 3 * i, "%02lx ", Color2Index(&c) & 0xFF);
    }
    buf[3 * NBANDS - 1] = 0;
    out(key, "%s", buf);
}

static void make_source(void)
{
    Rect r;
    PixMapHandle pm;
    Ptr base;
    long rb;
    int i, x, y;
    if(src_gw)
        return;
    src_ct = (CTabHandle)NewHandle(sizeof(ColorTable) + (NBANDS - 1) * sizeof(ColorSpec));
    (**src_ct).ctSeed = GetCTSeed();
    (**src_ct).ctFlags = 0;
    (**src_ct).ctSize = NBANDS - 1;
    for(i = 0; i < NBANDS; i++)
    {
        (**src_ct).ctTable[i].value = i;
        (**src_ct).ctTable[i].rgb.red = band_rgb[i][0];
        (**src_ct).ctTable[i].rgb.green = band_rgb[i][1];
        (**src_ct).ctTable[i].rgb.blue = band_rgb[i][2];
    }
    SetRect(&r, 0, 0, NBANDS * BAND_W, BAND_H);
    out("newgworld", "%d", NewGWorld(&src_gw, 8, &r, src_ct, NULL, 0));
    pm = GetGWorldPixMap(src_gw);
    LockPixels(pm);
    base = GetPixBaseAddr(pm);
    rb = (**pm).rowBytes & 0x3FFF;
    for(y = 0; y < BAND_H; y++)
        for(x = 0; x < NBANDS * BAND_W; x++)
            ((unsigned char *)base)[y * rb + x] = x / BAND_W;
}

/* the screen's indexes under a drawn rectangle: one per band, and a CRC */
static void out_screen(const char *key, const Rect *r)
{
    PixMapHandle pm = (**gd).gdPMap;
    long rb = (**pm).rowBytes & 0x3FFF, x, y;
    unsigned char *base = (unsigned char *)(**pm).baseAddr;
    char buf[NBANDS * 3 + 1], k[64];
    unsigned long crc = 0;
    int i;
    for(y = r->top; y < r->bottom; y++)
        crc = probe_crc32(base + y * rb + r->left, r->right - r->left, crc);
    for(i = 0; i < NBANDS; i++)
        sprintf(buf + 3 * i, "%02x ", base[(r->top + BAND_H / 2) * rb + r->left + i * BAND_W + 1]);
    buf[3 * NBANDS - 1] = 0;
    sprintf(k, "%s.index", key);
    out(k, "%s", buf);
    sprintf(k, "%s.crc", key);
    out(k, "%08lx", crc);
}

static void draw_copybits(CGrafPtr port, const Rect *to)
{
    PixMapHandle pm = GetGWorldPixMap(src_gw);
    RGBColor black = { 0, 0, 0 }, white = { 0xFFFF, 0xFFFF, 0xFFFF };
    SetPort((GrafPtr)port);
    RGBForeColor(&black);
    RGBBackColor(&white);
    CopyBits((BitMap *)*pm, (BitMap *)*port->portPixMap, &(**pm).bounds, to, srcCopy, NULL);
}

static PicHandle record_picture(void)
{
    CGrafPtr save_port;
    GDHandle save_gd;
    PicHandle pic;
    PixMapHandle pm = GetGWorldPixMap(src_gw);
    Rect r = (**pm).bounds;
    GWorldPtr gw;
    GetGWorld(&save_port, &save_gd);
    NewGWorld(&gw, 8, &r, NULL, NULL, 0);
    SetGWorld(gw, NULL);
    pic = OpenPicture(&r);
    CopyBits((BitMap *)*pm, (BitMap *)*GetGWorldPixMap(gw), &r, &r, srcCopy, NULL);
    ClosePicture();
    SetGWorld(save_port, save_gd);
    DisposeGWorld(gw);
    out("pict.size", "%ld", GetHandleSize((Handle)pic));
    return pic;
}

static void save_entries(void)
{
    CTabHandle ct = dev_ct();
    int i, n = (**ct).ctSize + 1;
    for(i = 0; i < 256; i++)
    {
        saved[i] = (**ct).ctTable[i < n ? i : 0];
        saved[i].value = i;
    }
}

/* One fade: SetEntries to black (start, count, value fields as given),
   a CopyBits and a DrawPicture on the screen, then the table back. */
static void fade_case(INTEGER start, INTEGER first, INTEGER count)
{
    ColorSpec black[256];
    CGrafPort screen;
    Rect r1, r2;
    long seed0, seed1;
    PicHandle pic;
    int i;
    set8();
    make_source();
    pic = record_picture();
    save_entries();
    OpenCPort(&screen);
    SetRect(&r1, 40, 120, 40 + NBANDS * BAND_W, 120 + BAND_H);
    SetRect(&r2, 40, 140, 40 + NBANDS * BAND_W, 140 + BAND_H);
    seed0 = (**dev_ct()).ctSeed;
    out_seeds("before", seed0);
    out_c2i("before.c2i");
    for(i = 0; i < count; i++)
    {
        black[i].value = first + i;
        black[i].rgb.red = black[i].rgb.green = black[i].rgb.blue = 0;
    }
    SetEntries(start, count - 1, black);
    out("setentries", "%d", QDError());
    out_seeds("black", seed0);
    seed1 = (**dev_ct()).ctSeed;
    out_c2i("black.c2i");
    out_seeds("black.after_c2i", seed1);
    draw_copybits(&screen, &r1);
    out_seeds("black.after_copybits", seed1);
    out_screen("black.copybits", &r1);
    SetPort((GrafPtr)&screen);
    DrawPicture(pic, &r2);
    out_seeds("black.after_drawpicture", seed1);
    out_screen("black.drawpicture", &r2);
    SetEntries(0, 255, saved);
    out_seeds("restored", seed1);
    out_screen("restored.copybits", &r1);
    out_c2i("restored.c2i");
    draw_copybits(&screen, &r1);
    out_screen("redrawn.copybits", &r1);
    CloseCPort(&screen);
    KillPicture(pic);
}

int main(void)
{
    probe_open("clut");
    HideCursor();

    if(SECTION("depth"))
    {
        gd = GetMainDevice();
        out("before~", "%d", (**(**gd).gdPMap).pixelSize);
        out("hasdepth8", "%s", HasDepth(gd, 8, 1, 1) ? "yes" : "no");
        set8();
        out("ctSize", "%d", (**dev_ct()).ctSize);
        out("ctFlags", "%04x", (unsigned short)(**dev_ct()).ctFlags);
    }

    /* no fade: what the drawing gets with the table as it is */
    if(SECTION("plain"))
    {
        CGrafPort screen;
        Rect r1, r2;
        PicHandle pic;
        set8();
        make_source();
        pic = record_picture();
        OpenCPort(&screen);
        SetRect(&r1, 40, 120, 40 + NBANDS * BAND_W, 120 + BAND_H);
        SetRect(&r2, 40, 140, 40 + NBANDS * BAND_W, 140 + BAND_H);
        out_seeds("seeds", (**dev_ct()).ctSeed);
        out_c2i("c2i");
        draw_copybits(&screen, &r1);
        out_screen("copybits", &r1);
        SetPort((GrafPtr)&screen);
        DrawPicture(pic, &r2);
        out_screen("drawpicture", &r2);
        /* SetEntries with the same colours */
        save_entries();
        {
            long seed = (**dev_ct()).ctSeed;
            SetEntries(0, 255, saved);
            out_seeds("same_entries", seed);
        }
        CloseCPort(&screen);
        KillPicture(pic);
    }

    if(SECTION("fade_all"))   /* SetEntries(0, 255, ...): EV Override's */
        fade_case(0, 0, 256);
    if(SECTION("fade_index")) /* SetEntries(-1, ...): the value fields */
        fade_case(-1, 0, 256);
    if(SECTION("fade_part"))  /* indexes 2-15 only */
        fade_case(2, 2, 14);

    if(SECTION("end"))
        ShowCursor();
    probe_close();
    return 0;
}
