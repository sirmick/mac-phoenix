/* probe.h: what a differential probe (tests/guest/difftest, tools/difftest)
   is made of. A probe calls the Toolbox and writes what came back, one
   "key: value" line each, to Host:<probe>.txt; the same probe runs on a
   ROM machine (Apple's code) and on Executor, and tools/difftest/run.py
   compares the two logs key by key.

   Rules that keep the logs comparable and catch the usual bugs:
   - outputs are filled with SENTINEL before each call, and printed whole:
     an output a call leaves alone shows as a5a5... (Executor answering 0
     from a call it lacks, the caller taking stale memory for a result);
   - handles, regions, strings and pixels are printed by content (sizes,
     bytes, CRCs), never by address; no ticks or dates;
   - each line is flushed as it is written, so a crash leaves what came
     before it; sections (SECTION) can be skipped to after a crash
     (Host:<probe>.start names the first section to run).

   Pixel dumps: probe_pixels() prints a CRC of a GWorld's pixels and, when
   Host:<probe>.dump exists, writes the GWorld as Host:<probe>-<crc>.ppm,
   the CRC of its key (HFS names end at 31 characters), the key in a
   comment line ("# section.key"): tools/difftest/run.py renames it
   <probe>-<section>.<key>.ppm. */
#ifndef PROBE_H
#define PROBE_H

#include <Types.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <CodeFragments.h>
#include <Fonts.h>
#include <MacWindows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>

#define SENTINEL 0xA5

static char probe_name[32];
static char probe_start[64];   /* first section to run ("" = all) */
static int probe_running = 1;
static int probe_dump = 0;
static char probe_section[64];

static unsigned long probe_crc32(const unsigned char *p, long n, unsigned long crc)
{
    long i;
    int k;
    crc = ~crc;
    for(i = 0; i < n; i++)
    {
        crc ^= p[i];
        for(k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320UL & (0 - (crc & 1)));
    }
    return ~crc;
}

