/* dither: CopyBits with ditherCopy from 32 bits to fewer, differential
   (tools/difftest/run.py dither). The ICM copies a movie frame to an 8-bit
   offscreen with ditherCopy before 'smc ' compresses it, and Executor drew
   ditherCopy as srcCopy (docs/executor/QuickTime.md). Each case fills a
   32-bit GWorld, copies it to a GWorld of another depth with ditherCopy
   (and srcCopy beside it), and prints the destination's rows as indexes:
   solid colours between the table's entries, ramps, an image of fixed
   pseudo-random colours, at 8, 4 and 1 bits with the standard tables and
   an 8-bit table of greys, whole and in part (offsets in source and
   destination). */
#include "probe.h"

#define W 32
#define H 8

static GWorldPtr src;

static void fill(unsigned long (*colour)(int x, int y, void *), void *arg)
{
    PixMapHandle pm = GetGWorldPixMap(src);
    Ptr base;
    long rb;
    int x, y;
    LockPixels(pm);
    base = GetPixBaseAddr(pm);
    rb = (**pm).rowBytes & 0x3FFF;
    for(y = 0; y < H; y++)
        for(x = 0; x < W; x++)
            ((unsigned long *)(base + y * rb))[x] = colour(x, y, arg);
}

static unsigned long solid(int x, int y, void *arg)
{
    return *(unsigned long *)arg;
}

/* red, green or blue (arg 16, 8, 0) from 0 to 255 across, or grey (-1) */
static unsigned long ramp(int x, int y, void *arg)
{
    int shift = *(int *)arg;
    unsigned long v = x * 255 / (W - 1);
    return shift < 0 ? (v << 16) | (v << 8) | v : v << shift;
}

static unsigned long random_colour(int x, int y, void *arg)
{
    unsigned long n = (unsigned long)(y * W + x) * 2654435761UL + 12345;
    n ^= n >> 13;
    n *= 0x5bd1e995UL;
    n ^= n >> 15;
    return n & 0x00FFFFFF;
}

static CTabHandle grey_table(void)
{
    CTabHandle ct = (CTabHandle)NewHandleClear(sizeof(ColorTable) + 255 * sizeof(ColorSpec));
    int i;
    (**ct).ctSeed = GetCTSeed();
    (**ct).ctFlags = 0;
    (**ct).ctSize = 255;
    for(i = 0; i < 256; i++)
    {
        unsigned short v = (unsigned short)((255 - i) * 0x101);
        (**ct).ctTable[i].value = i;
        (**ct).ctTable[i].rgb.red = (**ct).ctTable[i].rgb.green = (**ct).ctTable[i].rgb.blue = v;
    }
    return ct;
}

/* the destination's rows as pixel values, one hex digit per pixel below
   8 bits */
static void out_rows(const char *key, GWorldPtr dst, int depth)
{
    PixMapHandle pm = GetGWorldPixMap(dst);
    Ptr base;
    long rb;
    int y, x;
    char k[64], line[W * 3 + 1];
    LockPixels(pm);
    base = GetPixBaseAddr(pm);
    rb = (**pm).rowBytes & 0x3FFF;
    for(y = 0; y < H; y++)
    {
        unsigned char *row = (unsigned char *)(base + y * rb);
        char *p = line;
        for(x = 0; x < W; x++)
        {
            int v;
            if(depth == 8)
                v = row[x];
            else
            {
                int per = 8 / depth;
                v = (row[x / per] >> ((per - 1 - x % per) * depth)) & ((1 << depth) - 1);
            }
            p += sprintf(p, depth == 8 ? "%02x" : "%x", v);
            if(depth == 8 && x < W - 1)
                *p++ = ' ';
        }
        *p = 0;
        sprintf(k, "%s.%d", key, y);
        out(k, "%s", line);
    }
}

/* src (as filled) to a new GWorld of `depth` with table ct (nil: the
   standard one) by `mode`; srcRect/dstRect the whole, or offset */
