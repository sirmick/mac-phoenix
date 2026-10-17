/*
 * text_feed.cpp - every string QuickDraw draws, with where it landed.
 *
 * _StdText ($A882) is the bottleneck under DrawChar, DrawString, DrawText,
 * TextEdit, the Menu, Control and Dialog Managers and most applications.
 * text_feed_install() points its trap table entry at a stub:
 *
 *     EmulOp TEXT_PRE          ; the port, pen, font; the pixels under it
 *     move.w 16(sp),-(sp)      ; count     \
 *     move.l 14(sp),-(sp)      ; textAddr   | the arguments again
 *     move.l 14(sp),-(sp)      ; numer      |
 *     move.l 14(sp),-(sp)      ; denom     /
 *     jsr    original
 *     moveq  #1,d0             ; which patch
 *     EmulOp TEXT_POST         ; which pixels changed; MeasureText
 *     movea.l (sp)+,a0
 *     lea    14(sp),sp
 *     jmp    (a0)
 *
 * CopyBits ($A8EC) and ScrollRect ($A8EF) get the same stub without the
 * pre EmulOp: text moves with the pixels they copy. That is how text drawn
 * offscreen reaches the screen (System 7.5's menu bar, every
 * double-buffering application). Patching the trap table rather than
 * hooking A-line traps in the CPU core catches the ROM's internal callers
 * of StdText and, on the PowerPC machine, native code calling through
 * InterfaceLib, on every core.
 *
 * Two views come out of it. The draw log is a ring of the last draws, on
 * screen or off, for polling with since=N. The screen model is the text on
 * screen now, character by character (MeasureText gives each one's place),
 * in lines (a port and a baseline): a draw keeps the pixels it changed
 * (its ink) and their values; a character counts as there while its ink
 * is. That catches erasing, overdrawing and windows on top without hooking
 * the rest of QuickDraw, a string under a menu comes back when the menu's
 * saved bits are restored, and a redraw replaces just the characters under
 * it (TextEdit draws the character before the one typed, too). Ink that is
 * inverted (InvertRect selection) or whose background changed (hilite
 * colour) is reported as such. A character gone for a while is forgotten.
 *
 * Pixels moved behind the patches' back (ROM code calling QuickDraw
 * directly, native QuickDraw on the PowerPC machine) take their text out
 * of the model until it is drawn again: missing, never wrong. Long text
 * (TextEdit documents) is better read from its own records than from
 * pixels.
 *
 * A character drawn over itself (the same string redrawn in place, the
 * common update case) changes no pixel: the one already in the model
 * stays, or else every pixel of its cell is the check.
 *
 * Coordinates are a surface's own: pixels from its base address, so the
 * main screen's are global coordinates. A port's local point (h, v) is
 * (h - bounds.left, v - bounds.top) on its surface.
 *
 * Layouts (Inside Macintosh: Imaging With QuickDraw, Text):
 *   StdText(count: INTEGER; textAddr: Ptr; numer, denom: Point), Pascal:
 *     sp+4 denom, +8 numer, +12 textAddr, +16 count
 *   CopyBits(srcBits, dstBits: BitMap; srcRect, dstRect: Rect; mode:
 *     INTEGER; maskRgn): sp+4 maskRgn, +8 mode, +10 dstRect, +14 srcRect,
 *     +18 dstBits, +22 srcBits
 *   ScrollRect(r: Rect; dh, dv: INTEGER; updateRgn): sp+4 updateRgn,
 *     +8 dv, +10 dh, +12 r
 *   MeasureText(count: INTEGER; textAddr, charLocs: Ptr): count+1 offsets
 *   BitMap    baseAddr +0, rowBytes +4 (0x8000: a PixMap; 0xC000: a
 *             CGrafPort's portPixMap handle at +0), bounds +6
 *   PixMap    baseAddr +0, rowBytes +4, bounds +6, pixelSize +32
 *   GrafPort  portBits +2, portRect +16, visRgn +24, clipRgn +28,
 *             pnLoc +48, pnVis +66, txFont +68, txFace +70, txSize +74
 *   Region    rgnSize +0, rgnBBox +2
 *   FMOutput  (0x998, after FMSwapFont) italicPixels 0x99F, ascent 0x9A5,
 *             descent 0x9A6
 *   GDevice   (MainDevice 0x8A4) gdPMap +22, gdRect +34
 *   A5 -> QDGlobals: thePort at *(A5)
 */

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "m68k_registers.h"
#include "emul_op.h"
#include "platform.h"
#include "mac_roman.h"
#include "text_feed.h"
#include "guest_bridge.h"

bool text_feed_enabled = true;

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kMaxText = 1024;         /* characters kept per draw */
constexpr size_t kMaxScreen = 2048;    /* lines on the screen */
constexpr size_t kMaxOffscreen = 1024; /* lines in offscreen bitmaps */
constexpr size_t kLogSize = 4096;      /* draws in the log */
constexpr double kForget = 10.0;       /* seconds gone before a character is forgotten */

enum { kStdText = 1, kCopyBits = 2, kScrollRect = 3 };

bool valid(uint32_t p, uint32_t n)
{
    return p >= 0x400 && p < RAMSize && n <= RAMSize - p;
}

/* A pointer from guest memory; 24-bit mode leaves flags in the top byte. */
uint32_t ptr(uint32_t p)
{
    if (!valid(p, 1)) p &= 0x00FFFFFF;
    return valid(p, 1) ? p : 0;
}

uint32_t deref(uint32_t h)
{
    h = ptr(h);
    return h && valid(h, 4) ? ptr(ReadMacInt32(h)) : 0;
}

int16_t s16(uint32_t p) { return (int16_t)ReadMacInt16(p); }

struct Box {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty() const { return x1 <= x0 || y1 <= y0; }
    int area() const { return empty() ? 0 : (x1 - x0) * (y1 - y0); }
    int w() const { return x1 - x0; }
    int h() const { return y1 - y0; }
    Box operator&(const Box &o) const
    {
        return { std::max(x0, o.x0), std::max(y0, o.y0), std::min(x1, o.x1), std::min(y1, o.y1) };
    }
    Box operator|(const Box &o) const
    {
        if (empty()) return o;
        if (o.empty()) return *this;
        return { std::min(x0, o.x0), std::min(y0, o.y0), std::max(x1, o.x1), std::max(y1, o.y1) };
    }
    Box moved(int dx, int dy) const { return { x0 + dx, y0 + dy, x1 + dx, y1 + dy }; }
    bool operator==(const Box &o) const { return x0 == o.x0 && y0 == o.y0 && x1 == o.x1 && y1 == o.y1; }
    bool inside(const Box &o) const { return (*this & o) == *this; }
};

