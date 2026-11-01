/* MacPhoenix: high-resolution rendering, drawing side (hidpi.h): the 1x
   frame scaled up, text runs drawn over it with Skia from the fonts'
   outlines. The render thread only; no Executor headers here. */

#include <hidpi/hidpi_internal.h>

#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/effects/SkGradientShader.h"
#include "include/core/SkRegion.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkFontMgr_empty.h"

#include <algorithm>
#include <cmath>
#include <map>

using namespace Executor::hidpi::impl;

namespace
{
/* one typeface a font, made the first time it's drawn */
sk_sp<SkTypeface> typeface(const FontData *font)
{
    static sk_sp<SkFontMgr> mgr = SkFontMgr_New_Custom_Empty();
    static std::map<const FontData *, sk_sp<SkTypeface>> faces;
    auto it = faces.find(font);
    if(it != faces.end())
        return it->second;
    sk_sp<SkTypeface> tf = mgr->makeFromData(SkData::MakeWithoutCopy(font->bytes.data(), font->bytes.size()));
    if(!tf)
        fprintf(stderr, "[hidpi] Skia can't read a %zu-byte outline font\n", font->bytes.size());
    return faces[font] = tf;
}

/* 1x pixel edges at the display scale: pixel x covers [edge(x), edge(x + 1)) */
struct Scaler
{
    double s;
    int edge(int x) const { return (int)std::ceil(x * s - 1e-9); }
    SkIRect rect(const IRect &r) const
    {
        return SkIRect::MakeLTRB(edge(r.left), edge(r.top), edge(r.right), edge(r.bottom));
    }
};

void upscale(const Scaler &sc, const uint32_t *argb, int w, int h, const IRect &r,
             uint32_t *out, int ow, int oh)
{
    int Y0 = sc.edge(r.top), Y1 = std::min(oh, sc.edge(r.bottom));
    int X0 = sc.edge(r.left), X1 = std::min(ow, sc.edge(r.right));
    std::vector<int> sx(std::max(0, X1 - X0));
    for(int X = X0; X < X1; X++)
        sx[X - X0] = std::min(w - 1, (int)(X / sc.s + 1e-9));
    for(int Y = Y0; Y < Y1; Y++)
    {
        const uint32_t *src = argb + (size_t)std::min(h - 1, (int)(Y / sc.s + 1e-9)) * w;
        uint32_t *dst = out + (size_t)Y * ow;
        for(int X = X0; X < X1; X++)
            dst[X] = src[sx[X - X0]];
    }
}

/* The pixels the run changed, back to what they were before it, inside
   R: its glyphs are drawn there instead of the 1x ones. */
void restore(const Scaler &sc, const Run &run, const IRect &r, uint32_t *out, int ow, int oh,
             const Screen &screen)
{
    int top = std::max(run.box.top, r.top), bottom = std::min(run.box.bottom, r.bottom);
    int left = std::max(run.box.left, r.left), right = std::min(run.box.right, r.right);
    for(int y = top; y < bottom; y++)
    {
        const uint8_t *bef = &run.before[(size_t)(y - run.box.top) * run.rowLen];
        const uint8_t *aft = &run.after[(size_t)(y - run.box.top) * run.rowLen];
        for(int x = left; x < right; x++)
        {
            uint32_t b = raw_pixel(bef, x - run.x0, run.bpp);
            if(b == raw_pixel(aft, x - run.x0, run.bpp))
                continue;
            uint32_t c = screen.argb(b);
            int Y1 = std::min(oh, sc.edge(y + 1)), X1 = std::min(ow, sc.edge(x + 1));
            for(int Y = sc.edge(y); Y < Y1; Y++)
                for(int X = sc.edge(x); X < X1; X++)
                    out[(size_t)Y * ow + X] = c;
        }
    }
}

uint32_t mix(uint32_t a, uint32_t b)
{
    return 0xFF000000 | (((a >> 16 & 0xFF) + (b >> 16 & 0xFF)) / 2) << 16
        | (((a >> 8 & 0xFF) + (b >> 8 & 0xFF)) / 2) << 8 | ((a & 0xFF) + (b & 0xFF)) / 2;
}

void draw_run(SkCanvas *canvas, const Scaler &sc, const Run &run)
{
    sk_sp<SkTypeface> tf = typeface(run.font.get());
    if(!tf)
        return;
    float s = (float)sc.s;

    SkPaint paint;
    paint.setAntiAlias(run.mode != Mode::Xor);
    switch(run.mode)
    {
        case Mode::Or:
        case Mode::Copy:
            paint.setColor(run.fg);
            break;
        case Mode::Bic:
            paint.setColor(run.bk);
            break;
        case Mode::Grey:
            paint.setColor(mix(run.fg, run.bk));
            break;
        case Mode::Xor:
            paint.setColor(SK_ColorWHITE);
            paint.setBlendMode(SkBlendMode::kDifference);
            break;
    }

    if(run.mode == Mode::Copy)
    {
        SkPaint bk;
        bk.setColor(run.bk);
        canvas->drawIRect(sc.rect(run.box), bk);
    }

    SkFont font(tf, run.size * s);
    font.setEdging(run.mode == Mode::Xor ? SkFont::Edging::kAlias : SkFont::Edging::kAntiAlias);
    font.setSubpixel(true);
    font.setHinting(SkFontHinting::kSlight);
    if(run.synth & synthItalic)
        font.setSkewX(-0.25f);
    /* bold as QuickDraw makes it: the glyph again BOLD pixels to the right */
    float smear = run.synth & synthBold ? run.bold * s : 0;

    /* each glyph centred in the advance the 1x layout gives it (a bitmap
       strike's proportions aren't the outline's: Geneva 12's i and l) */
    std::vector<SkScalar> widths(run.glyphs.size());
    font.getWidths(run.glyphs, widths);
    std::vector<SkPoint> pos(run.glyphs.size());
    for(size_t i = 0; i < pos.size(); i++)
    {
        float slack = run.advances[i] > 0 ? (run.advances[i] * s - widths[i] - smear) / 2 : 0;
        pos[i] = { run.xs[i] * s + slack, run.baseline * s };
    }
    auto draw_glyphs = [&](SkPoint origin, const SkPaint &p) {
        for(float d = 0; d <= smear; d += 1)
            canvas->drawGlyphs(run.glyphs, pos, { origin.fX + d, origin.fY }, font, p);
    };

    if(run.outline)
    {
        /* outline: the glyphs' edges, hollow; shadow: and their edges
           offset down and right */
        SkPaint stroke = paint;
        stroke.setStyle(SkPaint::kStroke_Style);
        stroke.setStrokeWidth(s);
        if(run.shadow)
        {
            SkPaint sh = stroke;
            sh.setStyle(SkPaint::kStrokeAndFill_Style);
            float d = run.shadow * s;
            draw_glyphs({ d, d }, sh);
        }
        draw_glyphs({ 0, 0 }, stroke);
        if(run.mode != Mode::Xor)
        {
            SkPaint fill;
            fill.setAntiAlias(true);
            fill.setColor(run.bk);
            draw_glyphs({ 0, 0 }, fill);
        }
    }
    else
        draw_glyphs({ 0, 0 }, paint);

    if(run.underline)
    {
        SkPaint ul = paint;
        ul.setAntiAlias(false);
        canvas->drawRect(SkRect::MakeLTRB(run.ulLeft * s, (run.baseline + 1) * s,
                                          run.ulRight * s, (run.baseline + 1 + run.ulThick) * s),
                         ul);
    }
}

/* A chrome run: its shapes, anti-aliased, at the display scale. */
void draw_chrome(SkCanvas *canvas, const Scaler &sc, const Run &run)
{
    float s = (float)sc.s;
    for(const Executor::hidpi::Shape &sh : run.shapes)
    {
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor(sh.color);
        if(!sh.colors.empty())
        {
            std::vector<SkColor> colors(sh.colors.begin(), sh.colors.end());
            const SkScalar *pos = sh.pos.size() == colors.size() ? sh.pos.data() : nullptr;
            if(sh.gr > 0)
                paint.setShader(SkGradientShader::MakeRadial({ sh.gx * s, sh.gy * s }, sh.gr * s, colors.data(), pos,
                                                             (int)colors.size(), SkTileMode::kClamp));
            else
            {
                SkPoint ends[] = { { sh.gx * s, sh.gy * s }, { sh.gx2 * s, sh.gy2 * s } };
                paint.setShader(SkGradientShader::MakeLinear(ends, colors.data(), pos, (int)colors.size(),
                                                             SkTileMode::kClamp));
            }
        }
        if(sh.stroke > 0)
        {
            paint.setStyle(SkPaint::kStroke_Style);
            paint.setStrokeWidth(sh.stroke * s);
            paint.setStrokeJoin(SkPaint::kRound_Join);
            paint.setStrokeCap(SkPaint::kRound_Cap);
        }
        if(sh.kind == Executor::hidpi::Shape::Circle && sh.pts.size() >= 3)
        {
            canvas->drawCircle(sh.pts[0] * s, sh.pts[1] * s, sh.pts[2] * s, paint);
            continue;
        }
        if(sh.pts.size() < 4)
            continue;
        SkPathBuilder b;
        b.moveTo(sh.pts[0] * s, sh.pts[1] * s);
        for(size_t i = 2; i + 1 < sh.pts.size(); i += 2)
            b.lineTo(sh.pts[i] * s, sh.pts[i + 1] * s);
        if(sh.kind == Executor::hidpi::Shape::Polygon)
            b.close();
        canvas->drawPath(b.detach(), paint);
    }
}
}

