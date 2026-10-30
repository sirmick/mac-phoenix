/* MacPhoenix: TrueType outlines as bitmap strikes, written from Apple's
   TrueType Reference Manual (the 'sfnt' tables head, hhea, maxp, hmtx,
   cmap, loca, glyf, hdmx). Glyphs are scan converted without hinting: a
   pixel is on when its centre is inside the outline (non-zero winding),
   with dropout control for strokes thinner than a pixel. Integer advance
   widths come from 'hdmx' (the hinted widths recorded in the font) when it
   has the size. The strike is an NFNT (Inside Macintosh: Text 4-66), so
   QuickDraw draws and measures it as it does a bitmap font. */

#include <base/common.h>
#include <MemoryMgr.h>
#include <quickdraw/sfnt.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace Executor;

namespace
{
struct Reader
{
    const uint8_t *p;
    size_t n;
    bool ok(size_t off, size_t len) const { return off <= n && len <= n - off; }
    unsigned u8(size_t o) const { return ok(o, 1) ? p[o] : 0; }
    unsigned u16(size_t o) const { return ok(o, 2) ? (p[o] << 8) | p[o + 1] : 0; }
    int s16(size_t o) const { return (int16_t)u16(o); }
    uint32_t u32(size_t o) const { return ok(o, 4) ? (uint32_t)u16(o) << 16 | u16(o + 2) : 0; }
};

struct Table
{
    size_t off = 0, len = 0;
};

struct Font
{
    Reader r;
    Table head, hhea, maxp, hmtx, cmap, loca, glyf, hdmx;
    int upem = 0, loca_long = 0, num_glyphs = 0, num_hmetrics = 0;
    int ascender = 0, descender = 0, line_gap = 0, advance_max = 0;
    size_t cmap_sub = 0;
    int cmap_format = -1;