/* A QuickDraw Rect at p, moved by (dx, dy). */
Box rect_at(uint32_t p, int dx, int dy)
{
    return { s16(p + 2) + dx, s16(p) + dy, s16(p + 6) + dx, s16(p + 4) + dy };
}

/* Pixels: the main screen or an offscreen bitmap, (0,0) at base. */
struct Surface {
    uint32_t base = 0, row = 0;
    int depth = 0, w = 0, h = 0;
    bool operator==(const Surface &o) const
    {
        return base == o.base && row == o.row && depth == o.depth && w == o.w && h == o.h;
    }
    bool operator!=(const Surface &o) const { return !(*this == o); }
    Box box() const { return { 0, 0, w, h }; }
};

bool good_depth(int d)
{
    return d == 1 || d == 2 || d == 4 || d == 8 || d == 16 || d == 32;
}

Surface screen()
{
    Surface s;
    uint32_t gd = deref(ReadMacInt32(0x8A4));                 /* MainDevice */
    uint32_t pm = gd ? deref(ReadMacInt32(gd + 22)) : 0;
    Box b;
    if (pm && valid(gd, 42)) {
        s.base = ReadMacInt32(pm);
        s.row = ReadMacInt16(pm + 4) & 0x3FFF;
        s.depth = ReadMacInt16(pm + 32);
        b = rect_at(gd + 34, 0, 0);
    } else {                                               /* no Color QuickDraw */
        uint32_t wm = ptr(ReadMacInt32(0x9DE));            /* WMgrPort */
        if (!wm || !valid(wm, 16)) return s;
        s.base = ReadMacInt32(wm + 2);
        s.row = ReadMacInt16(wm + 6) & 0x3FFF;
        s.depth = 1;
        b = rect_at(wm + 8, 0, 0);
    }
    s.w = b.w();
    s.h = b.h();
    if (s.base != ReadMacInt32(0x824) || !good_depth(s.depth) || s.w <= 0 || s.h <= 0)   /* ScrnBase */
        s.base = 0;
    return s;
}

/* The surface a BitMap, PixMap or port's portBits describes, and its
 * bounds (local coordinates of the surface's top left). base 0: none. */
Surface bits_surface(uint32_t bits, const Surface &scr, int &left, int &top)
{
    Surface s;
    left = top = 0;
    bits = ptr(bits);
    if (!bits || !valid(bits, 14)) return s;
    uint16_t rb = ReadMacInt16(bits + 4);
    uint32_t map = bits;
    if ((rb & 0xC000) == 0xC000) map = deref(ReadMacInt32(bits));   /* CGrafPort */
    Box b;
    if (rb & 0x8000) {
        if (!map || !valid(map, 34)) return s;
        s.base = ReadMacInt32(map);
        s.row = ReadMacInt16(map + 4) & 0x3FFF;
        s.depth = ReadMacInt16(map + 32);
        b = rect_at(map + 6, 0, 0);
    } else {
        s.base = ReadMacInt32(bits);
        s.row = rb & 0x3FFF;
        s.depth = 1;
        b = rect_at(bits + 6, 0, 0);
    }
    left = b.x0;
    top = b.y0;
    /* An old-style GrafPort on a colour screen says depth 1: QuickDraw
     * draws it at the screen's. */
    if (scr.base && s.base == scr.base) return scr;
    s.w = b.w();
    s.h = b.h();
    if (!s.base || !good_depth(s.depth) || s.w <= 0 || s.h <= 0 || s.row * 8 < (uint32_t)s.w * s.depth)
        s.base = 0;
    return s;
}

uint32_t pixel(const Surface &s, int x, int y)
{
    uint32_t row = s.base + (uint32_t)y * s.row;
    switch (s.depth) {
    case 32: return ReadMacInt32(row + 4 * x) & 0x00FFFFFF;
    case 16: return ReadMacInt16(row + 2 * x) & 0x7FFF;
    case 8:  return ReadMacInt8(row + x);
    default: {
        int bit = x * s.depth;
        int shift = 8 - s.depth - (bit & 7);
        return (ReadMacInt8(row + (bit >> 3)) >> shift) & ((1 << s.depth) - 1);
    }
    }
}

uint32_t inverse(const Surface &s, uint32_t v)
{
    switch (s.depth) {
    case 32: return ~v & 0x00FFFFFF;
    case 16: return ~v & 0x7FFF;
    default: return ~v & ((1u << s.depth) - 1);
    }
}

/* 0 white .. 1 black. Indexed: the standard colour tables run white to
 * black; direct: by RGB. */
double darkness(const Surface &s, uint32_t v)
{
    switch (s.depth) {
    case 32: return 1.0 - (((v >> 16) & 0xFF) + ((v >> 8) & 0xFF) + (v & 0xFF)) / 765.0;
    case 16: return 1.0 - (((v >> 10) & 0x1F) + ((v >> 5) & 0x1F) + (v & 0x1F)) / 93.0;
    default: return (double)v / ((1u << s.depth) - 1);
    }
}

void read_box(const Surface &s, const Box &b, std::vector<uint32_t> &out)
{
    out.resize(b.area());
    size_t i = 0;
    for (int y = b.y0; y < b.y1; y++)
        for (int x = b.x0; x < b.x1; x++)
            out[i++] = pixel(s, x, y);
}

std::string cur_app()
{
    int n = ReadMacInt8(0x910);                             /* CurApName */
    std::string s;
    for (int i = 0; i < n && i < 31; i++) s += static_cast<char>(ReadMacInt8(0x911 + i));
    return s;
}

uint32_t the_port(uint32_t a5)
{
    uint32_t qd = valid(a5, 4) ? ptr(ReadMacInt32(a5)) : 0;
    uint32_t port = qd ? ptr(ReadMacInt32(qd)) : 0;
    return port && valid(port, 108) ? port : 0;
}