void Executor::hidpi::impl::draw(double scale, const uint32_t *argb, int w, int h,
                                 const std::vector<IRect> &rects, uint32_t *out, int ow, int oh,
                                 const Runs &runs, const Screen &screen)
{
    Scaler sc{ scale };
    std::unique_ptr<SkCanvas> canvas = SkCanvas::MakeRasterDirect(
        SkImageInfo::MakeN32Premul(ow, oh), out, (size_t)ow * 4);
    if(!canvas)
        return;
    for(const IRect &r : rects)
    {
        upscale(sc, argb, w, h, r, out, ow, oh);
        for(const auto &run : runs)
        {
            if(run->box.left >= r.right + 1 || r.left >= run->box.right + 1
               || run->box.top >= r.bottom + 1 || r.top >= run->box.bottom + 1)
                continue;
            if(run->clip.empty() || !run_valid(*run, screen))
                continue;
            /* only where its glyphs are drawn (its clip) */
            for(const IRect &c : run->clip)
                restore(sc, *run,
                        { std::max(r.top, c.top), std::max(r.left, c.left), std::min(r.bottom, c.bottom),
                          std::min(r.right, c.right) },
                        out, ow, oh, screen);
            canvas->save();
            canvas->clipIRect(sc.rect(r));
            SkRegion clip;
            for(const IRect &c : run->clip)
                clip.op(sc.rect(c), SkRegion::kUnion_Op);
            canvas->clipRegion(clip);
            if(!run->shapes.empty())
                draw_chrome(canvas.get(), sc, *run);
            else
                draw_run(canvas.get(), sc, *run);
            canvas->restore();
        }
    }
}
