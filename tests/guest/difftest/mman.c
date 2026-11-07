/* mman: where the Memory Manager puts blocks' data, differential
   (tools/difftest/run.py mman): each allocation's address modulo 16 for
   NewPtr, NewPtrClear, NewHandle, NewPtrSys, NewHandleSys, TempNewHandle,
   a resource, SetHandleSize and SetPtrSize, and a GWorld's pixels and
   rowBytes. Escape Velocity's PowerPC mask blitter takes the sprite
   pointer's low three bits as the mask's bit offset: it needs blocks
   8-aligned, as the PowerPC Mac's Memory Manager gives them. */
#include "probe.h"
#include <Resources.h>

#define N 24

static void residues(const char *key, unsigned long *a, int n)
{
    char buf[3 * N + 1];
    int i;
    for(i = 0; i < n; i++)
        sprintf(buf + 3 * i, "%2lu ", a[i] & 15);
    buf[3 * n - 1] = 0;
    out(key, "%s", buf);
}

/* the sizes asked for: small, odd, around multiples of 4 and 8 */
static const long sizes[N] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 13, 16,
                               17, 20, 24, 31, 33, 50, 80, 100, 255, 640, 3072, 6400 };

int main(void)
{
    unsigned long a[N];
    Ptr p[N];
    Handle h[N];
    OSErr err;
    int i;

    probe_open("mman");
    if(SECTION("ptr"))
    {
        for(i = 0; i < N; i++)
            a[i] = (unsigned long)(p[i] = NewPtr(sizes[i]));
        residues("NewPtr", a, N);
        for(i = 0; i < N; i++)
            DisposePtr(p[i]);
        for(i = 0; i < N; i++)
            a[i] = (unsigned long)(p[i] = NewPtrClear(sizes[i]));
        residues("NewPtrClear", a, N);
        /* interleaved with frees: holes reused */
        for(i = 0; i < N; i += 2)
            DisposePtr(p[i]);
        for(i = 0; i < N; i += 2)
            a[i / 2] = (unsigned long)(p[i] = NewPtr(sizes[N - 1 - i]));
        residues("NewPtr.reused", a, N / 2);
        for(i = 0; i < N; i++)
            DisposePtr(p[i]);
        for(i = 0; i < N; i++)
            a[i] = (unsigned long)(p[i] = NewPtrSys(sizes[i]));
        residues("NewPtrSys", a, N);
        for(i = 0; i < N; i++)
            DisposePtr(p[i]);
    }
    if(SECTION("handle"))
    {
        for(i = 0; i < N; i++)
            h[i] = NewHandle(sizes[i]);
        for(i = 0; i < N; i++)
            a[i] = (unsigned long)*h[i];
        residues("NewHandle", a, N);
        for(i = 0; i < N; i++)
        {
            SetHandleSize(h[i], sizes[N - 1 - i] + 3);
            a[i] = (unsigned long)*h[i];
        }
        residues("SetHandleSize", a, N);
        for(i = 0; i < N; i++)
        {
            MoveHHi(h[i]);
            a[i] = (unsigned long)*h[i];
        }
        residues("MoveHHi", a, N);
        CompactMem(0x7FFFFFFF);
        for(i = 0; i < N; i++)
            a[i] = (unsigned long)*h[i];
        residues("CompactMem", a, N);
        for(i = 0; i < N; i++)
            DisposeHandle(h[i]);
        for(i = 0; i < N; i++)
            h[i] = NewHandleSys(sizes[i]);
        for(i = 0; i < N; i++)
            a[i] = (unsigned long)*h[i];
        residues("NewHandleSys", a, N);
        for(i = 0; i < N; i++)
            DisposeHandle(h[i]);
        for(i = 0; i < N; i++)
        {
            h[i] = TempNewHandle(sizes[i], &err);
            a[i] = h[i] ? (unsigned long)*h[i] : 0xff;
        }
        residues("TempNewHandle", a, N);
        for(i = 0; i < N; i++)
            if(h[i])
                DisposeHandle(h[i]);
    }
    if(SECTION("resource"))
    {
        Handle r = GetResource('PAT#', 0);
        out("PAT#.0", "%s", r ? "" : "(none)");
        if(r)
        {
            a[0] = (unsigned long)*r;
            residues("PAT#.0.data", a, 1);
        }
    }
    if(SECTION("gworld"))
    {
        static const short widths[] = { 1, 7, 8, 80, 81, 640 };
        Rect b;
        for(i = 0; i < (int)(sizeof widths / sizeof widths[0]); i++)
        {
            GWorldPtr g = NULL;
            char k[32];
            SetRect(&b, 0, 0, widths[i], 10);
            err = NewGWorld(&g, 8, &b, NULL, NULL, 0);
            sprintf(k, "w%d.err", widths[i]);
            out(k, "%d", err);
            if(err == noErr)
            {
                PixMapHandle pm = GetGWorldPixMap(g);
                LockPixels(pm);
                sprintf(k, "w%d.rowBytes", widths[i]);
                out(k, "%d", (**pm).rowBytes & 0x3FFF);
                a[0] = (unsigned long)GetPixBaseAddr(pm);
                sprintf(k, "w%d.base", widths[i]);
                residues(k, a, 1);
                UnlockPixels(pm);
                DisposeGWorld(g);
            }
        }
    }
    if(SECTION("rowbytes"))
    {
        /* NewGWorld's rowBytes by depth and left edge, a width a column */
        static const short widths[] = { 1, 2, 3, 4, 15, 16, 17, 31, 32, 33, 63, 64, 100, 127, 128, 129, 512 };
        static const short depths[] = { 1, 2, 4, 8, 16, 32 };
        static const short lefts[] = { 0, 3, 17 };
        int d, l;
        for(d = 0; d < (int)(sizeof depths / sizeof depths[0]); d++)
            for(l = 0; l < (int)(sizeof lefts / sizeof lefts[0]); l++)
            {
                char k[32], v[256];
                int n = 0;
                for(i = 0; i < (int)(sizeof widths / sizeof widths[0]); i++)
                {
                    GWorldPtr g = NULL;
                    Rect b;
                    SetRect(&b, lefts[l], 5, lefts[l] + widths[i], 9);
                    err = NewGWorld(&g, depths[d], &b, NULL, NULL, 0);
                    if(err == noErr)
                    {
                        n += sprintf(v + n, " %d", (**GetGWorldPixMap(g)).rowBytes & 0x3FFF);
                        DisposeGWorld(g);
                    }
                    else
                        n += sprintf(v + n, " e%d", err);
                }
                sprintf(k, "d%d.l%d", depths[d], lefts[l]);
                out(k, "%s", v);
            }
    }
    probe_close();
    return 0;
}