/* Whether port is one of the drawing process's windows (System 7's
 * WindowList is per process), and its title. */
bool window_title(uint32_t wp, uint32_t port, int depth, std::string &title)
{
    for (int i = 0; (wp = ptr(wp)) && valid(wp, 160) && i < 64; i++) {
        if (s16(wp + 108) == -32767) {                     /* a Layer Manager layer */
            if (depth < 3 && window_title(ReadMacInt32(wp + 140), port, depth + 1, title)) return true;
        } else if (wp == port) {
            uint32_t t = deref(ReadMacInt32(wp + 134));
            if (t) for (int k = 0, n = ReadMacInt8(t); k < n; k++) title += static_cast<char>(ReadMacInt8(t + 1 + k));
            return true;
        }
        wp = ReadMacInt32(wp + 144);
    }
    return false;
}

/* 1: one of the current process's windows, with its title; 2: the Window
 * Manager's port (menus, window frames); 0: anything else. */
int where_is(uint32_t port, std::string &title)
{
    title.clear();
    if (window_title(ReadMacInt32(0x9D6), port, 0, title)) return 1;                 /* WindowList */
    if (port == ptr(ReadMacInt32(0x9DE)) || port == ptr(ReadMacInt32(0xD2C))) return 2;    /* WMgr(C)Port */
    return 0;
}

/* A region, moved by (dx, dy): its bounding box and, unless it is a
 * rectangle, its inversion points by line. A point is inside if an odd
 * number of them are above and left of it (or on). */
struct Rgn {
    Box bbox;
    std::vector<std::pair<int, std::vector<int>>> lines;
    bool contains(int x, int y) const
    {
        if (x < bbox.x0 || x >= bbox.x1 || y < bbox.y0 || y >= bbox.y1) return false;
        if (lines.empty()) return true;
        int n = 0;
        for (auto &[v, xs] : lines) {
            if (v > y) break;
            for (int h : xs) n += h <= x;
        }
        return n & 1;
    }
};

Rgn read_rgn(uint32_t r, int dx, int dy)
{
    Rgn g;
    g.bbox = rect_at(r + 2, dx, dy);
    int size = ReadMacInt16(r);
    if (size <= 10 || !valid(r, size)) return g;
    for (uint32_t p = r + 10, end = r + size; p + 2 <= end;) {
        int v = s16(p);
        p += 2;
        if (v == 32767) break;
        std::vector<int> xs;
        while (p + 2 <= end) {
            int h = s16(p);
            p += 2;
            if (h == 32767) break;
            xs.push_back(h + dx);
        }
        g.lines.push_back({ v + dy, std::move(xs) });
    }
    return g;
}

/* A StdText call in progress (the stub's pre EmulOp seen, post not yet). */
struct Pending {
    uint32_t sp = 0, port = 0, text_addr = 0;
    std::string text, app;
    int pen_h = 0, pen_v = 0, dx = 0, dy = 0;
    int font = 0, size = 0, face = 0;
    Surface surf;
    bool onscreen = false;
    Rgn vis, clip_rgn;                  /* where it can draw */
    Box box;                            /* ... around the pen: clip and estimate */
    std::vector<uint32_t> before;       /* box's pixels before the draw */
};

/* A character on a surface: its cell, and the cell's pixels after the draw.
 * (A whole string when it couldn't be measured.) */
struct Cell {
    std::string s;
    bool blank = false;                 /* a space: nothing to check it by */
    uint32_t seq = 0;
    int font = 0, size = 0, face = 0;
    Box box;
    std::vector<uint32_t> pixels;
    std::vector<uint8_t> ink;           /* 1: this draw set the pixel */
    int n_ink = 0;
    uint32_t ink_value = 0;             /* the commonest ink pixel */
    bool light = false;                 /* drawn light on dark (selected) */
    bool whole = false;                 /* nothing changed: check every pixel */
    bool gone = false;
    Clock::time_point gone_since{};
};

/* The characters a port drew on one baseline, left to right. */
struct Line {
    Surface surf;
    uint32_t port = 0;
    int where = 0;
    std::string title, app;
    int baseline = 0;
    std::vector<Cell> cells;
};

struct LogEntry {
    uint32_t seq = 0;
    std::string text, app;
    int font = 0, size = 0, face = 0;
    bool onscreen = false;
    Box rect;
};

std::vector<Pending> g_pending;         /* CPU thread only */
std::mutex g_mutex;                     /* the rest: CPU thread and the API */
std::deque<Line> g_screen, g_off;
std::deque<LogEntry> g_log;
uint32_t g_seq = 0;
uint32_t g_screen_base = 0;
uint32_t g_table = 0;                   /* the Toolbox trap table, 0: not found */
uint32_t g_measure = 0;                 /* 68k: MeasureText(d0, a0, a1) */
uint32_t g_locs = 0;                    /* its charLocs */

std::deque<Line> &store(const Surface &s)
{
    return s.base == g_screen_base ? g_screen : g_off;
}

/* How a character stands now. inverted: its pixels are, against the draw. */
struct State { bool visible = false, inverted = false, highlighted = false; };

State check(const Cell &c, const Surface &now, int dx = 0, int dy = 0)
{
    State st;
    Box at = c.box.moved(dx, dy);
    if (c.blank || c.box.empty() || !at.inside(now.box())) return st;
    std::vector<uint32_t> px;
    read_box(now, at, px);
    size_t n_ink = 0, same = 0, inv = 0, n_bg = 0, bg_same = 0, bg_inv = 0;
    std::unordered_map<uint32_t, int> bg_now;
    for (size_t i = 0; i < px.size(); i++) {
        if (c.ink[i]) {
            n_ink++;
            if (px[i] == c.pixels[i]) same++;
            else if (px[i] == inverse(now, c.pixels[i])) inv++;
        } else {
            n_bg++;
            if (px[i] == c.pixels[i]) bg_same++;
            else if (px[i] == inverse(now, c.pixels[i])) bg_inv++;
            bg_now[px[i]]++;
        }
    }
    if (!n_ink) return st;
    /* A stray pixel in 50 (a neighbour's overhang, the caret at the edge). */
    size_t need = n_ink - std::max<size_t>(1, n_ink / 50);
    /* The ink alone could land on something else's pixels by chance: the
     * background must be there too, or have gone one colour that isn't the
     * ink's (hilite colour behind the text), or, inverted, be inverted. */
    uint32_t bg_value = 0;
    int bg_most = 0;
    for (auto &[v, k] : bg_now)
        if (k > bg_most) { bg_most = k; bg_value = v; }
    /* One column may have changed whole: TextEdit's caret beside the
     * character last typed. */
    bool bg_there = !n_bg || bg_same + c.box.h() >= 0.6 * n_bg;
    bool bg_recoloured = n_bg && bg_most >= 0.9 * n_bg && bg_value != c.ink_value;
    if (same >= need && (bg_there || bg_recoloured)) {
        st.visible = true;
        st.highlighted = !bg_there;
    } else if (inv >= need && (!n_bg || bg_inv >= 0.6 * n_bg)) {
        st.visible = st.inverted = true;
    }
    return st;
}