    bool load();
    int glyph_for(int c) const;
    int advance(int g) const { return r.u16(hmtx.off + 4 * std::min(g, num_hmetrics - 1)); }
    bool glyph_range(int g, size_t *off, size_t *len) const;
    /* the 'hdmx' widths for PPEM (one byte per glyph), or -1 */
    long hdmx_record(int ppem) const;
};

bool Font::load()
{
    unsigned ntables = r.u16(4);
    if(!r.ok(12, 16 * ntables))
        return false;
    for(unsigned i = 0; i < ntables; i++)
    {
        size_t e = 12 + 16 * i;
        Table t{ r.u32(e + 8), r.u32(e + 12) };
        if(!r.ok(t.off, t.len))
            continue;
        uint32_t tag = r.u32(e);
        switch(tag)
        {
            case 0x68656164: head = t; break; /* head */
            case 0x68686561: hhea = t; break; /* hhea */
            case 0x6d617870: maxp = t; break; /* maxp */
            case 0x686d7478: hmtx = t; break; /* hmtx */
            case 0x636d6170: cmap = t; break; /* cmap */
            case 0x6c6f6361: loca = t; break; /* loca */
            case 0x676c7966: glyf = t; break; /* glyf */
            case 0x68646d78: hdmx = t; break; /* hdmx */
        }
    }
    if(head.len < 54 || hhea.len < 36 || maxp.len < 6 || !hmtx.len || !cmap.len
       || !loca.len || !glyf.len)
        return false;
    upem = r.u16(head.off + 18);
    loca_long = r.s16(head.off + 50);
    num_glyphs = r.u16(maxp.off + 4);
    ascender = r.s16(hhea.off + 4);
    descender = r.s16(hhea.off + 6);
    line_gap = r.s16(hhea.off + 8);
    advance_max = r.u16(hhea.off + 10);
    num_hmetrics = r.u16(hhea.off + 34);
    if(upem < 16 || !num_glyphs || !num_hmetrics || hmtx.len < 4u * num_hmetrics)
        return false;
    /* Macintosh Roman (platform 1, encoding 0): the Font Manager's
       character codes */
    unsigned nsub = r.u16(cmap.off + 2);
    for(unsigned i = 0; i < nsub; i++)
    {
        size_t e = cmap.off + 4 + 8 * i;
        if(r.u16(e) == 1 && r.u16(e + 2) == 0)
        {
            cmap_sub = cmap.off + r.u32(e + 4);
            cmap_format = r.u16(cmap_sub);
            break;
        }
    }
    return cmap_format == 0 || cmap_format == 4 || cmap_format == 6;
}

int Font::glyph_for(int c) const
{
    int g = 0;
    switch(cmap_format)
    {
        case 0:
            g = r.u8(cmap_sub + 6 + c);
            break;
        case 6:
        {
            int first = r.u16(cmap_sub + 6), count = r.u16(cmap_sub + 8);
            if(c >= first && c < first + count)
                g = r.u16(cmap_sub + 10 + 2 * (c - first));
            break;
        }
        case 4:
        {
            int segs = r.u16(cmap_sub + 6) / 2;
            size_t ends = cmap_sub + 14, starts = ends + 2 * segs + 2,
                   deltas = starts + 2 * segs, ranges = deltas + 2 * segs;
            for(int i = 0; i < segs; i++)
            {
                if(c > (int)r.u16(ends + 2 * i))
                    continue;
                int start = r.u16(starts + 2 * i);
                if(c < start)
                    break;
                int delta = r.u16(deltas + 2 * i);
                unsigned ro = r.u16(ranges + 2 * i);
                if(!ro)
                    g = (c + delta) & 0xFFFF;
                else
                {
                    g = r.u16(ranges + 2 * i + ro + 2 * (c - start));
                    if(g)
                        g = (g + delta) & 0xFFFF;
                }
                break;
            }
            break;
        }
    }
    return g < num_glyphs ? g : 0;
}

bool Font::glyph_range(int g, size_t *off, size_t *len) const
{
    if(g < 0 || g >= num_glyphs)
        return false;
    size_t a, b;
    if(loca_long)
    {
        a = r.u32(loca.off + 4 * g);
        b = r.u32(loca.off + 4 * g + 4);
    }
    else
    {
        a = 2 * r.u16(loca.off + 2 * g);
        b = 2 * r.u16(loca.off + 2 * g + 2);
    }
    if(b < a || b > glyf.len)
        return false;
    *off = glyf.off + a;
    *len = b - a;
    return true;
}

long Font::hdmx_record(int ppem) const
{
    if(hdmx.len < 8)
        return -1;
    unsigned n = r.u16(hdmx.off + 2);
    uint32_t size = r.u32(hdmx.off + 4);
    for(unsigned i = 0; i < n; i++)
    {
        size_t rec = hdmx.off + 8 + i * size;
        if(!r.ok(rec, 2 + num_glyphs))
            break;
        if((int)r.u8(rec) == ppem)
            return rec;
    }
    return -1;
}

struct Pt
{
    double x, y;
    bool on;
};

struct Outline
{
    std::vector<Pt> pts;
    std::vector<int> ends; /* last point of each contour */
};

double f2dot14(int v)
{
    return v / 16384.0;
}

bool glyph_outline(const Font &f, int g, Outline &o, int depth)
{
    size_t off, len;
    if(!f.glyph_range(g, &off, &len))
        return false;
    if(len == 0)
        return true; /* no contours (space) */
    const Reader &r = f.r;
    size_t end = off + len;
    int nc = r.s16(off);
    if(nc >= 0)
    {
        size_t p = off + 10;
        std::vector<int> ends(nc);
        for(int i = 0; i < nc; i++)
        {
            ends[i] = r.u16(p + 2 * i);
            if(i && ends[i] <= ends[i - 1])
                return false;
        }
        p += 2 * nc;
        int npts = nc ? ends[nc - 1] + 1 : 0;
        p += 2 + r.u16(p); /* instructions */
        std::vector<uint8_t> flags(npts);
        for(int i = 0; i < npts && p < end;)
        {
            uint8_t fl = r.u8(p++);
            flags[i++] = fl;
            if(fl & 8)
                for(int rep = r.u8(p++); rep > 0 && i < npts; rep--)
                    flags[i++] = fl;
        }
        size_t base = o.pts.size();
        int v = 0;
        for(int i = 0; i < npts; i++)
        {
            if(flags[i] & 2)
            {
                int d = r.u8(p++);
                v += flags[i] & 16 ? d : -d;
            }
            else if(!(flags[i] & 16))
            {
                v += r.s16(p);
                p += 2;
            }
            o.pts.push_back({ (double)v, 0, (flags[i] & 1) != 0 });
        }
        v = 0;
        for(int i = 0; i < npts; i++)
        {
            if(flags[i] & 4)
            {
                int d = r.u8(p++);
                v += flags[i] & 32 ? d : -d;
            }
            else if(!(flags[i] & 32))
            {
                v += r.s16(p);
                p += 2;
            }
            o.pts[base + i].y = v;
        }
        if(p > end)
            return false;
        for(int e : ends)
            o.ends.push_back(base + e);
        return true;
    }
    if(depth > 8)
        return false;
    size_t p = off + 10;
    for(;;)
    {
        if(p + 4 > end)
            return false;
        unsigned fl = r.u16(p), sub = r.u16(p + 2);
        p += 4;
        int a1, a2;
        if(fl & 1) /* ARG_1_AND_2_ARE_WORDS */
        {
            a1 = fl & 2 ? r.s16(p) : (int)r.u16(p);
            a2 = fl & 2 ? r.s16(p + 2) : (int)r.u16(p + 2);
            p += 4;
        }
        else
        {
            a1 = fl & 2 ? (int8_t)r.u8(p) : (int)r.u8(p);
            a2 = fl & 2 ? (int8_t)r.u8(p + 1) : (int)r.u8(p + 1);
            p += 2;
        }
        double a = 1, b = 0, c = 0, d = 1;
        if(fl & 8) /* WE_HAVE_A_SCALE */
        {
            a = d = f2dot14(r.s16(p));
            p += 2;
        }
        else if(fl & 0x40) /* WE_HAVE_AN_X_AND_Y_SCALE */
        {
            a = f2dot14(r.s16(p));
            d = f2dot14(r.s16(p + 2));
            p += 4;
        }
        else if(fl & 0x80) /* WE_HAVE_A_TWO_BY_TWO */
        {
            a = f2dot14(r.s16(p));
            b = f2dot14(r.s16(p + 2));
            c = f2dot14(r.s16(p + 4));
            d = f2dot14(r.s16(p + 6));
            p += 8;
        }
        Outline part;
        if(!glyph_outline(f, sub, part, depth + 1))
            return false;
        for(Pt &q : part.pts)
        {
            double x = q.x, y = q.y;
            q.x = a * x + c * y;
            q.y = b * x + d * y;
        }
        double dx = 0, dy = 0;
        if(fl & 2) /* ARGS_ARE_XY_VALUES */
        {
            dx = a1;
            dy = a2;
        }
        else if(a1 >= 0 && a1 < (int)o.pts.size() && a2 >= 0 && a2 < (int)part.pts.size())
        {
            /* point matching: the part's point a2 on the glyph's point a1 */
            dx = o.pts[a1].x - part.pts[a2].x;
            dy = o.pts[a1].y - part.pts[a2].y;
        }
        size_t base = o.pts.size();
        for(Pt &q : part.pts)
            o.pts.push_back({ q.x + dx, q.y + dy, q.on });
        for(int e : part.ends)
            o.ends.push_back(base + e);
        if(!(fl & 0x20)) /* MORE_COMPONENTS */
            break;
    }
    return true;
}

struct Edge
{
    double x0, y0, x1, y1;
};

void flatten(const Outline &o, double scale, std::vector<Edge> &edges)
{
    auto line = [&](Pt a, Pt b) {
        if(a.y != b.y || a.x != b.x)
            edges.push_back({ a.x * scale, a.y * scale, b.x * scale, b.y * scale });
    };
    auto quad = [&](Pt a, Pt c, Pt b) {
        double ddx = (a.x - 2 * c.x + b.x) * scale, ddy = (a.y - 2 * c.y + b.y) * scale;
        int steps = std::min(16, 1 + (int)std::sqrt(std::hypot(ddx, ddy) * 2));
        Pt prev = a;
        for(int i = 1; i <= steps; i++)
        {
            double t = (double)i / steps, u = 1 - t;
            Pt q{ u * u * a.x + 2 * u * t * c.x + t * t * b.x,
                  u * u * a.y + 2 * u * t * c.y + t * t * b.y, true };
            line(prev, q);
            prev = q;
        }
    };
    auto mid = [](Pt a, Pt b) { return Pt{ (a.x + b.x) / 2, (a.y + b.y) / 2, true }; };
    int s = 0;
    for(int e : o.ends)
    {
        int n = e - s + 1;
        if(n >= 2)
        {
            auto P = [&](int i) { return o.pts[s + (i % n + n) % n]; };
            int start = -1;
            for(int i = 0; i < n; i++)
                if(P(i).on)
                {
                    start = i;
                    break;
                }
            Pt begin, cur, ctrl;
            bool have_ctrl = false;
            auto to = [&](Pt q) {
                if(q.on)
                {
                    if(have_ctrl)
                        quad(cur, ctrl, q);
                    else
                        line(cur, q);
                    cur = q;
                    have_ctrl = false;
                }
                else
                {
                    if(have_ctrl)
                    {
                        Pt m = mid(ctrl, q);
                        quad(cur, ctrl, m);
                        cur = m;
                    }
                    ctrl = q;
                    have_ctrl = true;
                }
            };
            if(start >= 0)
            {
                begin = cur = P(start);
                for(int k = 1; k <= n; k++)
                    to(P(start + k));
            }
            else
            {
                begin = cur = mid(P(n - 1), P(0));
                for(int k = 0; k < n; k++)
                    to(P(k));
                to(begin);
            }
        }
        s = e + 1;
    }
}

struct Glyph
{
    int xmin = 0, ymax = 0, width = 0, height = 0; /* pixels, y up */
    std::vector<uint8_t> bits;                     /* width x height, row 0 at ymax */
    int advance = 0;
    bool missing = true;
};

/* spans narrower than this between two pixel centres get a pixel */
const double DROPOUT_MIN = 0.35;

void rasterize(const std::vector<Edge> &edges, Glyph &g)
{
    if(edges.empty())
        return;
    double minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
    for(const Edge &e : edges)
    {
        minx = std::min({ minx, e.x0, e.x1 });
        maxx = std::max({ maxx, e.x0, e.x1 });
        miny = std::min({ miny, e.y0, e.y1 });
        maxy = std::max({ maxy, e.y0, e.y1 });
    }
    g.xmin = (int)std::floor(minx);
    g.ymax = (int)std::ceil(maxy);
    g.width = (int)std::ceil(maxx) - g.xmin;
    g.height = g.ymax - (int)std::floor(miny);
    if(g.width <= 0 || g.height <= 0 || g.width > 1024 || g.height > 1024)
    {
        g.width = g.height = 0;
        return;
    }
    g.bits.assign(g.width * g.height, 0);
    struct Cross
    {
        double at;
        int dir;
        bool operator<(const Cross &o) const { return at < o.at; }
    };
    std::vector<Cross> xs;
    /* rows: pixel centres inside the outline, and dropouts */
    for(int row = 0; row < g.height; row++)
    {
        double yc = g.ymax - row - 0.5;
        xs.clear();
        for(const Edge &e : edges)
            if((e.y0 <= yc && yc < e.y1) || (e.y1 <= yc && yc < e.y0))
                xs.push_back({ e.x0 + (yc - e.y0) / (e.y1 - e.y0) * (e.x1 - e.x0), e.y1 > e.y0 ? 1 : -1 });
        std::sort(xs.begin(), xs.end());
        int wind = 0;
        double xa = 0;
        for(const Cross &c : xs)
        {
            int was = wind;
            wind += c.dir;
            if(!was && wind)
                xa = c.at;
            else if(was && !wind)
            {
                int lo = (int)std::ceil(xa - g.xmin - 0.5), hi = (int)std::ceil(c.at - g.xmin - 0.5) - 1;
                if(lo > hi && c.at - xa >= DROPOUT_MIN)
                    lo = hi = (int)std::floor((xa + c.at) / 2 - g.xmin);
                for(int col = std::max(lo, 0); col <= std::min(hi, g.width - 1); col++)
                    g.bits[row * g.width + col] = 1;
            }
        }
    }
    /* columns: dropouts in horizontal strokes */
    for(int col = 0; col < g.width; col++)
    {
        double xc = g.xmin + col + 0.5;
        xs.clear();
        for(const Edge &e : edges)
            if((e.x0 <= xc && xc < e.x1) || (e.x1 <= xc && xc < e.x0))
                xs.push_back({ e.y0 + (xc - e.x0) / (e.x1 - e.x0) * (e.y1 - e.y0), e.x1 > e.x0 ? 1 : -1 });
        std::sort(xs.begin(), xs.end());
        int wind = 0;
        double ya = 0;
        for(const Cross &c : xs)
        {
            int was = wind;
            wind += c.dir;
            if(!was && wind)
                ya = c.at;
            else if(was && !wind)
            {
                /* row centres in [ya, c.at) */
                int lo = (int)std::floor(g.ymax - 0.5 - c.at) + 1, hi = (int)std::floor(g.ymax - 0.5 - ya);
                if(lo > hi && c.at - ya >= DROPOUT_MIN)
                {
                    int row = (int)std::floor(g.ymax - (ya + c.at) / 2);
                    if(row >= 0 && row < g.height)
                        g.bits[row * g.width + col] = 1;
                }
            }
        }
    }
}

void put16(std::vector<uint8_t> &v, size_t at, int x)
{
    v[at] = (uint8_t)(x >> 8);
    v[at + 1] = (uint8_t)x;
}
}