static void copy_case(const char *key, int depth, CTabHandle ct, short mode, int part)
{
    GWorldPtr dst;
    Rect r, sr, dr;
    OSErr err;
    GWorldPtr save_port;
    GDHandle save_dev;
    SetRect(&r, 0, 0, W, H);
    err = NewGWorld(&dst, depth, &r, ct, NULL, 0);
    if(err != noErr)
    {
        out(key, "NewGWorld %d", err);
        return;
    }
    GetGWorld(&save_port, &save_dev);
    SetGWorld(dst, NULL);
    LockPixels(GetGWorldPixMap(dst));
    EraseRect(&r);
    if(part)
    {
        /* the source from x 5, y 1, to the destination at 3, 2 */
        SetRect(&sr, 5, 1, W, H);
        SetRect(&dr, 3, 2, W - 2, H + 1);
    }
    else
        sr = dr = r;
    ForeColor(blackColor);
    BackColor(whiteColor);
    CopyBits((BitMap *)*GetGWorldPixMap(src), (BitMap *)*GetGWorldPixMap(dst), &sr, &dr, mode, NULL);
    SetGWorld(save_port, save_dev);
    out_rows(key, dst, depth);
    DisposeGWorld(dst);
}

static void both(const char *name, int depth, CTabHandle ct, int part)
{
    char k[64];
    sprintf(k, "%s.dither", name);
    copy_case(k, depth, ct, ditherCopy, part);
    sprintf(k, "%s.copy", name);
    copy_case(k, depth, ct, srcCopy, part);
}

static const unsigned long solids[] = {
    0x808080, 0x404040, 0xC0C0C0, 0x101010, 0xF0F0F0, /* greys */
    0x800000, 0x008000, 0x000080, 0xFF8000, 0x80FF80, /* between the cube's */
    0x336699, 0x123456, 0xEDCBA9, 0x7F7F7F, 0x010101  /* on and off it */
};

int main(void)
{
    Rect r;
    char k[64];
    int i;
    probe_open("dither");
    SetRect(&r, 0, 0, W, H);
    if(NewGWorld(&src, 32, &r, NULL, NULL, 0) != noErr)
    {
        out("src", "NewGWorld failed");
        probe_close();
        return 0;
    }

    if(SECTION("solid8"))
        for(i = 0; i < sizeof solids / sizeof solids[0]; i++)
        {
            unsigned long c = solids[i];
            fill(solid, &c);
            sprintf(k, "solid8.%06lx", c);
            both(k, 8, NULL, 0);
        }

    if(SECTION("ramp8"))
    {
        static const int shifts[] = { -1, 16, 8, 0 };
        static const char *names[] = { "grey", "red", "green", "blue" };
        for(i = 0; i < 4; i++)
        {
            fill(ramp, (void *)&shifts[i]);
            sprintf(k, "ramp8.%s", names[i]);
            both(k, 8, NULL, 0);
        }
    }

    if(SECTION("random8"))
    {
        fill(random_colour, NULL);
        both("random8", 8, NULL, 0);
        both("random8.part", 8, NULL, 1);
    }

    if(SECTION("grey8"))
    {
        CTabHandle ct = grey_table();
        int g = -1;
        fill(ramp, &g);
        both("grey8.ramp", 8, ct, 0);
        fill(random_colour, NULL);
        both("grey8.random", 8, ct, 0);
        DisposeHandle((Handle)ct);
    }

    if(SECTION("depth4"))
    {
        unsigned long c = 0x808080;
        int g = -1;
        fill(solid, &c);
        both("depth4.grey80", 4, NULL, 0);
        fill(ramp, &g);
        both("depth4.ramp", 4, NULL, 0);
        fill(random_colour, NULL);
        both("depth4.random", 4, NULL, 0);
    }

    if(SECTION("depth1"))
    {
        unsigned long c = 0x808080;
        int g = -1;
        fill(solid, &c);
        both("depth1.grey80", 1, NULL, 0);
        fill(ramp, &g);
        both("depth1.ramp", 1, NULL, 0);
        fill(random_colour, NULL);
        both("depth1.random", 1, NULL, 0);
    }

    if(SECTION("end"))
        DisposeGWorld(src);
    probe_close();
    return 0;
}