/* Of a line's characters with ink, how many are there, (dx, dy) away. */
int there(const Line &l, const Surface &now, int dx, int dy, int &inked)
{
    int n = 0;
    inked = 0;
    for (const Cell &c : l.cells) {
        if (c.blank) continue;
        inked++;
        n += check(c, now, dx, dy).visible;
    }
    return n;
}

Line *find_line(std::deque<Line> &lines, const Surface &surf, uint32_t port, int baseline)
{
    for (Line &l : lines)
        if (l.surf == surf && l.port == port && l.baseline == baseline) return &l;
    return nullptr;
}

/* Put cells into line l: each replaces the characters it mostly covers. */
void put_cells(Line &l, std::vector<Cell> &&cells)
{
    for (Cell &n : cells) {
        auto &v = l.cells;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const Cell &o) {
                    int ov = std::min(o.box.x1, n.box.x1) - std::max(o.box.x0, n.box.x0);
                    int w = std::min(o.box.w(), n.box.w());
                    return ov > 0 && (w <= 0 || ov * 2 >= w);
                }),
                v.end());
        auto at = std::upper_bound(v.begin(), v.end(), n.box.x0,
                                   [](int x, const Cell &c) { return x < c.box.x0; });
        v.insert(at, std::move(n));
    }
}

/* Forget characters gone for a while (all: gone now), and empty lines. */
void sweep(std::deque<Line> &lines, const Surface &now, bool all)
{
    auto t = Clock::now();
    for (Line &l : lines) {
        if (l.surf != now) { l.cells.clear(); continue; }
        for (Cell &c : l.cells) {
            if (c.blank) continue;
            if (check(c, now).visible) { c.gone = false; continue; }
            if (!c.gone) { c.gone = true; c.gone_since = t; }
        }
        auto &v = l.cells;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const Cell &c) {
                    return !c.blank && c.gone &&
                           (all || std::chrono::duration<double>(t - c.gone_since).count() > kForget);
                }),
                v.end());
        /* Spaces go with the characters around them. */
        bool inked = std::any_of(v.begin(), v.end(), [](const Cell &c) { return !c.blank; });
        if (!inked) v.clear();
    }
    lines.erase(std::remove_if(lines.begin(), lines.end(), [](const Line &l) { return l.cells.empty(); }),
                lines.end());
}

void add_line(Line &&n)
{
    std::deque<Line> &lines = store(n.surf);
    Line *l = find_line(lines, n.surf, n.port, n.baseline);
    if (l) {
        put_cells(*l, std::move(n.cells));
        l->app = n.app;
        if (n.where) { l->where = n.where; l->title = n.title; }
    } else {
        lines.push_back(std::move(n));
    }
    size_t cap = &lines == &g_screen ? kMaxScreen : kMaxOffscreen;
    if (lines.size() > cap && &lines == &g_screen) sweep(lines, lines.back().surf, true);
    while (lines.size() > cap) lines.pop_front();
}

/* The part of l in clip, moved by (dx, dy) onto surface to. */
bool moved_line(const Line &l, const Box &clip, int dx, int dy, const Surface &to, Line &out)
{
    out = Line();
    out.surf = to;
    out.port = l.port;
    out.where = l.where;
    out.title = l.title;
    out.app = l.app;
    out.baseline = l.baseline + dy;
    for (const Cell &c : l.cells) {
        Box b = c.box.moved(dx, dy);
        if (!c.box.inside(clip) || !b.inside(to.box())) continue;
        Cell m = c;
        m.box = b;
        m.gone = false;
        out.cells.push_back(std::move(m));
    }
    return std::any_of(out.cells.begin(), out.cells.end(), [](const Cell &c) { return !c.blank; });
}

/* Most of a line's inked characters are there. */
bool mostly_there(const Line &l, const Surface &now)
{
    int inked, n = there(l, now, 0, 0, inked);
    return inked && n * 10 >= inked * 9;
}

/* Pixels copied from src (srcR) to dst (dstR): so is the text in them.
 * from_src: moved rather than copied (ScrollRect, CopyBits on the screen
 * itself, which is how TextEdit scrolls): the text leaves srcR, and
 * whether it was there can't be told any more where the two overlap. */
void copied(const Surface &src, const Box &srcR, const Surface &dst, const Box &dstR, uint32_t port, bool from_src)
{
    if (!src.base || !dst.base || src.depth != dst.depth || srcR.w() != dstR.w() || srcR.h() != dstR.h())
        return;
    int dx = dstR.x0 - srcR.x0, dy = dstR.y0 - srcR.y0;
    std::deque<Line> &lines = store(src);
    std::vector<std::pair<Line *, Line>> moved;
    for (Line &l : lines) {
        Line m;
        if (l.surf == src && moved_line(l, srcR, dx, dy, dst, m) && (from_src || mostly_there(l, src)))
            moved.push_back({ &l, std::move(m) });
    }
    /* Only what arrived (not clipped, nor changed by a transfer mode) left. */
    std::vector<Line> arrived;
    for (auto &[from, m] : moved) {
        if (!mostly_there(m, dst)) continue;
        if (from_src) {
            auto &v = from->cells;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const Cell &c) { return c.box.inside(srcR); }), v.end());
        }
        if (m.where == 0 && port && !from_src)  /* drawn offscreen: the copy says where it is */
            m.where = where_is(port, m.title);
        if (m.where == 0) m.port = port ? port : m.port;
        arrived.push_back(std::move(m));
    }
    lines.erase(std::remove_if(lines.begin(), lines.end(), [](const Line &l) { return l.cells.empty(); }),
                lines.end());
    for (Line &m : arrived) add_line(std::move(m));
}