/* key: printf-formatted value */
static void out(const char *key, const char *fmt, ...)
{
    va_list ap;
    if(!probe_running)
        return;
    printf("%s.%s: ", probe_section, key);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

static void out_hex(const char *key, const void *p, long n)
{
    char buf[3 * 64 + 1];
    long i, m = n > 64 ? 64 : n;
    buf[0] = 0;
    for(i = 0; i < m; i++)
        sprintf(buf + 2 * i, "%02x", ((const unsigned char *)p)[i]);
    if(n > 64)
        out(key, "%s... (%ld bytes, crc %08lx)", buf, n, probe_crc32(p, n, 0));
    else
        out(key, "%s", buf);
}

static void out_str(const char *key, ConstStringPtr s)
{
    char buf[256];
    if(!s)
    {
        out(key, "(nil)");
        return;
    }
    memcpy(buf, s + 1, s[0]);
    buf[s[0]] = 0;
    out(key, "\"%s\" (%d)", buf, s[0]);
}

static void out_rect(const char *key, const Rect *r)
{
    out(key, "%d,%d,%d,%d", r->top, r->left, r->bottom, r->right);
}

static void out_rgn(const char *key, RgnHandle r)
{
    if(!r || !*r)
    {
        out(key, "(nil)");
        return;
    }
    out(key, "bbox %d,%d,%d,%d size %d crc %08lx", (**r).rgnBBox.top, (**r).rgnBBox.left,
        (**r).rgnBBox.bottom, (**r).rgnBBox.right, (**r).rgnSize,
        probe_crc32((unsigned char *)*r, GetHandleSize((Handle)r), 0));
}

static void out_handle(const char *key, Handle h)
{
    if(!h)
        out(key, "(nil)");
    else if(!*h)
        out(key, "(empty)");
    else if(h == (Handle)0xA5A5A5A5)
        out(key, "(unset)");
    else
    {
        char k[64];
        sprintf(k, "%s.size", key);
        out(k, "%ld", GetHandleSize(h));
        HLock(h);
        sprintf(k, "%s.data", key);
        out_hex(k, *h, GetHandleSize(h));
        HUnlock(h);
    }
}

/* The section's lines and calls run only from the start section on. */
static int SECTION(const char *name)
{
    strncpy(probe_section, name, sizeof probe_section - 1);
    if(!probe_running && !strcmp(name, probe_start))
        probe_running = 1;
    if(probe_running)
    {
        printf("== %s\n", name);
        fflush(stdout);
    }
    return probe_running;
}

static void probe_open(const char *name)
{
    char path[64];
    FILE *f;
    /* as an application starts: a port, fonts, windows, menus */
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    strncpy(probe_name, name, sizeof probe_name - 1);
    sprintf(path, "Host:%s.start", name);
    if((f = fopen(path, "r")))
    {
        if(fgets(probe_start, sizeof probe_start, f))
        {
            probe_start[strcspn(probe_start, "\r\n")] = 0;
            probe_running = !probe_start[0];
        }
        fclose(f);
    }
    sprintf(path, "Host:%s.dump", name);
    if((f = fopen(path, "r")))
    {
        probe_dump = 1;
        fclose(f);
    }
    sprintf(path, "Host:%s.txt", name);
    freopen(path, probe_start[0] ? "a" : "w", stdout);
    setvbuf(stdout, NULL, _IONBF, 0);
}

static void probe_close(void)
{
    probe_running = 1;
    printf("DONE 0\n---\n");
    fflush(stdout);
}

/* A GWorld's pixels: CRC of each row's used bytes; with dumping on, a PPM. */
static void probe_pixels(const char *key, GWorldPtr gw)
{
    PixMapHandle pm = GetGWorldPixMap(gw);
    Rect b = (**pm).bounds;
    long rb = (**pm).rowBytes & 0x3FFF, y, x, w = b.right - b.left, h = b.bottom - b.top;
    int depth = (**pm).pixelSize;
    long used = (w * depth + 7) / 8;
    unsigned long crc = 0;
    Ptr base;
    LockPixels(pm);
    base = GetPixBaseAddr(pm);
    for(y = 0; y < h; y++)
        crc = probe_crc32((unsigned char *)base + y * rb, used, crc);
    out(key, "%ldx%ld depth %d crc %08lx", w, h, depth, crc);
    if(probe_dump && probe_running)
    {
        char path[96], full[96];
        FILE *f;
        sprintf(full, "%s.%s", probe_section, key);
        sprintf(path, "Host:%s-%08lx.ppm", probe_name,
                probe_crc32((const unsigned char *)full, strlen(full), 0));
        if((f = fopen(path, "wb")))
        {
            CTabHandle ct = (**pm).pmTable;
            fprintf(f, "P6\n# %s\n%ld %ld\n255\n", full, w, h);
            for(y = 0; y < h; y++)
                for(x = 0; x < w; x++)
                {
                    unsigned char *row = (unsigned char *)base + y * rb, rgb[3];
                    if(depth == 32)
                        rgb[0] = row[4 * x + 1], rgb[1] = row[4 * x + 2], rgb[2] = row[4 * x + 3];
                    else
                    {
                        int bit = x * depth, v = (row[bit / 8] >> (8 - depth - bit % 8)) & ((1 << depth) - 1);
                        RGBColor c = (**ct).ctTable[v < (**ct).ctSize + 1 ? v : 0].rgb;
                        if(depth == 1)
                            c.red = c.green = c.blue = v ? 0 : 0xFFFF;
                        rgb[0] = c.red >> 8, rgb[1] = c.green >> 8, rgb[2] = c.blue >> 8;
                    }
                    fwrite(rgb, 1, 3, f);
                }
            fclose(f);
        }
    }
    UnlockPixels(pm);
}

/* A routine looked up at run time (a private call, or one that may be
   missing): its address, or NULL, logged as "bind.<name>: found/missing". */
static void *probe_bind(const char *lib, const char *name)
{
    Str63 l;
    Str255 n, err;
    ConnectionID conn;
    Ptr main_addr, sym;
    SymClass cls;
    l[0] = strlen(lib);
    memcpy(l + 1, lib, l[0]);
    n[0] = strlen(name);
    memcpy(n + 1, name, n[0]);
    if(GetSharedLibrary(l, kPowerPCArch, kReferenceCFrag, &conn, &main_addr, err) != noErr
       || FindSymbol(conn, n, &sym, &cls) != noErr)
        sym = NULL;
    {
        char k[96];
        sprintf(k, "bind.%s", name);
        out(k, sym ? "found" : "missing");
    }
    return sym;
}

#endif