Handle Executor::ROMlib_sfnt_strike(Handle sfnt, int ppem, SfntMetrics *metrics)
{
    if(!sfnt || !*sfnt || ppem <= 0 || ppem > 255)
        return nullptr;
    Font f;
    f.r = { (const uint8_t *)*sfnt, (size_t)GetHandleSize(sfnt) };
    if(!f.load())
        return nullptr;
    double scale = (double)ppem / f.upem;
    long hdmx = f.hdmx_record(ppem);

    /* 0..255 and the missing glyph (.notdef) */
    std::vector<Glyph> glyphs(257);
    int first = -1, last = -1;
    for(int c = 0; c <= 256; c++)
    {
        int g = c < 256 ? f.glyph_for(c) : 0;
        if(c < 256 && !g)
            continue;
        Glyph &gl = glyphs[c];
        Outline o;
        if(!glyph_outline(f, g, o, 0))
            continue;
        std::vector<Edge> edges;
        flatten(o, scale, edges);
        rasterize(edges, gl);
        gl.advance = hdmx >= 0 ? (int)f.r.u8(hdmx + 2 + g) : (int)std::lround(f.advance(g) * scale);
        gl.missing = false;
        if(c < 256)
        {
            if(first < 0)
                first = c;
            last = c;
        }
    }
    if(first < 0 || glyphs[256].missing)
        return nullptr;

    /* as 8.6 answers them (fitted on the G3 for Chicago, Charcoal,
       Monaco and Palatino at 9-24): the descent rounds halves down, the
       widest advance up */
    SfntMetrics m;
    m.ascent = std::lround(f.ascender * scale);
    m.descent = (int)std::ceil(-f.descender * scale - 0.5);
    m.leading = std::lround(f.line_gap * scale);
    m.widMax = (int)std::ceil(f.advance_max * scale);
    int ascent = m.ascent, descent = m.descent, kern = 0, total = 0, widmax = 0;
    auto used = [&](int c) { return (c >= first && c <= last) || c == 256; };
    for(int c = 0; c <= 256; c++)
        if(used(c) && !glyphs[c].missing)
        {
            const Glyph &gl = glyphs[c];
            if(gl.width)
            {
                ascent = std::max(ascent, gl.ymax);
                descent = std::max(descent, gl.height - gl.ymax);
                kern = std::min(kern, gl.xmin);
            }
            total += gl.width;
            widmax = std::max(widmax, gl.advance);
        }
    int height = ascent + descent;
    int rowwords = (total + 15) / 16;
    int n = last - first + 3;
    size_t image = 2 * (size_t)rowwords * height;
    size_t size = 26 + image + 4 * n;
    /* the location table and owTLoc are 16-bit */
    if(total > 32767 || (26 + image + 2 * n - 16) / 2 > 32767 || widmax > 255 || height > 255)
        return nullptr;

    std::vector<uint8_t> v(size, 0);
    put16(v, 0, 0x9000); /* proportional font */
    put16(v, 2, first);
    put16(v, 4, last);
    put16(v, 6, widmax);
    put16(v, 8, kern);
    put16(v, 10, -descent);
    put16(v, 12, 0);
    put16(v, 14, height);
    put16(v, 16, (int)((26 + image + 2 * n - 16) / 2));
    put16(v, 18, ascent);
    put16(v, 20, descent);
    put16(v, 22, m.leading);
    put16(v, 24, rowwords);
    size_t loc = 26 + image, ow = loc + 2 * n;
    int x = 0, frectwidth = 0;
    for(int i = 0; i < n - 1; i++)
    {
        int c = i < n - 2 ? first + i : 256;
        const Glyph &gl = glyphs[c];
        put16(v, loc + 2 * i, x);
        if(gl.missing)
        {
            put16(v, ow + 2 * i, -1);
            continue;
        }
        int offset = gl.width ? gl.xmin - kern : 0;
        if(offset > 254)
            return nullptr;
        put16(v, ow + 2 * i, offset << 8 | gl.advance);
        for(int row = 0; row < gl.height; row++)
        {
            int y = ascent - gl.ymax + row;
            for(int col = 0; col < gl.width; col++)
                if(gl.bits[row * gl.width + col])
                    v[26 + (size_t)y * 2 * rowwords + (x + col) / 8] |= 0x80 >> ((x + col) & 7);
        }
        x += gl.width;
        frectwidth = std::max(frectwidth, gl.width + offset);
    }
    put16(v, loc + 2 * (n - 1), x);
    put16(v, ow + 2 * (n - 1), -1);
    put16(v, 12, frectwidth);

    Handle h = NewHandleSys(size);
    if(!h)
        return nullptr;
    memcpy(*h, v.data(), size);
    *metrics = m;
    return h;
}