/* Each character's left edge from the pen (MeasureText), n + 1 of them;
 * scaled to the pen's advance. Empty if it can't be had. a5: the drawing
 * application's (Execute68k runs with the registers it is given). */
std::vector<int> measure(const Pending &d, int advance, uint32_t a5)
{
    std::vector<int> locs;
    int n = (int)d.text.size();
    if (!g_measure || !d.text_addr || n > kMaxText) return locs;
    M68kRegisters r = {};
    r.d[0] = n;
    r.a[0] = d.text_addr;
    r.a[1] = g_locs;
    r.a[5] = a5;
    Execute68k(g_measure, &r);
    locs.resize(n + 1);
    for (int i = 0; i <= n; i++) locs[i] = s16(g_locs + 2 * i);
    int total = locs[n];
    if (total <= 0 || locs[0] != 0) return {};
    if (advance > 0 && std::abs(total - advance) > 1)
        for (int &x : locs) x = x * advance / total;
    return locs;
}

void post_stdtext(uint32_t sp, uint32_t a5)
{
    while (!g_pending.empty() && g_pending.back().sp < sp)
        g_pending.pop_back();
    if (g_pending.empty() || g_pending.back().sp != sp) return;
    Pending d = std::move(g_pending.back());
    g_pending.pop_back();
    if (!d.port) return;

    int width = s16(d.port + 50) - d.pen_h;
    int asc = ReadMacInt8(0x9A5), desc = ReadMacInt8(0x9A6), ital = ReadMacInt8(0x99F);
    int sz = d.size > 0 ? d.size : 12;
    if (asc <= 0 || asc > 127) { asc = sz; desc = sz / 3; }
    if (desc > 127) desc = sz / 3;
    if (ital > 32) ital = 0;
    int x = d.pen_h + d.dx, y = d.pen_v + d.dy;
    Box rect = { std::min(x, x + width), y - asc, std::max(x, x + width) + ital, y + desc };
    /* Measured before the lock: MeasureText runs 68k code. */
    std::vector<int> locs;
    Box r = rect & d.box;
    if (d.surf.base && !r.empty() && width > 0) locs = measure(d, width, a5);

    std::lock_guard<std::mutex> lock(g_mutex);
    LogEntry e;
    e.seq = ++g_seq;
    e.text = d.text;
    e.app = d.app;
    e.font = d.font; e.size = d.size; e.face = d.face;
    e.onscreen = d.onscreen;
    e.rect = d.onscreen && !r.empty() ? r : rect;
    g_log.push_back(e);
    while (g_log.size() > kLogSize) g_log.pop_front();
    if (!d.surf.base || r.empty()) return;

    /* What changed: the ink. The commonest unchanged value: the background. */
    std::vector<uint32_t> after;
    read_box(d.surf, r, after);
    std::vector<uint8_t> ink(after.size(), 0);
    std::unordered_map<uint32_t, int> bg;
    int bw = d.box.w();
    for (int yy = r.y0, i = 0; yy < r.y1; yy++)
        for (int xx = r.x0; xx < r.x1; xx++, i++) {
            if (after[i] != d.before[(size_t)(yy - d.box.y0) * bw + (xx - d.box.x0)]) ink[i] = 1;
            else bg[after[i]]++;
        }
    uint32_t bg_value = 0, ink_value = 0;
    int best = -1;
    for (auto &[v, c] : bg)
        if (c > best) { best = c; bg_value = v; }
    {
        std::unordered_map<uint32_t, int> inks;
        for (size_t i = 0; i < after.size(); i++)
            if (ink[i]) inks[after[i]]++;
        best = -1;
        for (auto &[v, c] : inks)
            if (c > best) { best = c; ink_value = v; }
    }
    /* Light on dark: selected. The draw's commonest ink against its background. */
    bool light = best > 0 && !bg.empty() && darkness(d.surf, ink_value) < darkness(d.surf, bg_value);

    Line line;
    line.surf = d.surf;
    line.port = d.port;
    line.where = where_is(d.port, line.title);
    line.app = d.app;
    line.baseline = y;
    std::deque<Line> &lines = store(d.surf);
    Line *old = find_line(lines, d.surf, d.port, y);
    /* One cell per character; unmeasured, the string is one. */
    int n = locs.empty() ? 1 : (int)d.text.size();
    for (int k = 0; k < n; k++) {
        Cell c;
        c.s = locs.empty() ? d.text : d.text.substr(k, 1);
        c.seq = e.seq;
        c.font = d.font; c.size = d.size; c.face = d.face;
        if (locs.empty()) c.box = r;
        else c.box = Box{ x + locs[k], r.y0, k == n - 1 ? std::max(x + locs[k + 1], rect.x1) : x + locs[k + 1], r.y1 } & r;
        if (c.box.empty()) continue;            /* zero width (a return), or clipped */
        /* Behind another window: visRgn left it out, nothing was drawn. */
        int mx = (c.box.x0 + c.box.x1) / 2, my = (c.box.y0 + c.box.y1) / 2;
        if ((!d.vis.bbox.empty() && !d.vis.contains(mx, my)) ||
            (!d.clip_rgn.bbox.empty() && !d.clip_rgn.contains(mx, my)))
            continue;
        c.pixels.resize(c.box.area());
        c.ink.resize(c.box.area());
        std::unordered_map<uint32_t, int> values;
        for (int yy = c.box.y0, i = 0; yy < c.box.y1; yy++)
            for (int xx = c.box.x0; xx < c.box.x1; xx++, i++) {
                size_t j = (size_t)(yy - r.y0) * r.w() + (xx - r.x0);
                c.pixels[i] = after[j];
                c.ink[i] = ink[j];
                if (ink[j]) { c.n_ink++; values[after[j]]++; }
            }
        unsigned char ch = c.s[0];
        c.blank = c.s.size() == 1 && (ch == ' ' || ch == 0xCA || (ch < 0x20 && (ch < 0x11 || ch > 0x14)));
        if (!c.blank && c.n_ink < 3) {
            /* Drawn over itself: the character already here stays. (A pixel
             * or two is a caret or an overhang, too few to tell it by.) */
            bool kept = false;
            if (old)
                for (Cell &o : old->cells)
                    if (o.s == c.s && std::abs(o.box.x0 - c.box.x0) <= 1 && check(o, d.surf).visible) {
                        o.seq = e.seq;
                        kept = true;
                    }
            if (kept) continue;
            c.whole = true;
            std::fill(c.ink.begin(), c.ink.end(), 1);
            c.n_ink = (int)c.ink.size();
            values.clear();
            for (uint32_t v : c.pixels) values[v]++;
        }
        best = -1;
        for (auto &[v, cnt] : values)
            if (cnt > best) { best = cnt; c.ink_value = v; }
        c.light = light;
        line.cells.push_back(std::move(c));
    }
    if (!line.cells.empty()) add_line(std::move(line));
}

