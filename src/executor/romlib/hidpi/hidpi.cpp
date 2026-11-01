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
   "before" is what was there before OLD (both runs' 1x glyphs go). */
void inherit_before(Run &now, const Run &old)
{
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

/* Drop the runs that aren't on the screen any more. */
void sweep(const Screen &s)
{
    g_runs.erase(std::remove_if(g_runs.begin(), g_runs.end(),
                                [&](const auto &r) { return !run_valid(*r, s); }),
                 g_runs.end());
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
    Screen s = current_screen();
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
        for(auto &r : g_runs)
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
        auto it = std::find(g_runs.begin(), g_runs.end(), u);
        if(it == g_runs.end())
            continue;
        /* srcXor drawn again over itself: both gone */
        if(mode == Mode::Xor && u->mode == Mode::Xor && !erased && same_text(*u, *run)
           && u->box.top == box.top && u->box.left == box.left)
        {
            g_runs.erase(it);
            erased = true;
            continue;
        }
        if(covered_by(*u, *run))
        {
            inherit_before(*run, *u);
            g_runs.erase(it);
            replaced++;
            continue;
        }
        auto copy = std::make_shared<Run>(*u);
        capture(*copy, s, copy->after);
        *it = copy;
    }
    /* the other runs it overlaps: the ones it draws again go (a caret
       blinking over one, or the line erased and drawn again, as TextEdit
       does, makes no difference: where what it left is still under this
       one, this one takes their "before"); those not on the screen any more
       go too */
    g_runs.erase(std::remove_if(g_runs.begin(), g_runs.end(), [&](const auto &r) {
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
                 g_runs.end());
    if(erased)
    {
        log_text(p, txmode, "erases its srcXor twin");
        return;
    }
    if(ERROR_ENABLED_P(ERROR_HIDPI_LOG))
    {
        char what[96];
        snprintf(what, sizeof what, "recorded%s at %d,%d-%d,%d (%zu under, %d replaced, %d gone, %zu on screen)",
                 run->clip.empty() ? ", clipped away" : "", box.left, box.top, box.right, box.bottom,
                 under.size(), replaced, dropped, g_runs.size() + 1);
        log_text(p, txmode, what);
    }
    g_runs.push_back(run);
    if(g_runs.size() > MAX_RUNS)
    {
        sweep(s);
        if(g_runs.size() > MAX_RUNS * 3 / 4)
            g_runs.erase(g_runs.begin(), g_runs.begin() + (g_runs.size() - MAX_RUNS * 3 / 4));
    }
}

void Executor::hidpi::chrome_draw(const Rect &r, Shapes shapes, const std::function<void()> &blit)
{
    if(!active() || shapes.empty() || !drawing_on_screen())
    {
        blit();
        return;
    }
    GrafPtr port = qdGlobals().thePort;
    Screen s = current_screen();
    const Rect &bounds = PORT_BOUNDS(port);
    int dx = bounds.left, dy = bounds.top;
    IRect box = intersection({ r.top - dy, r.left - dx, r.bottom - dy, r.right - dx }, { 0, 0, s.height, s.width });
    if(empty(box) || (long)(box.bottom - box.top) * (box.right - box.left) > MAX_BOX_PIXELS)
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
        for(auto &u : g_runs)
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
    run->clip = port_clip(port, dx, dy, box);

    std::lock_guard lk(g_mutex);
    for(auto &u : under)
    {
        auto it = std::find(g_runs.begin(), g_runs.end(), u);
        if(it == g_runs.end())
            continue;
        /* the same chrome drawn again: this one, with what was there
           before the first */
        if(!u->shapes.empty() && u->box.top == box.top && u->box.left == box.left
           && u->box.bottom == box.bottom && u->box.right == box.right && u->shapes == run->shapes)
        {
            inherit_before(*run, *u);
            g_runs.erase(it);
            continue;
        }
        auto copy = std::make_shared<Run>(*u);
        capture(*copy, s, copy->after);
        *it = copy;
    }
    g_runs.erase(std::remove_if(g_runs.begin(), g_runs.end(),
                                [&](const auto &u) { return intersects(u->box, box) && !run_valid(*u, s); }),
                 g_runs.end());
    g_runs.push_back(run);
    if(g_runs.size() > MAX_RUNS)
    {
        sweep(s);
        if(g_runs.size() > MAX_RUNS * 3 / 4)
            g_runs.erase(g_runs.begin(), g_runs.begin() + (g_runs.size() - MAX_RUNS * 3 / 4));
    }
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
