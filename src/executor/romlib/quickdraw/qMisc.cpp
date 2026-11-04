/* Copyright 1986-1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <ToolboxEvent.h>
#include <ToolboxUtil.h>

#include <quickdraw/quick.h>
#include <quickdraw/cquick.h>
#include <quickdraw/region.h>

#include <algorithm>
#include <vector>

using namespace Executor;

static Boolean EquivRect(const Rect *, const Rect *);
static INTEGER fromhex(char c);

static Boolean EquivRect(const Rect *rp1, const Rect *rp2)
{
    return rp1->bottom - rp1->top == rp2->bottom - rp2->top && rp1->right - rp1->left == rp2->right - rp2->left;
}

/* MacPhoenix: as the Quadra's ROM answers (a probe, 14 seeds x 4 calls,
   tests/Random.cpp): a seed with its top bit set counts as its low 31
   bits plus one, then seed * 16807 mod 2^31-1, the low word returned
   (-32768 as 0). Was the same in int32 arithmetic whose overflow the
   compiler doesn't wrap: half the seeds gave other numbers, and Photoshop
   4.0, which decodes its personalization with Random, said it was
   invalid. */
INTEGER Executor::C_Random()
{
    uint32_t seed = (uint32_t)qdGlobals().randSeed;
    if(seed & 0x80000000)
        seed = (seed & 0x7FFFFFFF) + 1;
    seed = (uint32_t)((uint64_t)seed * 16807 % 0x7FFFFFFF);
    qdGlobals().randSeed = seed;
    INTEGER retval = (INTEGER)(uint16_t)seed;
    return retval == -32768 ? 0 : retval;
}

/* MacPhoenix: as the Quadra ROM's $A833: the main device's PixMap copied
   as a 14-byte BitMap (rowBytes loses its pixel-map flag).  Callers pass a
   PixMap record too (Date & Time's clock port) and restore the flag
   themselves. */
void Executor::C_ScrnBitMap(BitMap *bm)
{
    PixMapHandle pm = GD_PMAP(LM(MainDevice));
    bm->baseAddr = PIXMAP_BASEADDR(pm);
    bm->rowBytes = (*pm)->rowBytes & 0x7FFF;
    bm->bounds = PIXMAP_BOUNDS(pm);
}

Boolean Executor::C_GetPixel(INTEGER h, INTEGER v)
{
    BitMap temp_bm;
    unsigned char temp_fbuf[4];
    Rect src_rect, dst_rect;

    temp_bm.baseAddr = (Ptr)temp_fbuf;
    temp_bm.bounds.top = 0;
    temp_bm.bounds.bottom = 1;
    temp_bm.bounds.left = 0;
    temp_bm.bounds.right = 1;
    temp_bm.rowBytes = 4;

    src_rect.top = v;
    src_rect.bottom = v + 1;
    src_rect.left = h;
    src_rect.right = h + 1;

    dst_rect = temp_bm.bounds;

    CopyBits(PORT_BITS_FOR_COPY(qdGlobals().thePort), &temp_bm,
             &src_rect, &dst_rect, srcCopy, nullptr);

    return (*temp_fbuf & 0x80) != 0;
}

/* fromhex: converts from '0'-'9' to 0-9, 'a-z' and 'A-Z' similarly */

static INTEGER fromhex(char c)
{
    if(c >= '0' && c <= '9')
        return (c - '0');
    else if(c >= 'A' && c <= 'Z')
        return (c - 'A' + 10);
    else
        return (c - 'a' + 10);
}

void Executor::C_StuffHex(Ptr p, ConstStringPtr s)
{
    char *sp, *ep;
    unsigned len;

    len = s[0];
    sp = (char *)s + 1;
    ep = sp + (len & ~1);
    for(; sp != ep; sp += 2)
        *p++ = (fromhex(*sp) << 4) | fromhex(sp[1]);

    if(len & 1)
        *p = (*p & 0xF) | (fromhex(*sp) << 4);
}

void Executor::C_ScalePt(GUEST<Point> *pt, const Rect *srcr, const Rect *dstr)
{
    INTEGER srcdh, srcdv, dstdh, dstdv;

    if(pt->h || pt->v)
    {
        srcdh = srcr->right - srcr->left;
        srcdv = srcr->bottom - srcr->top;
        dstdh = dstr->right - dstr->left;
        dstdv = dstr->bottom - dstr->top;

        pt->h = ((((LONGINT)pt->h * dstdh) << 1) / srcdh + 1) >> 1;
        pt->v = ((((LONGINT)pt->v * dstdv) << 1) / srcdv + 1) >> 1;

        if(pt->v < 1)
            pt->v = 1;
        if(pt->h < 1)
            pt->h = 1;
    }
}

/* One coordinate of MapPt, measured on the G3's 8.6 (tests/guest/difftest
   desk): an offset when the two sizes are the same (two empty sizes
   too), 0 from an empty source; otherwise scaled by the Fixed ratio of
   the sizes, rounded to the nearest (half up), or, for a negative ratio
   (an inverted rectangle), half a pixel towards zero and truncated. */