void post_copybits(uint32_t sp, uint32_t a5)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_screen.empty() && g_off.empty()) return;
    Surface scr = screen();
    if (scr.base != g_screen_base) return;
    int sl, st, dl, dt;
    Surface src = bits_surface(ReadMacInt32(sp + 22), scr, sl, st);
    Surface dst = bits_surface(ReadMacInt32(sp + 18), scr, dl, dt);
    uint32_t sr = ptr(ReadMacInt32(sp + 14)), dr = ptr(ReadMacInt32(sp + 10));
    if (!src.base || !dst.base || !sr || !dr) return;
    Box full = rect_at(sr, -sl, -st);
    Box srcR = full & src.box();
    Box dstR = rect_at(dr, -dl, -dt);
    /* Clipped by the source's edge: the destination shrinks with it. */
    dstR = { dstR.x0 + (srcR.x0 - full.x0), dstR.y0 + (srcR.y0 - full.y0),
             dstR.x1 - (full.x1 - srcR.x1), dstR.y1 - (full.y1 - srcR.y1) };
    copied(src, srcR, dst, dstR, the_port(a5), src == dst);
}

void post_scrollrect(uint32_t sp, uint32_t a5)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    uint32_t port = the_port(a5), rp = ptr(ReadMacInt32(sp + 12));
    if (!port || !rp) return;
    Surface scr = screen();
    if (scr.base != g_screen_base) return;
    int left, top;
    Surface s = bits_surface(port + 2, scr, left, top);
    if (!s.base) return;
    Box r = rect_at(rp, -left, -top) & s.box();
    int dh = s16(sp + 10), dv = s16(sp + 8);
    copied(s, r, s, r.moved(dh, dv), port, true);
}

std::string utf8(const std::string &mac)
{
    /* The system font's control characters: command, check, diamond, apple. */
    std::string u;
    for (char c : mac) {
        switch (c) {
        case 0x11: u += "⌘"; break;
        case 0x12: u += "✓"; break;
        case 0x13: u += "◆"; break;
        case 0x14: u += ""; break;
        default:   u += mac_roman_to_utf8(std::string(1, c));
        }
    }
    return u;
}

void json_str(std::string &out, const std::string &mac)
{
    std::string u = utf8(mac);
    out += '"';
    for (unsigned char c : u) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\r' || c == '\n') out += "\\n";
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; }
        else out += c;
    }
    out += '"';
}

void json_box(std::string &out, const Box &b)
{
    char t[64];
    snprintf(t, sizeof t, "[%d,%d,%d,%d]", b.x0, b.y0, b.x1, b.y1);
    out += t;
}

uint32_t tool_trap(uint16_t trap)
{
    M68kRegisters r = {};
    r.d[0] = trap;
    Execute68kTrap(0xA746, &r);                             /* GetToolTrapAddress */
    return r.a[0];
}

/* The Toolbox trap table: where entries $80..$8F hold what GetToolTrapAddress
 * says they do (most of them: the Process Manager's swapping can leave one
 * or two apart), StdText's among them. $E00 on the 68k machines; searched
 * for elsewhere. */
uint32_t tool_table()
{
    uint32_t want[16];
    for (int k = 0; k < 16; k++) want[k] = tool_trap(0xA880 + k);
    auto at = [&](uint32_t t) {
        if (ReadMacInt32(t + 4 * 0x82) != want[2]) return false;
        int same = 0;
        for (int k = 0; k < 16; k++) same += ReadMacInt32(t + 4 * (0x80 + k)) == want[k];
        return same >= 12;
    };
    if (at(0xE00)) return 0xE00;
    for (uint32_t t = 0; t + 0x1000 <= RAMSize; t += 4)
        if (at(t)) return t;
    return 0;
}

uint32_t new_ptr_sys(uint32_t size)
{
    M68kRegisters r = {};
    r.d[0] = size;
    Execute68kTrap(0xA71E, &r);                             /* NewPtrSysClear */
    return r.a[0];
}

/* Point trap at a stub: [EmulOp TEXT_PRE,] the arguments (args bytes)
 * pushed again, jsr the original, moveq #which,d0, EmulOp TEXT_POST, pop
 * the arguments, return.
 *
 * Into the trap table directly where it was found: System 7's Process
 * Manager takes a SetToolTrapAddress made while an application is current
 * (PatchAfterStartup runs in the Finder's time) for that application's
 * patch, and swaps it out whenever another one runs. */
