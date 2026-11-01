/* MacPhoenix: high-resolution rendering, recording side (hidpi.h). Runs on
   the emulator thread, inside StdText; compose() on the front end's render
   thread. Nothing here changes what QuickDraw draws at 1x. */

#include <base/common.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <FontMgr.h>
#include <MemoryMgr.h>
#include <ResourceMgr.h>

#include <quickdraw/cquick.h>
#include <quickdraw/region.h>
#include <quickdraw/font.h>
#include <quickdraw/sfnt.h>
#include <quickdraw/gworld.h>
#include <vdriver/vdriver.h>
#include <error/error.h>

#include <hidpi/hidpi.h>
#include <hidpi/hidpi_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <tuple>

using namespace Executor;
using namespace Executor::hidpi::impl;

namespace
{
double g_scale = 1.0;

/* The runs on the screen, oldest first: drawn in that order. */
std::mutex g_mutex;
Runs g_runs;
enum
{
    MAX_RUNS = 4096,
    MAX_BOX_PIXELS = 1 << 20,
};

bool intersects(const IRect &a, const IRect &b, int slack = 0)
{
    return a.left < b.right + slack && b.left < a.right + slack
        && a.top < b.bottom + slack && b.top < a.bottom + slack;
}

IRect intersection(const IRect &a, const IRect &b)
{
    return { std::max(a.top, b.top), std::max(a.left, b.left),
             std::min(a.bottom, b.bottom), std::min(a.right, b.right) };
}

bool empty(const IRect &r) { return r.bottom <= r.top || r.right <= r.left; }

Screen current_screen()
{
    return { vdriver->framebuffer(), vdriver->width(), vdriver->height(),
             vdriver->rowBytes(), vdriver->bpp(),
             [](uint32_t raw) { return vdriver->pixelARGB(raw); } };
}

/* The box's rows of the framebuffer, as a run stores them. */
void capture(const Run &run, const Screen &s, std::vector<uint8_t> &out)
{
    int rows = run.box.bottom - run.box.top;
    out.resize((size_t)rows * run.rowLen);
    int byte0 = run.x0 * run.bpp / 8;
    for(int y = 0; y < rows; y++)
        memcpy(&out[(size_t)y * run.rowLen], s.data + (size_t)(run.box.top + y) * s.rowBytes + byte0,
               run.rowLen);
}

/* ---- fonts ---- */

uint64_t fnv1a(const uint8_t *p, size_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for(size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

struct FontChoice
{
    std::shared_ptr<const FontData> font;
    int style; /* the outline's own style (bold/italic bits) */
};

/* family, face, the top resource map (another application: other fonts) */
std::map<std::tuple<int, int, uint32_t>, FontChoice> g_choices;
std::map<uint64_t, std::shared_ptr<const FontData>> g_fonts; /* by content */

/* The family's outline for FACE, copied out of the guest; the Resource
   Manager's and Memory Manager's state as it was. An outline the Font
   Manager hasn't loaded (a size with a bitmap strike) is read and emptied
   again, as purging would. */
const FontChoice &font_for(INTEGER family, int face)
{
    face &= (int)bold | (int)italic;
    auto key = std::make_tuple((int)family, face, (uint32_t)(uintptr_t)(Handle)LM(TopMapHndl));
    auto it = g_choices.find(key);
    if(it != g_choices.end())
        return it->second;

    FontChoice choice{ nullptr, 0 };
    INTEGER saved_map = LM(CurMap);
    INTEGER saved_reserr = LM(ResErr);
    INTEGER saved_memerr = LM(MemErr);
    Boolean saved_resload = LM(ResLoad);
    INTEGER style = 0;
    if(Handle h = ROMlib_family_outline(family, face, &style))
    {
        bool loaded = *h != nullptr;
        if(!loaded)
            LoadResource(h);
        if(*h)
        {
            size_t n = GetHandleSize(h);
            const uint8_t *p = (const uint8_t *)*h;
            uint64_t hash = fnv1a(p, n) ^ n;
            auto f = g_fonts.find(hash);
            if(f != g_fonts.end())
                choice.font = f->second;
            else
            {
                auto data = std::make_shared<FontData>();
                data->bytes.assign(p, p + n);
                data->cmap = ROMlib_sfnt_cmap(data->bytes.data(), n);
                if(!data->cmap.empty())
                    choice.font = g_fonts[hash] = data;
            }
            choice.style = style;
            if(!loaded)
                EmptyHandle(h);
        }
    }
    LM(CurMap) = saved_map;
    LM(ResErr) = saved_reserr;
    LM(MemErr) = saved_memerr;
    LM(ResLoad) = saved_resload;
    return g_choices[key] = choice;
}

/* ---- the port ---- */

uint32_t argb_of(const RGBColor &c)
{
    return 0xFF000000 | (c.red >> 8) << 16 | (c.green >> 8) << 8 | (c.blue >> 8);
}

void region_rects(RgnHandle rgn, int dx, int dy, std::vector<IRect> &out)
{
    out.clear();
    if(!rgn || !*rgn)
        return;
    if(RGN_SMALL_P(rgn))
    {
        const Rect &b = RGN_BBOX(rgn);
        IRect r{ b.top - dy, b.left - dx, b.bottom - dy, b.right - dx };
        if(!empty(r))
            out.push_back(r);
        return;
    }
    RgnVector v(rgn);
    std::vector<int16_t> data(v.begin(), v.end());
    if(data.empty())
        return;
    forEachRect(data.begin(), [&](int l, int t, int r, int b) {
        out.push_back({ t - dy, l - dx, b - dy, r - dx });
    });
}

/* vis & clip & BOX */
std::vector<IRect> port_clip(GrafPtr port, int dx, int dy, const IRect &box)
{
    std::vector<IRect> vis, clip, out;
    region_rects(PORT_VIS_REGION(port), dx, dy, vis);
    region_rects(PORT_CLIP_REGION(port), dx, dy, clip);
    for(const IRect &a : vis)
    {
        IRect ab = intersection(a, box);
        if(empty(ab))
            continue;
        for(const IRect &c : clip)
        {
            IRect r = intersection(ab, c);
            if(!empty(r))
                out.push_back(r);
        }
    }
    return out;
}

bool same_text(const Run &a, const Run &b)
{
    return a.font == b.font && a.size == b.size && a.synth == b.synth && a.mode == b.mode
        && a.fg == b.fg && a.bk == b.bk && a.baseline == b.baseline && a.glyphs == b.glyphs && a.xs == b.xs
        && a.underline == b.underline && a.outline == b.outline && a.shadow == b.shadow;
}

/* Whether NEW draws every glyph OLD does, where it does, the same way
   (TextEdit draws the whole line again at each keystroke): OLD drawn too
   would darken the anti-aliasing. */
bool covered_by(const Run &old, const Run &now)
{
    if(old.font != now.font || old.size != now.size || old.synth != now.synth
       || old.mode != now.mode || old.fg != now.fg || old.baseline != now.baseline
       || old.outline != now.outline || old.shadow != now.shadow || old.mode == Mode::Xor)
        return false;
    size_t j = 0;
    for(size_t i = 0; i < old.glyphs.size(); i++)
    {
        while(j < now.xs.size() && now.xs[j] < old.xs[i])
            j++;
        if(j == now.xs.size() || now.xs[j] != old.xs[i] || now.glyphs[j] != old.glyphs[i])
            return false;
    }
    return !old.underline || now.underline;
}

/* NOW replaces OLD: where OLD's text is still what's under NOW, NOW's
   "before" is what was there before OLD (both runs' 1x glyphs go), and
   NOW is drawn where OLD was too (text drawn again through a smaller
   clip: AppleWorks' font pop-up) */
void inherit_before(Run &now, const Run &old)
{
    for(const IRect &c : old.clip)
    {
        IRect r = intersection(c, { now.box.top - 1, now.box.left - 1, now.box.bottom + 1, now.box.right + 1 });
        if(!empty(r) && std::find_if(now.clip.begin(), now.clip.end(), [&](const IRect &n) {
                            return n.top <= r.top && n.left <= r.left && n.bottom >= r.bottom && n.right >= r.right;
                        }) == now.clip.end())
            now.clip.push_back(r);
    }
    IRect r = intersection(now.box, old.box);
    for(int y = r.top; y < r.bottom; y++)
    {
        uint8_t *nb = &now.before[(size_t)(y - now.box.top) * now.rowLen];
        const uint8_t *ob = &old.before[(size_t)(y - old.box.top) * old.rowLen];
        const uint8_t *oa = &old.after[(size_t)(y - old.box.top) * old.rowLen];
        for(int x = r.left; x < r.right; x++)
        {
            uint32_t b = raw_pixel(ob, x - old.x0, old.bpp), a = raw_pixel(oa, x - old.x0, old.bpp);
            if(b != a && raw_pixel(nb, x - now.x0, now.bpp) == a)
                set_raw_pixel(nb, x - now.x0, now.bpp, b);
        }
    }
}

/* --executor-debug hidpi: each piece of text StdText drew on the screen,
   and what became of it */
void log_text(const hidpi::TextParams &p, int mode, const char *what)
{
    if(!ERROR_ENABLED_P(ERROR_HIDPI_LOG))
        return;
    std::string text;
    for(const hidpi::Glyph &g : p.glyphs)
        if(g.c >= 0x20 && g.c < 0x7F)
            text += (char)g.c;
        else
            text += "\\x" + std::string(1, "0123456789abcdef"[g.c >> 4]) + "0123456789abcdef"[g.c & 15];
    fprintf(stderr, "[hidpi] \"%s\" font %d size %d face $%02x mode %d: %s\n", text.c_str(),
            p.family, p.size, p.face, mode, what);
}

/* A run still on the screen with another drawn over it (IT, in g_runs):
   what's there now is its "after". Where the new one drew over all the
   pixels it had changed (a button drawn unpressed over its pressed look),
   nothing of it shows: it goes (it could never be found off the screen
   again), and false. */
bool recapture(Runs &runs, Runs::iterator it, const Screen &s)
{
    auto copy = std::make_shared<Run>(**it);
    capture(*copy, s, copy->after);
    if(copy->before == copy->after && !(*it)->noInk)
    {
        runs.erase(it);
        return false;
    }
    *it = copy;
    return true;
}

/* Drop the runs that aren't on the screen any more. */
void sweep(Runs &runs, const Screen &s)
{
    runs.erase(std::remove_if(runs.begin(), runs.end(),
                              [&](const auto &r) { return !run_valid(*r, s); }),
               runs.end());
}

/* ---- offscreen pixels: GWorld shadows (phase 4) ----

   Text and chrome drawn into a GWorld (or any port whose pixels aren't the
   screen's) are runs of that pixel buffer, kept as the screen's are, in
   its own coordinates. CopyBits from it carries the runs still valid in
   the rectangle copied over to where it lands, the screen or another
   offscreen buffer (copy_bits below). The runs are checked against the
   buffer's 1x pixels at each copy: a program that wrote them behind
   QuickDraw's back (or drew over them otherwise) leaves the 1x pixels.
   The emulator thread only: the render thread sees only the screen's. */

struct Offscreen
{
    int width = 0, height = 0, rowBytes = 0, bpp = 0;
    Runs runs;
    uint64_t used = 0;
};
/* by GWorld (its pixels move while unlocked) or else by base address */
std::map<const void *, Offscreen> g_offscreens;
uint64_t g_offscreen_clock;
enum
{
    MAX_OFFSCREENS = 64,
    MAX_OFFSCREEN_RUNS = 1024,
};

/* A pixel buffer drawn into, and its runs. */
struct Target
{
    Runs *runs;
    Screen s;
    bool screen;
};

/* The pixel buffer at BASE (a host pointer; ALT: what the pixmap's
   baseAddr says, a GWorld's pixels' handle while they're unlocked);
   CREATE: a buffer without runs yet too (else false) */
bool offscreen_target(Ptr base, const void *alt, const Rect &bounds, int rowBytes, int bpp, Target *t,
                      bool create = true)
{
    int w = bounds.right - bounds.left, h = bounds.bottom - bounds.top;
    if(!base || w <= 0 || h <= 0 || (bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 32)
       || (long)rowBytes * 8 < (long)w * bpp)
        return false;
    gw_info_t *gw = lookup_gw_info_by_gw_pixmap_baseaddr(base);
    if(!gw && alt)
        gw = lookup_gw_info_by_gw_pixmap_baseaddr((void *)alt);
    const void *key = gw ? (const void *)gw->gw : (const void *)base;
    if(!create && !g_offscreens.count(key))
        return false;
    if(g_offscreens.size() >= MAX_OFFSCREENS && !g_offscreens.count(key))
    {
        auto lru = g_offscreens.begin();
        for(auto it = g_offscreens.begin(); it != g_offscreens.end(); ++it)
            if(it->second.used < lru->second.used)
                lru = it;
        g_offscreens.erase(lru);
    }
    Offscreen &o = g_offscreens[key];
    if(o.width != w || o.height != h || o.rowBytes != rowBytes || o.bpp != bpp)
    {
        o.runs.clear();
        o.width = w;
        o.height = h;
        o.rowBytes = rowBytes;
        o.bpp = bpp;
    }
    o.used = ++g_offscreen_clock;
    t->runs = &o.runs;
    t->s = { (const uint8_t *)base, w, h, rowBytes, bpp, nullptr };
    t->screen = false;
    return true;
}

bool screen_target(Target *t)
{
    t->runs = &g_runs;
    t->s = current_screen();
    t->screen = true;
    return true;
}

/* What PORT draws into. */
bool port_target(GrafPtr port, Target *t)
{
    GDHandle gd = LM(MainDevice);
    if(!port || !gd || !*gd || !vdriver || !vdriver->framebuffer())
        return false;
    if(PORT_BASEADDR(port) == PIXMAP_BASEADDR(GD_PMAP(gd)))
        return screen_target(t);
    if(CGrafPort_p(port))
    {
        PixMapHandle pm = CPORT_PIXMAP((CGrafPtr)port);
        Ptr base = PIXMAP_BASEADDR(pm);
        const void *alt = nullptr;
        if((*pm)->pmVersion == gw_pm_version_handle && base)
        {
            alt = base;
            base = *(Handle)base;
        }
        return offscreen_target(base, alt, PIXMAP_BOUNDS(pm), PIXMAP_ROWBYTES(pm), PIXMAP_PIXEL_SIZE(pm), t);
    }
    BitMap &bm = PORT_BITS(port);
    return offscreen_target(bm.baseAddr, nullptr, bm.bounds, BITMAP_ROWBYTES(&bm), 1, t);
}

/* What a canonical PixMap (StdBits') is. */
bool pixmap_target(const PixMap *pm, Target *t, bool create)
{
    GDHandle gd = LM(MainDevice);
    if(!pm || !gd || !*gd || !vdriver || !vdriver->framebuffer())
        return false;
    if(pm->baseAddr == PIXMAP_BASEADDR(GD_PMAP(gd)))
        return screen_target(t);
    return offscreen_target(pm->baseAddr, nullptr, pm->bounds, BITMAP_ROWBYTES(pm), pm->pixelSize, t, create);
}

/* At most so many runs on T. */
void trim(Runs &runs, const Target &t)
{
    size_t max = t.screen ? MAX_RUNS : MAX_OFFSCREEN_RUNS;
    if(runs.size() > max)
    {
        sweep(runs, t.s);
        if(runs.size() > max * 3 / 4)
            runs.erase(runs.begin(), runs.begin() + (runs.size() - max * 3 / 4));
    }
}
}

bool Executor::hidpi::impl::run_valid(const Run &run, const Screen &s)
{
    if(run.bpp != s.bpp || run.box.bottom > s.height || run.box.right > s.width || !s.data)
        return false;
    int byte0 = run.x0 * run.bpp / 8;
    int rows = run.box.bottom - run.box.top;
    /* whether the run changed pixel (x, y) (box coordinates) */
    auto inked = [&](int x, int y) {
        if(y < 0 || y >= rows || x < run.box.left || x >= run.box.right)
            return false;
        int i = x - run.x0;
        return raw_pixel(&run.before[(size_t)y * run.rowLen], i, run.bpp)
            != raw_pixel(&run.after[(size_t)y * run.rowLen], i, run.bpp);
    };
    for(int y = 0; y < rows; y++)
    {
        const uint8_t *cur = s.data + (size_t)(run.box.top + y) * s.rowBytes + byte0;
        const uint8_t *aft = &run.after[(size_t)y * run.rowLen];
        if(!memcmp(cur, aft, run.rowLen))
            continue;
        if(run.noInk)
            return false;
        if(run.mode == Mode::Copy)
            for(int x = run.box.left; x < run.box.right; x++)
            {
                if(raw_pixel(cur, x - run.x0, run.bpp) != raw_pixel(aft, x - run.x0, run.bpp))
                    return false;
            }
        /* the text and a pixel round it (its anti-aliasing, the background
           it is restored to): a selected label's black box erased under
           its white text changes only those */
        for(int x = run.box.left; x < run.box.right; x++)
        {
            if(raw_pixel(cur, x - run.x0, run.bpp) == raw_pixel(aft, x - run.x0, run.bpp))
                continue;
            for(int dy = -1; dy <= 1; dy++)
                for(int dx = -1; dx <= 1; dx++)
                    if(inked(x + dx, y + dy))
                        return false;
        }
    }
    return true;
}

void Executor::hidpi::set_scale(double scale)
{
    g_scale = std::clamp(scale, 1.0, 4.0);
}

double Executor::hidpi::scale()
{
    return g_scale;
}

void Executor::hidpi::scaled_size(int w, int h, int *ow, int *oh)
{
    *ow = (int)std::ceil(w * g_scale);
    *oh = (int)std::ceil(h * g_scale);
}

bool Executor::hidpi::drawing_on_screen()
{
    GDHandle gd = LM(MainDevice);
    GrafPtr port = qdGlobals().thePort;
    if(!gd || !*gd || !port || !vdriver || !vdriver->framebuffer())
        return false;
    return PORT_BASEADDR(port) == PIXMAP_BASEADDR(GD_PMAP(gd));
}

void Executor::hidpi::text_draw(const TextParams &p, const std::function<void()> &blit)
{
    GrafPtr port = qdGlobals().thePort;
    int txmode = PORT_TX_MODE(port);
    auto skip = [&](const char *why) {
        log_text(p, txmode, why);
        blit();
    };
    Mode mode;
    switch(txmode & 0x37)
    {
        case srcOr: mode = Mode::Or; break;
        case srcBic: mode = Mode::Bic; break;
        case srcCopy: mode = Mode::Copy; break;
        case srcXor: mode = Mode::Xor; break;
        case grayishTextOr & 0x37: mode = Mode::Grey; break;
        default: skip("1x (transfer mode)"); return;
    }
    Target t;
    if(!port_target(port, &t))
    {
        skip("1x (not a pixel buffer)");
        return;
    }
    const Screen &s = t.s;
    Runs &runs = *t.runs;
    const Rect &bounds = PORT_BOUNDS(port);
    int dx = bounds.left, dy = bounds.top;
    IRect box = intersection({ p.dst->top - dy, p.dst->left - dx, p.dst->bottom - dy, p.dst->right - dx },
                             { 0, 0, s.height, s.width });
    if(empty(box) || (long)(box.bottom - box.top) * (box.right - box.left) > MAX_BOX_PIXELS
       || p.glyphs.empty() || p.size <= 0)
    {
        skip("1x (nothing on the screen)");
        return;
    }
    /* nothing of it drawn (a dialog's items drawn again outside its
       update region): the runs there stay what's on the screen */
    if(port_clip(port, dx, dy, box).empty())
    {
        skip("1x (clipped away)");
        return;
    }
    const FontChoice &choice = font_for(p.family, p.face);
    if(!choice.font)
    {
        skip("1x (no outline)");
        return;
    }
    /* a character the strike draws and the outline lacks: all of it 1x */
    for(const hidpi::Glyph &g : p.glyphs)
        if(!choice.font->cmap[g.c] && g.c != ' ' && g.c != '\r' && g.c != '\t' && g.c != '\n' && g.c)
        {
            skip("1x (a character the outline lacks)");
            return;
        }

    auto run = std::make_shared<Run>();
    run->box = box;
    run->bpp = s.bpp;
    int ppb = 8 / std::min(s.bpp, 8); /* pixels a byte (bpp < 8) */
    run->x0 = s.bpp < 8 ? box.left / ppb * ppb : box.left;
    run->rowLen = s.bpp < 8 ? (box.right - run->x0 + ppb - 1) / ppb : (box.right - box.left) * s.bpp / 8;
    capture(*run, s, run->before);

    /* the runs under this one that are still on the screen */
    Runs under;
    {
        std::lock_guard lk(g_mutex);
        for(auto &r : runs)
            if(intersects(r->box, box) && run_valid(*r, s))
                under.push_back(r);
    }

    blit();

    capture(*run, s, run->after);
    run->mode = mode;
    if(CGrafPort_p(port))
    {
        run->fg = argb_of(CPORT_RGB_FG_COLOR(port));
        run->bk = argb_of(CPORT_RGB_BK_COLOR(port));
    }
    else
    {
        run->fg = argb_of(*ROMlib_qd_color_to_rgb(PORT_FG_COLOR(port)));
        run->bk = argb_of(*ROMlib_qd_color_to_rgb(PORT_BK_COLOR(port)));
    }
    run->font = choice.font;
    run->size = p.size;
    run->synth = (p.face & ~choice.style & (int)bold ? synthBold : 0)
        | (p.face & ~choice.style & (int)italic ? synthItalic : 0);
    run->bold = std::max(p.bold, 1);
    run->underline = p.face & (int)underline;
    run->outline = p.face & ((int)outline | (int)shadow);
    run->ulThick = std::max(p.ulThick, 1);
    run->shadow = p.face & (int)shadow ? std::max(p.shadow, 1) : 0;
    run->baseline = p.baseline - dy;
    run->ulLeft = p.glyphs.front().x / 65536.0f - dx;
    run->ulRight = p.end / 65536.0f - dx;
    for(size_t i = 0; i < p.glyphs.size(); i++)
        if(uint16_t glyph = choice.font->cmap[p.glyphs[i].c])
        {
            Fixed next = i + 1 < p.glyphs.size() ? p.glyphs[i + 1].x : p.end;
            run->glyphs.push_back(glyph);
            run->xs.push_back(p.glyphs[i].x / 65536.0f - dx);
            run->advances.push_back((next - p.glyphs[i].x) / 65536.0f);
        }
    /* a pixel round the box for the anti-aliasing, where QuickDraw could
       have drawn */
    run->clip = port_clip(port, dx, dy,
                          intersection({ box.top - 1, box.left - 1, box.bottom + 1, box.right + 1 },
                                       { 0, 0, s.height, s.width }));

    std::lock_guard lk(g_mutex);
    /* the runs under it: what they left is what's there now (this one is
       drawn after them) */
    bool erased = false;
    int replaced = 0, dropped = 0;
    for(auto &u : under)
    {
        auto it = std::find(runs.begin(), runs.end(), u);
        if(it == runs.end())
            continue;
        /* srcXor drawn again over itself: both gone */
        if(mode == Mode::Xor && u->mode == Mode::Xor && !erased && same_text(*u, *run)
           && u->box.top == box.top && u->box.left == box.left)
        {
            runs.erase(it);
            erased = true;
            continue;
        }
        if(covered_by(*u, *run))
        {
            inherit_before(*run, *u);
            runs.erase(it);
            replaced++;
            continue;
        }
        if(!recapture(runs, it, s))
            dropped++;
    }
    /* the other runs it overlaps: the ones it draws again go (a caret
       blinking over one, or the line erased and drawn again, as TextEdit
       does, makes no difference: where what it left is still under this
       one, this one takes their "before"); those not on the screen any more
       go too */
    runs.erase(std::remove_if(runs.begin(), runs.end(), [&](const auto &r) {
                     if(!intersects(r->box, box)
                        || std::find(under.begin(), under.end(), r) != under.end())
                         return false;
                     if(covered_by(*r, *run))
                     {
                         inherit_before(*run, *r);
                         replaced++;
                         return true;
                     }
                     if(run_valid(*r, s))
                         return false;
                     dropped++;
                     return true;
                 }),
                 runs.end());
    if(erased)
    {
        log_text(p, txmode, "erases its srcXor twin");
        return;
    }
    run->noInk = run->before == run->after;
    if(ERROR_ENABLED_P(ERROR_HIDPI_LOG))
    {
        char what[96];
        snprintf(what, sizeof what, "recorded at %d,%d-%d,%d (%zu under, %d replaced, %d gone, %zu on screen)",
                 box.left, box.top, box.right, box.bottom,
                 under.size(), replaced, dropped, runs.size() + 1);
        log_text(p, txmode, what);
    }
    runs.push_back(run);
    trim(runs, t);
}

void Executor::hidpi::chrome_draw(const Rect &r, Shapes shapes, const std::function<void()> &blit)
{
    GrafPtr port = qdGlobals().thePort;
    Target t;
    if(!active() || shapes.empty() || !port_target(port, &t))
    {
        blit();
        return;
    }
    const Screen &s = t.s;
    Runs &runs = *t.runs;
    const Rect &bounds = PORT_BOUNDS(port);
    int dx = bounds.left, dy = bounds.top;
    IRect box = intersection({ r.top - dy, r.left - dx, r.bottom - dy, r.right - dx }, { 0, 0, s.height, s.width });
    if(empty(box) || (long)(box.bottom - box.top) * (box.right - box.left) > MAX_BOX_PIXELS)
    {
        blit();
        return;
    }
    /* nothing of it drawn (a title's background redrawn under the bar's
       corners): the runs there stay */
    std::vector<IRect> clip = port_clip(port, dx, dy, box);
    if(clip.empty())
    {
        blit();
        return;
    }
    auto run = std::make_shared<Run>();
    run->box = box;
    run->bpp = s.bpp;
    int ppb = 8 / std::min(s.bpp, 8);
    run->x0 = s.bpp < 8 ? box.left / ppb * ppb : box.left;
    run->rowLen = s.bpp < 8 ? (box.right - run->x0 + ppb - 1) / ppb : (box.right - box.left) * s.bpp / 8;
    capture(*run, s, run->before);
    Runs under;
    {
        std::lock_guard lk(g_mutex);
        for(auto &u : runs)
            if(intersects(u->box, box) && run_valid(*u, s))
                under.push_back(u);
    }

    blit();

    capture(*run, s, run->after);
    run->mode = Mode::Or;
    for(Shape &sh : shapes)
    {
        for(size_t i = 0; i + 1 < sh.pts.size(); i += 2)
        {
            if(sh.kind == Shape::Circle && i == 2)
                break; /* the radius */
            sh.pts[i] -= dx;
            sh.pts[i + 1] -= dy;
        }
        sh.gx -= dx;
        sh.gy -= dy;
        sh.gx2 -= dx;
        sh.gy2 -= dy;
    }
    run->shapes = std::move(shapes);
    run->clip = std::move(clip);

    std::lock_guard lk(g_mutex);
    for(auto &u : under)
    {
        auto it = std::find(runs.begin(), runs.end(), u);
        if(it == runs.end())
            continue;
        /* the same chrome drawn again: this one, with what was there
           before the first */
        if(!u->shapes.empty() && u->box.top == box.top && u->box.left == box.left
           && u->box.bottom == box.bottom && u->box.right == box.right && u->shapes == run->shapes)
        {
            inherit_before(*run, *u);
            runs.erase(it);
            continue;
        }
        recapture(runs, it, s);
    }
    runs.erase(std::remove_if(runs.begin(), runs.end(),
                                [&](const auto &u) { return intersects(u->box, box) && !run_valid(*u, s); }),
                 runs.end());
    run->noInk = run->before == run->after;
    if(ERROR_ENABLED_P(ERROR_HIDPI_LOG))
        fprintf(stderr, "[hidpi] chrome: %zu shapes at %d,%d-%d,%d%s (%zu under, %zu on screen)\n",
                run->shapes.size(), box.left, box.top, box.right, box.bottom, run->noInk ? " (no change)" : "",
                under.size(), runs.size() + 1);
    runs.push_back(run);
    trim(runs, t);
}

void Executor::hidpi::compose(const uint32_t *argb, int w, int h, const DirtyRects::Rects &rects,
                              uint32_t *out, int ow, int oh)
{
    if(!vdriver || !vdriver->framebuffer())
        return;
    Runs runs;
    {
        std::lock_guard lk(g_mutex);
        runs = g_runs;
    }
    std::vector<IRect> r;
    for(const auto &d : rects)
        r.push_back({ d.top, d.left, d.bottom, d.right });
    auto t0 = std::chrono::steady_clock::now();
    draw(g_scale, argb, w, h, r, out, ow, oh, runs, current_screen());

    /* --executor-debug hidpi: what composing costs, every 5 s */
    if(ERROR_ENABLED_P(ERROR_HIDPI_LOG))
    {
        static std::chrono::steady_clock::time_point since = t0;
        static long calls, pixels;
        static double total_ms, max_ms;
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        calls++;
        total_ms += ms;
        max_ms = std::max(max_ms, ms);
        for(const IRect &d : r)
            pixels += (long)(d.bottom - d.top) * (d.right - d.left);
        if(t0 - since >= std::chrono::seconds(5))
        {
            fprintf(stderr, "[hidpi] compose: %ld calls, %ld 1x pixels, %.1f ms (max %.2f ms), %zu runs\n",
                    calls, pixels, total_ms, max_ms, runs.size());
            since = t0;
            calls = pixels = 0;
            total_ms = max_ms = 0;
        }
    }
}

/* ---- GWorld shadows: runs carried by CopyBits ---- */

bool Executor::hidpi::recording()
{
    Target t;
    return active() && port_target(qdGlobals().thePort, &t);
}

void Executor::hidpi::forget_offscreen(GWorldPtr gw)
{
    g_offscreens.erase((const void *)gw);
}

namespace
{
/* The colours of indexed pixels of SRC and DST are the same ones. */
bool same_colours(const PixMap *src, const PixMap *dst)
{
    if(src->pixelSize > 8)
        return true;
    CTabHandle a = src->pmTable, b = dst->pmTable;
    if(a == b)
        return true;
    if(!a || !b || !*a || !*b || CTAB_SIZE(a) != CTAB_SIZE(b))
        return false;
    for(int i = 0; i <= CTAB_SIZE(a); i++)
    {
        const ColorSpec &x = CTAB_TABLE(a)[i], &y = CTAB_TABLE(b)[i];
        if(x.rgb.red != y.rgb.red || x.rgb.green != y.rgb.green || x.rgb.blue != y.rgb.blue)
            return false;
    }
    return true;
}

/* RUN of the source, where the copy put it: its box the part copied
   (OX, OY: source pixel to destination pixel), its "before" the source's
   "before" wherever the destination got the source's pixel (masked out,
   clipped: the destination's own), its clip CLIP's part of its own. Null
   when nothing it drew landed. D: the destination after the copy. */
std::shared_ptr<Run> carried(const Run &run, int ox, int oy, const IRect &src_area,
                             const std::vector<IRect> &clip, const Screen &d)
{
    IRect sbox = intersection(run.box, src_area);
    IRect box = intersection({ sbox.top + oy, sbox.left + ox, sbox.bottom + oy, sbox.right + ox },
                             { 0, 0, d.height, d.width });
    if(empty(box))
        return nullptr;
    auto n = std::make_shared<Run>(run);
    n->box = box;
    n->bpp = d.bpp;
    int ppb = 8 / std::min(d.bpp, 8);
    n->x0 = d.bpp < 8 ? box.left / ppb * ppb : box.left;
    n->rowLen = d.bpp < 8 ? (box.right - n->x0 + ppb - 1) / ppb : (box.right - box.left) * d.bpp / 8;
    capture(*n, d, n->after);
    n->before = n->after;
    bool ink = false;
    for(int y = box.top; y < box.bottom; y++)
    {
        const uint8_t *sb = &run.before[(size_t)(y - oy - run.box.top) * run.rowLen];
        const uint8_t *sa = &run.after[(size_t)(y - oy - run.box.top) * run.rowLen];
        uint8_t *nb = &n->before[(size_t)(y - box.top) * n->rowLen];
        const uint8_t *na = &n->after[(size_t)(y - box.top) * n->rowLen];
        for(int x = box.left; x < box.right; x++)
        {
            uint32_t b = raw_pixel(sb, x - ox - run.x0, run.bpp), a = raw_pixel(sa, x - ox - run.x0, run.bpp);
            if(b != a && raw_pixel(na, x - n->x0, n->bpp) == a)
            {
                set_raw_pixel(nb, x - n->x0, n->bpp, b);
                ink = true;
            }
        }
    }
    if(!ink && !run.noInk)
        return nullptr;
    n->noInk = !ink;
    for(size_t i = 0; i < n->xs.size(); i++)
        n->xs[i] += ox;
    n->baseline += oy;
    n->ulLeft += ox;
    n->ulRight += ox;
    for(Executor::hidpi::Shape &sh : n->shapes)
    {
        for(size_t i = 0; i + 1 < sh.pts.size(); i += 2)
        {
            if(sh.kind == Executor::hidpi::Shape::Circle && i == 2)
                break; /* the radius */
            sh.pts[i] += ox;
            sh.pts[i + 1] += oy;
        }
        sh.gx += ox;
        sh.gy += oy;
        sh.gx2 += ox;
        sh.gy2 += oy;
    }
    n->clip.clear();
    for(const IRect &c : run.clip)
    {
        IRect m{ c.top + oy, c.left + ox, c.bottom + oy, c.right + ox };
        for(const IRect &k : clip)
        {
            IRect r = intersection(m, k);
            if(!empty(r))
                n->clip.push_back(r);
        }
    }
    if(n->clip.empty())
        return nullptr;
    return n;
}

void log_copy(const char *what, const IRect &r, size_t runs)
{
    if(ERROR_ENABLED_P(ERROR_HIDPI_LOG))
        fprintf(stderr, "[hidpi] copy %d,%d-%d,%d: %s (%zu runs)\n", r.left, r.top, r.right, r.bottom, what, runs);
}
}

void Executor::hidpi::copy_bits(const PixMap *src, const PixMap *dst, const Rect *sr, const Rect *dr, int mode,
                                RgnHandle mask, const std::function<void()> &blit)
{
    Target from;
    bool src_screen = active() && vdriver && vdriver->framebuffer() && LM(MainDevice)
        && src->baseAddr == PIXMAP_BASEADDR(GD_PMAP(LM(MainDevice)));
    if(!active() || (!src_screen && g_offscreens.empty()) || !pixmap_target(src, &from, false)
       || from.runs->empty())
    {
        blit();
        return;
    }
    int sx = src->bounds.left, sy = src->bounds.top;
    IRect src_area = intersection({ sr->top - sy, sr->left - sx, sr->bottom - sy, sr->right - sx },
                                  { 0, 0, from.s.height, from.s.width });
    /* the source's runs in the rectangle copied that it still shows */
    Runs runs;
    {
        std::unique_lock lk(g_mutex, std::defer_lock);
        if(from.screen)
            lk.lock();
        for(const auto &r : *from.runs)
            if(intersects(r->box, src_area) && run_valid(*r, from.s))
                runs.push_back(r);
    }
    /* 1:1, srcCopy, the same pixels: else the copy is the 1x pixels */
    const char *why = nullptr;
    if(runs.empty())
        why = "no runs";
    else if(sr->right - sr->left != dr->right - dr->left || sr->bottom - sr->top != dr->bottom - dr->top)
        why = "scaled";
    else if((mode & ~64) != srcCopy) /* ditherCopy: 64 */
        why = "transfer mode";
    else if(src->pixelSize != dst->pixelSize || !same_colours(src, dst))
        why = "depth or colours";
    Target to;
    if(!why && !pixmap_target(dst, &to, true))
        why = "destination";
    if(why)
    {
        log_copy(why, src_area, runs.size());
        blit();
        return;
    }

    blit();

    /* where the copy could draw: the destination rectangle, the port's
       vis and clip, the mask */
    int dx = dst->bounds.left, dy = dst->bounds.top;
    IRect dst_area = intersection({ dr->top - dy, dr->left - dx, dr->bottom - dy, dr->right - dx },
                                  { 0, 0, to.s.height, to.s.width });
    std::vector<IRect> clip = port_clip(qdGlobals().thePort, dx, dy, dst_area);
    if(mask)
    {
        std::vector<IRect> m, both;
        region_rects(mask, dx, dy, m);
        for(const IRect &a : clip)
            for(const IRect &b : m)
                if(!empty(intersection(a, b)))
                    both.push_back(intersection(a, b));
        clip = std::move(both);
    }
    int ox = (dr->left - dx) - (sr->left - sx), oy = (dr->top - dy) - (sr->top - sy);
    Runs made;
    for(const auto &r : runs)
        if(auto n = carried(*r, ox, oy, src_area, clip, to.s))
            made.push_back(n);
    if(made.empty())
    {
        log_copy("nothing landed", src_area, runs.size());
        return;
    }

    std::unique_lock lk(g_mutex, std::defer_lock);
    if(to.screen)
        lk.lock();
    Runs &dst_runs = *to.runs;
    /* the runs it copied over: those the copy brings again go (a window's
       contents copied again unchanged: drawn twice they'd darken), giving
       their "before"; those it painted over go; the others' "after" is
       what's there now */
    for(size_t i = 0; i < dst_runs.size();)
    {
        auto it = dst_runs.begin() + i;
        if(!intersects((*it)->box, dst_area, 1))
        {
            i++;
            continue;
        }
        bool replaced = false;
        for(auto &n : made)
        {
            bool same = (*it)->shapes.empty() ? !(*it)->glyphs.empty() && covered_by(**it, *n)
                                             : (*it)->box.top == n->box.top && (*it)->box.left == n->box.left
                                                 && (*it)->shapes == n->shapes;
            if(same)
            {
                auto m = std::make_shared<Run>(*n);
                inherit_before(*m, **it);
                n = m;
                replaced = true;
                break;
            }
        }
        if(replaced || !run_valid(**it, to.s))
            dst_runs.erase(it);
        else if(recapture(dst_runs, it, to.s))
            i++;
    }
    for(auto &n : made)
        dst_runs.push_back(n);
    trim(dst_runs, to);
    if(ERROR_ENABLED_P(ERROR_HIDPI_LOG))
    {
        char what[96];
        snprintf(what, sizeof what, "%zu carried to %s at %+d,%+d (%zu there)", made.size(),
                 to.screen ? "the screen" : "an offscreen", ox, oy, dst_runs.size());
        log_copy(what, src_area, runs.size());
    }
}