static INTEGER map_coord(INTEGER n, INTEGER srcd, INTEGER dstd)
{
    if(srcd == dstd)
        return n;
    if(srcd == 0)
        return 0;
    int64_t ratio = ((((int64_t)dstd << 17) / srcd) + 1) >> 1; /* rounded */
    int64_t x = (int64_t)n * ratio;
    if(ratio >= 0)
        return (x + 0x8000) >> 16;
    x += x > 0 ? -0x8000 : x < 0 ? 0x8000 : 0;
    return x / 0x10000;
}

void Executor::C_MapPt(GUEST<Point> *pt, const Rect *srcr, const Rect *dstr)
{
    INTEGER srcdh, srcdv, dstdh, dstdv;

    srcdh = srcr->right - srcr->left;
    srcdv = srcr->bottom - srcr->top;
    dstdh = dstr->right - dstr->left;
    dstdv = dstr->bottom - dstr->top;

    pt->h = map_coord(pt->h - srcr->left, srcdh, dstdh) + dstr->left;
    pt->v = map_coord(pt->v - srcr->top, srcdv, dstdv) + dstr->top;
}

void Executor::C_MapRect(Rect *r, const Rect *srcr, const Rect *dstr)
{
    MapPt((GUEST<Point> *)&r->top, srcr, dstr);
    MapPt((GUEST<Point> *)&r->bottom, srcr, dstr);
}

/* MapRgn maps each inversion point as MapPt does and builds the region
   again: lines that land on the same row merge, points that meet cancel,
   and a mirrored mapping (an inverted rectangle) turns the region over as
   8.6 does on the G3 (tests/guest/difftest desk). Nothing to scale from or
   to leaves it empty. */
void Executor::C_MapRgn(RgnHandle rh, const Rect *srcr, const Rect *dstr)
{
    INTEGER srcdh, dstdh, srcdv, dstdv;

    if(EquivRect(srcr, dstr))
        OffsetRgn(rh, dstr->left - srcr->left,
                  dstr->top - srcr->top);
    else if(RGN_SMALL_P(rh))
        MapRect(&(*rh)->rgnBBox, srcr, dstr);
    else if(srcr->right == srcr->left || srcr->bottom == srcr->top || EmptyRect(dstr))
        SetEmptyRgn(rh);
    else
    {
        srcdh = srcr->right - srcr->left;
        dstdh = dstr->right - dstr->left;
        srcdv = srcr->bottom - srcr->top;
        dstdv = dstr->bottom - dstr->top;

        struct Line
        {
            INTEGER v;
            std::vector<INTEGER> xs;
        };
        std::vector<Line> lines;
        GUEST<INTEGER> *ip = (GUEST<INTEGER> *)*rh + 5;
        for(INTEGER v; (v = *ip++) != 32767;)
        {
            Line l;
            l.v = map_coord(v - srcr->top, srcdv, dstdv) + dstr->top;
            for(INTEGER x; (x = *ip++) != 32767;)
                l.xs.push_back(map_coord(x - srcr->left, srcdh, dstdh) + dstr->left);
            lines.push_back(std::move(l));
        }
        std::stable_sort(lines.begin(), lines.end(),
                         [](const Line &a, const Line &b) { return a.v < b.v; });

        /* no longer than it was: written over the old data */
        GUEST<INTEGER> *op = (GUEST<INTEGER> *)*rh + 5;
        for(size_t i = 0; i < lines.size();)
        {
            std::vector<INTEGER> xs;
            size_t j = i;
            for(; j < lines.size() && lines[j].v == lines[i].v; j++)
                xs.insert(xs.end(), lines[j].xs.begin(), lines[j].xs.end());
            std::sort(xs.begin(), xs.end());
            std::vector<INTEGER> kept;
            for(INTEGER x : xs)
                if(!kept.empty() && kept.back() == x)
                    kept.pop_back();
                else
                    kept.push_back(x);
            if(!kept.empty())
            {
                *op++ = lines[i].v;
                for(INTEGER x : kept)
                    *op++ = x;
                *op++ = 32767;
            }
            i = j;
        }
        *op++ = 32767;
        ROMlib_sizergn(rh, false);
    }
}

void Executor::C_MapPoly(PolyHandle poly, const Rect *srcr, const Rect *dstr)
{
    GUEST<Point> *ip, *ep;

    if(EmptyRect(srcr) || EmptyRect(dstr))
        /* left alone (8.6 on the G3) */
        return;
    if(EquivRect(srcr, dstr))
        OffsetPoly(poly, dstr->left - srcr->left,
                   dstr->top - srcr->top);
    else
    {
        MapPt((GUEST<Point> *)&(*poly)->polyBBox.top, srcr, dstr);
        MapPt((GUEST<Point> *)&(*poly)->polyBBox.bottom, srcr, dstr);
        for(ip = (*poly)->polyPoints,
        ep = ip + ((*poly)->polySize - SMALLPOLY) / sizeof(Point);
            ip != ep;)
            MapPt(ip++, srcr, dstr);
    }
}

/* QuickDraw 8.5's pattern origin (NQD): kept and answered; Executor still
   aligns patterns with the port's origin. (0,0) until set, as on the G3's
   8.6. */
namespace
{
Point pattern_origin = { 0, 0 };
}

void Executor::C_QDSetPatternOrigin(Point origin)
{
    pattern_origin = origin;
}

void Executor::C_QDGetPatternOrigin(GUEST<Point> *origin)
{
    if(origin)
        *origin = pattern_origin;
}