bool patch(uint32_t table, uint16_t trap, int args, bool pre, int which)
{
    uint32_t orig = tool_trap(trap);
    if (!orig) return false;
    uint32_t stub = new_ptr_sys(64);
    if (!stub) return false;
    uint32_t p = stub;
    auto w16 = [&](uint16_t v) { WriteMacInt16(p, v); p += 2; };
    if (pre) w16(platform_make_emulop(M68K_EMUL_OP_TEXT_PRE));
    /* The arguments are at 4(sp)..4+args(sp); copying from the top down,
     * each word or long to push is at args(sp) once the odd word is done. */
    if (args & 2) { w16(0x3F2F); w16(args + 2); }           /* move.w args+2(sp),-(sp) */
    for (int i = 0; i < args / 4; i++) { w16(0x2F2F); w16(args); }   /* move.l args(sp),-(sp) */
    w16(M68K_JSR); w16(orig >> 16); w16(orig & 0xFFFF);
    w16(0x7000 | which);                                    /* moveq #which,d0 */
    w16(platform_make_emulop(M68K_EMUL_OP_TEXT_POST));
    w16(0x205F);                                            /* movea.l (sp)+,a0 */
    w16(0x4FEF); w16(args);                                 /* lea args(sp),sp */
    w16(M68K_JMP_A0);
    if (table) {
        WriteMacInt32(table + 4 * (trap & 0x3FF), stub);
    } else {
        M68kRegisters r = {};
        r.a[0] = stub;
        r.d[0] = trap;
        Execute68kTrap(0xA647, &r);                         /* SetToolTrapAddress */
    }
    fprintf(stderr, "[text] %04x patched%s: stub %08x, original %08x\n", trap,
            table ? "" : " (SetToolTrapAddress: this application only)", stub, orig);
    return true;
}

}  // namespace

void text_feed_install()
{
    if (!text_feed_enabled) return;
    g_pending.clear();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_screen.clear();
        g_off.clear();
    }
    /* move.w d0,-(sp); move.l a0,-(sp); move.l a1,-(sp); _MeasureText; rts */
    g_measure = new_ptr_sys(16);
    g_locs = new_ptr_sys(2 * (kMaxText + 2));
    if (!g_measure || !g_locs) g_measure = 0;
    else {
        static const uint16_t code[] = { 0x3F00, 0x2F08, 0x2F09, 0xA837, M68K_RTS };
        for (int i = 0; i < 5; i++) WriteMacInt16(g_measure + 2 * i, code[i]);
    }
    uint32_t table = g_table = tool_table();
    fprintf(stderr, "[text] Toolbox trap table at %08x\n", table);
    if (patch(table, 0xA882, 14, true, kStdText)) {
        patch(table, 0xA8EC, 22, false, kCopyBits);
        patch(table, 0xA8EF, 12, false, kScrollRect);
    }
}

void bridge_trap_install()
{
    uint16_t trap = guest_bridge::kTrap;
    uint32_t table = g_table ? g_table : tool_table();
    uint32_t unimpl = tool_trap(0xA89F);                    /* _Unimplemented */
    uint32_t cur = tool_trap(trap);
    if (!table || cur != unimpl) {
        fprintf(stderr, "[bridge2] trap %04x not installed: table %08x, entry %08x, unimplemented %08x\n",
                trap, table, cur, unimpl);
        return;
    }
    uint32_t stub = new_ptr_sys(8);
    if (!stub) return;
    WriteMacInt16(stub, platform_make_emulop(M68K_EMUL_OP_BRIDGE_COMM));
    WriteMacInt16(stub + 2, M68K_RTS);
    WriteMacInt32(table + 4 * (trap & 0x3FF), stub);
    fprintf(stderr, "[bridge2] trap %04x installed: stub %08x\n", trap, stub);
}

void text_feed_pre(uint32_t sp, uint32_t a5)
{
    while (!g_pending.empty() && g_pending.back().sp <= sp) /* left by a longjmp */
        g_pending.pop_back();
    Pending d;
    d.sp = sp;
    d.port = the_port(a5);
    int count = s16(sp + 16);
    uint32_t text = ptr(ReadMacInt32(sp + 12));
    if (d.port && count > 0 && text) {
        int n = std::min<int>(count, kMaxText);
        if (valid(text, n)) {
            for (int i = 0; i < n; i++) d.text += static_cast<char>(ReadMacInt8(text + i));
            if (n == count) d.text_addr = text;
        }
    }
    if (d.text.empty() || s16(d.port + 66) < 0) {          /* nothing, or pen hidden */
        d.port = 0;
        g_pending.push_back(std::move(d));
        return;
    }
    d.app = cur_app();
    d.pen_v = s16(d.port + 48);
    d.pen_h = s16(d.port + 50);
    d.font = ReadMacInt16(d.port + 68);
    d.face = ReadMacInt8(d.port + 70);
    d.size = ReadMacInt16(d.port + 74);
    Surface scr = screen();
    if (scr.base != g_screen_base) {                        /* new screen: the model is void */
        std::lock_guard<std::mutex> lock(g_mutex);
        g_screen.clear();
        g_screen_base = scr.base;
    }
    int left, top;
    d.surf = bits_surface(d.port + 2, scr, left, top);
    d.dx = -left;
    d.dy = -top;
    d.onscreen = scr.base && d.surf.base == scr.base;
    if (d.surf.base) {
        Box clip = rect_at(d.port + 16, d.dx, d.dy) & d.surf.box();         /* portRect */
        uint32_t vis = deref(ReadMacInt32(d.port + 24)), cl = deref(ReadMacInt32(d.port + 28));
        if (vis) { d.vis = read_rgn(vis, d.dx, d.dy); clip = clip & d.vis.bbox; }
        if (cl) { d.clip_rgn = read_rgn(cl, d.dx, d.dy); clip = clip & d.clip_rgn.bbox; }
        /* Generous: a character is rarely wider than 1.5 x its size. */
        int sz = d.size > 0 ? d.size : 12;
        int nh = s16(sp + 10), nv = s16(sp + 8), dh = s16(sp + 6), dv = s16(sp + 4);
        int scale = 1;
        if (dh > 0 && nh > dh) scale = std::max(scale, (nh + dh - 1) / dh);
        if (dv > 0 && nv > dv) scale = std::max(scale, (nv + dv - 1) / dv);
        if (scale > 1) d.text_addr = 0;                     /* MeasureText measures 1:1 */
        int x = d.pen_h + d.dx, y = d.pen_v + d.dy;
        long w = ((long)d.text.size() * (sz + sz / 2) + sz) * scale;
        Box est = { x - sz, y - (sz * 3 / 2 + 2) * scale, (int)std::min<long>(x + w, 32767),
                    y + (sz / 2 + 2) * scale };
        d.box = clip & est;
        if (!d.box.empty()) read_box(d.surf, d.box, d.before);
    }
    g_pending.push_back(std::move(d));
}

void text_feed_post(uint32_t sp, uint32_t a5, int which)
{
    switch (which) {
    case kStdText:    post_stdtext(sp, a5); break;
    case kCopyBits:   post_copybits(sp, a5); break;
    case kScrollRect: post_scrollrect(sp, a5); break;
    }
}

std::string text_feed_json(const std::string &query)
{
    auto param = [&](const char *key) -> std::string {
        std::string k = std::string(key) + "=";
        for (size_t pos = 0; pos < query.size();) {
            size_t end = query.find('&', pos);
            if (end == std::string::npos) end = query.size();
            if (query.compare(pos, k.size(), k) == 0) return query.substr(pos + k.size(), end - pos - k.size());
            pos = end + 1;
        }
        return "";
    };
    bool debug = param("debug") == "1";
    std::string out = "{\"app\":";
    json_str(out, cur_app());
    std::lock_guard<std::mutex> lock(g_mutex);
    char b[200];
    snprintf(b, sizeof b, ",\"seq\":%u,\"enabled\":%s", g_seq, text_feed_enabled ? "true" : "false");
    out += b;
    if (debug && g_table) {                                 /* StdText, CopyBits, ScrollRect */
        snprintf(b, sizeof b, ",\"trap_table\":[\"%08x\",\"%08x\",\"%08x\",\"%08x\"]", g_table,
                 ReadMacInt32(g_table + 4 * 0x82), ReadMacInt32(g_table + 4 * 0xEC), ReadMacInt32(g_table + 4 * 0xEF));
        out += b;
    }

    std::string since = param("since");
    if (!since.empty()) {
        uint32_t after = (uint32_t)strtoul(since.c_str(), nullptr, 10);
        out += ",\"draws\":[";
        bool first = true;
        for (auto &e : g_log) {
            if (e.seq <= after) continue;
            if (!first) out += ',';
            first = false;
            snprintf(b, sizeof b, "{\"seq\":%u,\"onscreen\":%s,\"font\":%d,\"size\":%d,\"face\":%d,\"text\":",
                     e.seq, e.onscreen ? "true" : "false", e.font, e.size, e.face);
            out += b;
            json_str(out, e.text);
            out += ",\"app\":";
            json_str(out, e.app);
            out += ",\"rect\":";
            json_box(out, e.rect);
            out += '}';
        }
        out += "]}";
        return out;
    }

    Surface now = screen();
    if (now.base == g_screen_base) sweep(g_screen, now, false);
    else g_screen.clear();

    /* A line's characters as strings: split where one is gone, at a gap
     * wider than half the size, or where selection changes. */
    struct Item {
        const Line *line;
        std::string text;
        Box rect;
        uint32_t seq = 0;
        int font = 0, size = 0, face = 0;
        bool inverted = false, highlighted = false;
        int inked = 0;                      /* characters checked by their ink */
    };
    std::vector<Item> items;
    for (const Line &l : g_screen) {
        Item it;
        bool open = false;
        std::string spaces;
        Box space_box;
        /* Characters checked whole (a dot: too little ink to go by) count
         * only beside ones checked by their ink: alone, any blank patch
         * would pass for one. */
        auto close = [&]() {
            if (open && it.inked) items.push_back(it);
            open = false;
            spaces.clear();
            space_box = Box();
        };
        for (const Cell &c : l.cells) {
            if (c.blank) {
                if (open) { spaces += ' '; space_box = space_box | c.box; }
                continue;
            }
            State st = check(c, now);
            if (!st.visible) { close(); continue; }
            bool inv = c.light != st.inverted;
            int gap = open ? c.box.x0 - it.rect.x1 - space_box.w() : 0;
            /* ... and take the string's selection. */
            if (open && ((!c.whole && it.inked && (inv != it.inverted || st.highlighted != it.highlighted)) ||
                         gap > std::max(c.size > 0 ? c.size : 12, 12) / 2))
                close();
            if (!open) {
                it = Item();
                it.line = &l;
                it.font = c.font; it.size = c.size; it.face = c.face;
                it.inverted = inv;
                it.highlighted = st.highlighted;
                open = true;
            } else {
                if (spaces.empty() && gap >= 2) spaces = " ";
                it.text += spaces;
                it.rect = it.rect | space_box;
            }
            spaces.clear();
            space_box = Box();
            if (!c.whole && !it.inked++) {
                it.inverted = inv;
                it.highlighted = st.highlighted;
            }
            it.text += c.s;
            it.rect = it.rect | c.box;
            it.seq = std::max(it.seq, c.seq);
        }
        close();
    }
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        if (a.line->baseline != b.line->baseline) return a.line->baseline < b.line->baseline;
        return a.rect.x0 < b.rect.x0;
    });

    snprintf(b, sizeof b, ",\"screen\":[%d,%d],\"text\":[", now.w, now.h);
    out += b;
    bool first = true;
    for (const Item &it : items) {
        const Line &l = *it.line;
        if (!first) out += ',';
        first = false;
        out += "{\"text\":";
        json_str(out, it.text);
        out += ",\"rect\":";
        json_box(out, it.rect);
        snprintf(b, sizeof b, ",\"baseline\":%d,\"font\":%d,\"size\":%d,\"face\":%d,\"seq\":%u", l.baseline,
                 it.font, it.size, it.face, it.seq);
        out += b;
        if (it.inverted) out += ",\"inverted\":true";       /* looks selected */
        if (it.highlighted) out += ",\"highlighted\":true";
        if (l.where == 1) {
            out += ",\"window\":";
            json_str(out, l.title);
        } else if (l.where == 2) {
            out += ",\"port\":\"screen\"";
        }
        out += ",\"app\":";
        json_str(out, l.app);
        if (debug) {
            const Cell *c = nullptr;
            for (const Cell &k : l.cells)
                if (k.box.x0 >= it.rect.x0 && !k.blank) { c = &k; break; }
            snprintf(b, sizeof b, ",\"debug\":{\"cells\":%zu,\"port\":\"%08x\",\"first\":{\"whole\":%d,\"ink\":%d,\"of\":%zu,\"light\":%d,\"ink_value\":%u}}",
                     l.cells.size(), l.port, c ? c->whole : -1, c ? c->n_ink : -1, c ? c->pixels.size() : 0, c ? c->light : -1,
                     c ? c->ink_value : 0);
            out += b;
        }
        out += '}';
    }
    out += "]}";
    return out;
}
