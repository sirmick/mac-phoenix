/* MacPhoenix: the Appearance Manager's drawing of controls (Appearance.h):
 * tracks (scroll bars, sliders, progress bars), buttons, headers, frames,
 * tabs, for 8.x's control definitions (ControlDefinitions) and
 * applications. Executor's look (appearance_draw.h); the measurements a
 * real Mac gave where the reference has them (tests/guest/appearance/
 * reference-9.0.4.txt: TRACK, BUTTON, TAB), Executor's own elsewhere. */

#include <base/common.h>
#include <Appearance.h>
#include <ControlMgr.h>
#include <ControlMgr8.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <quickdraw/cquick.h>
#include <base/functions.impl.h>
#include "appearance_draw.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <tuple>

using namespace Executor;
using namespace Executor::appearance;

namespace
{
/* ── Tracks ──────────────────────────────────────────────────────────── */

constexpr int arrowSize = 16; /* a scroll bar's arrow boxes, its width */
constexpr int sliderThumbLength = 15, sliderPlainThumbLength = 13, sliderInset = 5;

/* Where a track's parts are. The axis: v (vertical) or h; `lo`/`hi` the
   ends along it. */
struct TrackGeometry
{
    Rect bounds, track, drag, thumb;
    Rect arrowLo, arrowHi;
    bool horizontal, scrollBar, hasArrows, hasThumb;
    int thumbLength;

    int lo(const Rect &r) const { return horizontal ? r.left : r.top; }
    int hi(const Rect &r) const { return horizontal ? r.right : r.bottom; }
    void setAxis(Rect &r, int a, int b) const
    {
        if(horizontal)
            r.left = a, r.right = b;
        else
            r.top = a, r.bottom = b;
    }
};

TrackGeometry scrollBarGeometry(const Rect &bounds, bool horizontal, bool arrows)
{
    TrackGeometry g{};
    g.bounds = bounds;
    g.horizontal = horizontal;
    g.scrollBar = true;
    g.hasArrows = arrows && (horizontal ? bounds.right - bounds.left : bounds.bottom - bounds.top) >= 2 * arrowSize;
    g.track = bounds;
    if(g.hasArrows)
    {
        g.arrowLo = g.arrowHi = bounds;
        g.setAxis(g.arrowLo, g.lo(bounds), g.lo(bounds) + arrowSize);
        g.setAxis(g.arrowHi, g.hi(bounds) - arrowSize, g.hi(bounds));
        /* the track shares the arrows' outlines */
        g.setAxis(g.track, g.lo(bounds) + arrowSize - 1, g.hi(bounds) - arrowSize + 1);
    }
    g.drag = g.track;
    return g;
}

bool isProgress(ThemeTrackKind kind)
{
    return kind == kThemeMediumProgressBar || kind == kThemeLargeProgressBar || kind == kThemeMediumIndeterminateBar
           || kind == kThemeLargeIndeterminateBar;
}

TrackGeometry trackGeometry(const ThemeTrackDrawInfo &info)
{
    bool horizontal = info.attributes & kThemeTrackHorizontal;
    TrackGeometry g;
    int32_t min = info.min, max = info.max, value = info.value;
    if(info.kind == kThemeScrollBar || info.kind == kThemeSmallScrollBar)
    {
        /* the track only: a scroll bar's definition draws its arrows
           (DrawThemeScrollBarArrows) and gives the rest here */
        g = scrollBarGeometry(info.bounds, horizontal, false);
        int length = g.hi(g.drag) - g.lo(g.drag);
        int32_t viewSize = info.trackInfo.scrollbar.viewsize;
        int width = horizontal ? info.bounds.bottom - info.bounds.top : info.bounds.right - info.bounds.left;
        g.thumbLength = width;
        if(viewSize > 0 && max > min)
            g.thumbLength = std::max<int>(width, (int64_t)length * viewSize / ((int64_t)max - min + viewSize));
        g.hasThumb = (info.attributes & kThemeTrackShowThumb) && max > min
                     && info.enableState == kThemeTrackActive && g.thumbLength < length;
    }
    else if(isProgress(info.kind))
    {
        /* progress bars: the bounds themselves (8.6: track.3/4) */
        g = TrackGeometry{};
        g.horizontal = horizontal;
        g.bounds = g.track = g.drag = info.bounds;
        g.hasThumb = false;
    }
    else
    {
        /* sliders: 16 across, inset 2 along (reference TRACK 2), the
           thumb travelling 5 in from the ends */
        g = TrackGeometry{};
        g.horizontal = horizontal;
        g.bounds = info.bounds;
        if(horizontal)
            g.bounds.top = info.bounds.top, g.bounds.bottom = info.bounds.top + 16;
        else
            g.bounds.left = info.bounds.left, g.bounds.right = info.bounds.left + 16;
        g.setAxis(g.bounds, g.lo(info.bounds) + 2, g.hi(info.bounds) - 2);
        g.track = g.bounds;
        g.drag = info.bounds;
        g.setAxis(g.drag, g.lo(info.bounds) + sliderInset, g.hi(info.bounds) - sliderInset);
        if(horizontal)
            g.drag.top = g.bounds.top, g.drag.bottom = g.bounds.bottom;
        else
            g.drag.left = g.bounds.left, g.drag.right = g.bounds.right;
        /* a non-directional thumb is 13 long (8.6: appr controls.56) */
        g.thumbLength = info.kind == kThemeSlider && info.trackInfo.slider.thumbDir == kThemeThumbPlain
                            ? sliderPlainThumbLength
                            : sliderThumbLength;
        g.hasThumb = info.kind == kThemeSlider;
    }
    g.thumb = g.drag;
    if(g.hasThumb)
    {
        int travel = g.hi(g.drag) - g.lo(g.drag) - g.thumbLength;
        int pos = g.lo(g.drag);
        if(max > min && g.scrollBar)
            pos += (int64_t)travel * (std::clamp(value, min, max) - (int64_t)min) / ((int64_t)max - min);
        else if(max > min) /* a slider's rounded (8.6: 37.5 is 38, appr controls.48) */
            pos += ((int64_t)travel * (std::clamp(value, min, max) - (int64_t)min) * 2 + ((int64_t)max - min))
                   / (2 * ((int64_t)max - min));
        g.setAxis(g.thumb, pos, pos + g.thumbLength);
    }
    else
        SetRect(&g.thumb, 0, 0, 0, 0);
    return g;
}

/* A scroll bar's own face: boxFace's bevel, but kTrack (not kShadow) along
   the shaded edge - measured from the G3 (appr controls.16/.384). Used for
   the arrow boxes, the thumb, and the page above/below it. */
void scrollFace(const Rect &r, uint16_t fill, bool pressed)
{
    frameGray(r, kBlack);
    Rect in = r;
    InsetRect(&in, 1, 1);
    fillGray(in, pressed ? kShadow : fill);
    if(!pressed && in.right - in.left > 2 && in.bottom - in.top > 2)
        bevel(in, kWhite, kTrack);
}

/* An arrow box, its arrow pointing away from the track. */
void drawArrowBox(const Rect &r, int dir, bool pressed, bool enabled)
{
    scrollFace(r, kFace, pressed);
    arrow(r, dir, 7, enabled ? kBlack : kShadow);
}

void drawScrollBarArrows(const TrackGeometry &g, ThemeTrackEnableState enable, ThemeTrackPressState press)
{
    if(!g.hasArrows)
        return;
    bool enabled = enable == kThemeTrackActive;
    drawArrowBox(g.arrowLo, g.horizontal ? kThemeArrowLeft : kThemeArrowUp,
                 enabled && (press & kThemeLeftOutsideArrowPressed), enabled);
    drawArrowBox(g.arrowHi, g.horizontal ? kThemeArrowRight : kThemeArrowDown,
                 enabled && (press & kThemeRightOutsideArrowPressed), enabled);
}

/* The default appearance's accent ramp, darkest first: progress bars and
   slider thumbs are shaded in it (appr controls.48/80). */
const RGBColor accentRamp[] = {
    { 0x0000, 0x0000, 0x5555 }, { 0x3333, 0x3333, 0x9999 }, { 0x6666, 0x6666, 0xCCCC },
    { 0x9999, 0x9999, 0xFFFF }, { 0xCCCC, 0xCCCC, 0xFFFF }, { 0xEEEE, 0xEEEE, 0xEEEE },
};

/* A slider's thumb: pointing down (horizontal) or right (vertical), up or
   left, or neither (8.6's CDEF 48, 52, 56: appr controls.48/52/56). */
enum class Thumb
{
    down,
    up,
    plain
};

Thumb thumbShape(const ThemeTrackDrawInfo &info)
{
    switch(info.trackInfo.slider.thumbDir)
    {
        case kThemeThumbDownward: return Thumb::down;
        case kThemeThumbUpward: return Thumb::up;
        default: return Thumb::plain;
    }
}

/* Where a pixel of a slider goes: A along the track, C across it, from
   (a0, c0); a vertical slider is a horizontal one transposed (8.6's
   track.5 thumb region is track.2's). */
struct SliderAxes
{
    bool horizontal;
    int a0, c0;
    void line(int a1, int c1, int a2, int c2, RGBColor col) const
    {
        if(horizontal)
            lineRGB(a0 + a1, c0 + c1, a0 + a2, c0 + c2, col);
        else
            lineRGB(c0 + c1, a0 + a1, c0 + c2, a0 + a2, col);
    }
    void line(int a1, int c1, int a2, int c2, uint16_t g) const
    {
        line(a1, c1, a2, c2, RGBColor{ g, g, g });
    }
};

/* The pointed thumb's outline, 15 along and 16 across: a body of 10 (its
   leading corners cut), then a point narrowing by a pixel each side a
   row, 3 wide at its tip. Calls SPAN(across, from, to) along it. */
template<typename F>
void pointedThumbRows(F span)
{
    span(0, 1, 13);
    for(int c = 1; c < 10; c++)
        span(c, 0, 14);
    for(int k = 0; k < 5; k++)
        span(10 + k, 1 + k, 13 - k);
    span(15, 6, 8);
}

/* A thumb's rows, 16 across: the pointed one, the same turned over (its
   point up), or 13 along with its corners cut. */
template<typename F>
void thumbRows(Thumb shape, F span)
{
    if(shape == Thumb::down)
        pointedThumbRows(span);
    else if(shape == Thumb::up)
        pointedThumbRows([&](int c, int from, int to) { span(15 - c, from, to); });
    else
    {
        span(0, 1, 11);
        for(int c = 1; c < 15; c++)
            span(c, 0, 12);
        span(15, 1, 11);
    }
}

/* 8.6's region (appr track.2/5 thumb.rgn, the CRCs). */
void sliderThumbRgn(const TrackGeometry &g, Thumb shape, RgnHandle rgn)
{
    SetEmptyRgn(rgn);
    RgnHandle row = NewRgn();
    thumbRows(shape, [&](int c, int from, int to) {
        Rect r;
        if(g.horizontal)
            SetRect(&r, g.thumb.left + from, g.thumb.top + c, g.thumb.left + to + 1, g.thumb.top + c + 1);
        else
            SetRect(&r, g.thumb.left + c, g.thumb.top + from, g.thumb.left + c + 1, g.thumb.top + to + 1);
        RectRgn(row, &r);
        UnionRgn(rgn, row, rgn);
    });
    DisposeRgn(row);
}

/* The slider's channel, 6 deep, 3 into the track and its length less 1:
   a #222 outline with its corners cut (#777 inactive), #aaa inside (#bbb)
   and, active, a #aaa line over and before it (appr controls.48). */
void sliderChannel(const TrackGeometry &g, Thumb shape, bool active)
{
    int len = g.hi(g.bounds) - g.lo(g.bounds) - 2; /* the last pixel along */
    /* under the thumb's body: 3 in, 6 with the point before it, 4 */
    int in = shape == Thumb::down ? 3 : shape == Thumb::up ? 6 : 4;
    SliderAxes ax{ g.horizontal, g.lo(g.bounds), g.horizontal ? g.bounds.top + in : g.bounds.left + in };
    uint16_t edge = active ? 0x2222 : 0x7777, inside = active ? 0xAAAA : kTrack;
    if(active)
    {
        ax.line(1, 0, len - 1, 0, 0xAAAA);
        ax.line(0, 1, 0, 4, 0xAAAA);
        ax.line(1, 1, 1, 1, 0xAAAA);
        ax.line(len, 1, len, 1, 0xAAAA);
    }
    ax.line(2, 1, len - 1, 1, edge);
    ax.line(2, 5, len - 1, 5, edge);
    ax.line(1, 2, 1, 4, edge);
    ax.line(len, 2, len, 4, edge);
    for(int c = 2; c <= 4; c++)
        ax.line(2, c, len - 1, c, inside);
}

/* A slider's thumb: black outline (#777 inactive), shaded in the accent
   ramp lit from the top left, three grip ridges; pressed a step darker,
   inactive #ddd and no grip (appr controls.48/52/56). */
void sliderThumbPixels(const TrackGeometry &g, Thumb shape, bool active, bool pressed);

/* The thumb at the display scale: its outline filled, the body inside it,
   the lit and shaded edges, the grip (the 1x shape's corners) */
hidpi::Shapes sliderThumbShapes(const TrackGeometry &g, Thumb shape, bool active, bool pressed)
{
    float a0 = g.lo(g.thumb), c0 = g.horizontal ? g.thumb.top : g.thumb.left;
    auto pts = [&](std::initializer_list<float> ac) {
        std::vector<float> v;
        for(auto it = ac.begin(); it != ac.end(); it += 2)
        {
            float a = a0 + it[0], c = c0 + it[1];
            v.push_back(g.horizontal ? a : c);
            v.push_back(g.horizontal ? c : a);
        }
        return v;
    };
    int d = pressed ? 1 : 0;
    auto ramp = [&](int idx) {
        const RGBColor &c = accentRamp[std::max(0, idx - d)];
        return active ? 0xFF000000u | (c.red >> 8) << 16 | (c.green >> 8) << 8 | (c.blue >> 8) : argbGray(kFace);
    };
    uint32_t outline = argbGray(active ? kBlack : 0x7777);
    hidpi::Shapes s;
    auto line = [&](std::initializer_list<float> ac, int idx) { s.push_back({ hidpi::Shape::Polyline, pts(ac), 1, ramp(idx) }); };
    /* the grip: ridges at K, from C down (LEN long) */
    auto grip = [&](int k0, float c, float len) {
        if(active)
            for(int k = k0; k <= k0 + 4; k += 2)
            {
                line({ k + 0.5f, c + 0.5f, k + 0.5f, c + 0.5f + len }, 4);
                line({ k + 1.5f, c + 1.5f, k + 1.5f, c + 1.5f + len }, 1);
            }
    };
    switch(shape)
    {
        case Thumb::down:
            s.push_back({ hidpi::Shape::Polygon, pts({ 0, 1, 1, 0, 14, 0, 15, 1, 15, 10, 9, 16, 6, 16, 0, 10 }), 0, outline });
            s.push_back({ hidpi::Shape::Polygon, pts({ 1, 1, 14, 1, 14, 10.4f, 8.6f, 15, 6.4f, 15, 1, 10.4f }), 0, ramp(3) });
            line({ 13.5f, 2, 13.5f, 10.2f, 8.4f, 14.6f }, 2);
            line({ 1.5f, 10.2f, 6.6f, 14.6f }, 2);
            line({ 1.5f, 9.5f, 1.5f, 1.5f, 12.5f, 1.5f }, 4);
            grip(4, 3, 5);
            break;
        case Thumb::up:
            s.push_back({ hidpi::Shape::Polygon, pts({ 0, 15, 1, 16, 14, 16, 15, 15, 15, 6, 9, 0, 6, 0, 0, 6 }), 0, outline });
            s.push_back({ hidpi::Shape::Polygon, pts({ 1, 15, 14, 15, 14, 5.6f, 8.6f, 1, 6.4f, 1, 1, 5.6f }), 0, ramp(3) });
            line({ 1.5f, 14, 1.5f, 5.8f, 6.6f, 1.4f }, 4);
            line({ 8.4f, 1.4f, 13.5f, 5.8f, 13.5f, 14.5f, 2, 14.5f }, 2);
            grip(4, 6, 5);
            break;
        case Thumb::plain:
            s.push_back({ hidpi::Shape::Polygon, pts({ 0, 1, 1, 0, 12, 0, 13, 1, 13, 15, 12, 16, 1, 16, 0, 15 }), 0, outline });
            s.push_back({ hidpi::Shape::Polygon, pts({ 1, 1, 12, 1, 12, 15, 1, 15 }), 0, ramp(3) });
            line({ 1.5f, 14, 1.5f, 1.5f, 11, 1.5f }, 4);
            line({ 11.5f, 2, 11.5f, 14.5f, 2, 14.5f }, 2);
            grip(3, 4, 6);
            break;
    }
    return s;
}

void sliderThumb(const TrackGeometry &g, Thumb shape, bool active, bool pressed)
{
    Rect box = g.thumb;
    if(g.horizontal)
        box.bottom = box.top + 16;
    else
        box.right = box.left + 16;
    hidpi::chrome_draw(box, hidpi::active() ? sliderThumbShapes(g, shape, active, pressed) : hidpi::Shapes{},
                       [&] { sliderThumbPixels(g, shape, active, pressed); });
}

void sliderThumbPixels(const TrackGeometry &g, Thumb shape, bool active, bool pressed)
{
    SliderAxes ax{ g.horizontal, g.lo(g.thumb), g.horizontal ? g.thumb.top : g.thumb.left };
    int d = pressed ? 1 : 0;
    auto ramp = [&](int idx) { return active ? accentRamp[std::max(0, idx - d)] : RGBColor{ kFace, kFace, kFace }; };
    auto px = [&](int a, int c, int idx) { ax.line(a, c, a, c, ramp(idx)); };
    auto run = [&](int a1, int a2, int c, int idx) { ax.line(a1, c, a2, c, ramp(idx)); };
    uint16_t outline = active ? kBlack : 0x7777;
    /* the outline: each row's ends, the first and last rows whole */
    thumbRows(shape, [&](int c, int from, int to) {
        if(c == 0 || c == 15)
            ax.line(from, c, to, c, outline);
        else
        {
            ax.line(from, c, from, c, outline);
            ax.line(to, c, to, c, outline);
        }
    });
    /* the body: a lit first row and column, shaded last ones; its grip
       ridges from row TOP, LEN long, at K, K + 2, K + 4 */
    int gripTop, gripLen, gripK;
    switch(shape)
    {
        case Thumb::down:
            px(1, 1, 5);
            run(2, 12, 1, 4);
            px(13, 1, 3);
            for(int c = 2; c < 10; c++)
            {
                px(1, c, 4);
                run(2, 12, c, 3);
                px(13, c, 2);
            }
            for(int k = 0; k < 5; k++)
            {
                int c = 10 + k;
                run(2 + k, 12 - k, c, k == 4 ? 2 : 3);
                px(2 + k, c, 2);
                px(12 - k, c, 2);
            }
            gripTop = 3, gripLen = 5, gripK = 4;
            break;
        case Thumb::up:
            /* the point: lit on its leading side, shaded on the other */
            px(6, 1, 5);
            px(7, 1, 4);
            px(8, 1, 3);
            for(int c = 2; c < 6; c++)
            {
                px(7 - c, c, 4);
                run(8 - c, 6 + c, c, 3);
                px(7 + c, c, 2);
            }
            px(1, 6, 5);
            run(2, 12, 6, 3);
            px(13, 6, 2);
            for(int c = 7; c < 14; c++)
            {
                px(1, c, 4);
                run(2, 12, c, 3);
                px(13, c, 2);
            }
            px(1, 14, 3);
            run(2, 13, 14, 2);
            gripTop = 6, gripLen = 5, gripK = 4;
            break;
        default:
            px(1, 1, 5);
            run(2, 10, 1, 4);
            px(11, 1, 3);
            for(int c = 2; c < 14; c++)
            {
                px(1, c, 4);
                run(2, 10, c, 3);
                px(11, c, 2);
            }
            px(1, 14, 3);
            run(2, 11, 14, 2);
            gripTop = 4, gripLen = 6, gripK = 3;
            break;
    }
    if(!active)
        return;
    for(int r = gripK; r <= gripK + 4; r += 2)
    {
        px(r, gripTop, 5);
        ax.line(r, gripTop + 1, r, gripTop + gripLen, ramp(4));
        ax.line(r + 1, gripTop + 1, r + 1, gripTop + gripLen + 1, ramp(1));
    }
}

/* A scroll bar's thumb and the page above/below it: three panels, each
   scrollFace's own frame+fill+bevel (not one continuous track) - measured
   from the G3 (appr controls.16/.384); the thumb's tint is the accent
   colour (the same blue C_DrawThemeTrack's progress bar fills with). */
void drawScrollTrack(const TrackGeometry &g, bool pressed)
{
    RGBColor accent = { 0x9999, 0x9999, 0xFFFF };
    if(!g.hasThumb)
    {
        scrollFace(g.track, kFace, false);
        return;
    }
    Rect pageLo = g.track, pageHi = g.track;
    g.setAxis(pageLo, g.lo(g.track), g.lo(g.thumb));
    g.setAxis(pageHi, g.hi(g.thumb), g.hi(g.track));
    auto page = [](const Rect &pr) {
        frameGray(pr, kBlack);
        Rect in = pr;
        InsetRect(&in, 1, 1);
        fillGray(in, 0xAAAA);
        if(in.right - in.left > 2 && in.bottom - in.top > 2)
            bevel(in, kShadow, kTrack);
    };
    if(g.lo(pageLo) < g.hi(pageLo))
        page(pageLo);
    if(g.lo(pageHi) < g.hi(pageHi))
        page(pageHi);
    frameGray(g.thumb, kBlack);
    Rect in = g.thumb;
    InsetRect(&in, 1, 1);
    RGBForeColor(&accent);
    PaintRect(&in);
    if(!pressed && in.right - in.left > 2 && in.bottom - in.top > 2)
    {
        RGBColor lit = { 0xCCCC, 0xCCCC, 0xFFFF }, shade = { 0x6666, 0x6666, 0xCCCC };
        RGBForeColor(&lit);
        MoveTo(in.left, in.bottom - 2);
        LineTo(in.left, in.top);
        LineTo(in.right - 2, in.top);
        RGBForeColor(&shade);
        MoveTo(in.right - 1, in.top + 1);
        LineTo(in.right - 1, in.bottom - 1);
        LineTo(in.left + 1, in.bottom - 1);
    }
}

ControlPartCode hitTrack(const TrackGeometry &g, Point p)
{
    if(!PtInRect(p, &g.bounds))
        return kControlNoPart;
    if(g.hasArrows && PtInRect(p, &g.arrowLo))
        return inUpButton;
    if(g.hasArrows && PtInRect(p, &g.arrowHi))
        return inDownButton;
    if(!g.hasThumb)
        return kControlNoPart;
    if(PtInRect(p, &g.thumb))
        return kControlIndicatorPart;
    int at = g.horizontal ? p.h : p.v;
    if(!g.scrollBar)
        return kControlNoPart;
    return at < g.lo(g.thumb) ? inPageUp : inPageDown;
}

/* ── Buttons ─────────────────────────────────────────────────────────── */

/* Reference BUTTON lines (bounds 20,20 - 40,100): what each kind's content
   rectangle leaves of its bounds. */
Rect buttonContent(const Rect &b, ThemeButtonKind kind)
{
    Rect r = b;
    switch(kind)
    {
        case kThemePushButton:
        case kThemeSmallBevelButton:
            InsetRect(&r, 2, 2);
            break;
        case kThemeCheckBox:
        case kThemeRadioButton:
        case kThemeDisclosureButton:
        case kThemeSmallCheckBox:
        case kThemeSmallRadioButton:
            r.left += 18;
            break;
        case kThemeBevelButton:
            InsetRect(&r, 3, 3);
            break;
        case kThemeArrowButton:
        case kThemePopupButton:
            InsetRect(&r, 0, 2);
            r.left += 6;
            r.right -= 28;
            break;
        case kThemeIncDecButton:
            r.left += 15;
            break;
        case kThemeLargeBevelButton:
            InsetRect(&r, 4, 4);
            break;
        case kThemeListHeaderButton:
            InsetRect(&r, 4, 1);
            break;
        default:
            break;
    }
    return r;
}

/* The kinds 8.6 and 9.0.4 draw; the others answer paramErr (reference
   BUTTON 10-20) and draw nothing. */
bool knownButton(ThemeButtonKind kind)
{
    return kind <= kThemeLargeBevelButton;
}

/* ── Platinum buttons, measured from 8.6's on a G3 (appr draw32.button.*,
   bounds 10,10 - 34,100; the same at 8 bits) ── */

/* The gray with hex digit n in each place: grey(0xd) is #dddddd. */
constexpr uint16_t grey(int n) { return 0x1111 * n; }

void pixel(int h, int v, uint16_t g)
{
    lineGray(h, v, h, v, g);
}

/* Inclusive runs. */
void hrun(int h1, int h2, int v, uint16_t g)
{
    if(h2 >= h1)
        lineGray(h1, v, h2, v, g);
}

void vrun(int h, int v1, int v2, uint16_t g)
{
    if(v2 >= v1)
        lineGray(h, v1, h, v2, g);
}

/* A bevel's ring: TL along the top and the left, BR along the bottom and
   the right, X on the two pixels where they meet (top right, bottom left). */
struct Ring
{
    uint8_t tl, br, x;
};

/* Ring K of r (0 its outline), square. */
void squareRing(const Rect &r, int k, Ring c)
{
    int L = r.left + k, T = r.top + k, R = r.right - 1 - k, B = r.bottom - 1 - k;
    hrun(L, R - 1, T, grey(c.tl));
    vrun(L, T + 1, B - 1, grey(c.tl));
    pixel(R, T, grey(c.x));
    pixel(L, B, grey(c.x));
    vrun(R, T + 1, B, grey(c.br));
    hrun(L + 1, R - 1, B, grey(c.br));
}

/* A bevel: its rings from the outline in, then the face. */
void bevelBox(const Rect &r, std::initializer_list<Ring> rings, int face)
{
    Rect in = r;
    InsetRect(&in, rings.size(), rings.size());
    fillGray(in, grey(face));
    int k = 0;
    for(Ring c : rings)
        squareRing(r, k++, c);
}

/* A push button's outline, corners cut three pixels deep: OUTER at their
   ends (#222 against black; inactive, the outline's own). */
void roundOutline(const Rect &r, uint16_t line, uint16_t outer)
{
    int L = r.left, T = r.top, R = r.right - 1, B = r.bottom - 1;
    hrun(L + 3, R - 3, T, line);
    hrun(L + 3, R - 3, B, line);
    vrun(L, T + 3, B - 3, line);
    vrun(R, T + 3, B - 3, line);
    pixel(L + 1, T + 1, line);
    pixel(R - 1, T + 1, line);
    pixel(L + 1, B - 1, line);
    pixel(R - 1, B - 1, line);
    for(int h : { L + 2, R - 2 })
        for(int v : { T, B })
            pixel(h, v, outer);
    for(int h : { L, R })
        for(int v : { T + 2, B - 2 })
            pixel(h, v, outer);
}

/* The two up and down arrows of an arrow button (and a popup's arrow
   part), 7 wide, centred on h. */
void upDownArrows(int h, int vMid, uint16_t g)
{
    for(int i = 0; i < 4; i++)
    {
        hrun(h - i, h + i, vMid - 5 + i, g);
        hrun(h - i, h + i, vMid + 4 - i, g);
    }
}

/* One arrow of 7, pointing up or down, its tip at (h, tip). */
void smallArrow(int h, int tip, bool up, uint16_t g)
{
    for(int i = 0; i < 4; i++)
        hrun(h - i, h + i, up ? tip + i : tip - i, g);
}

enum class Look
{
    active,
    pressed,
    inactive
};

Look lookOf(ThemeDrawState state)
{
    switch(state)
    {
        case kThemeStateInactive:
        case kThemeStateUnavailable:
        case kThemeStateUnavailableInactive:
            return Look::inactive;
        case kThemeStatePressedUp:
        case kThemeStatePressedDown:
            return Look::pressed;
        default:
            return Look::active;
    }
}

/* A rectangle of pixels [x0, x1) x [y0, y1) in grey G, as a shape */
void rectShape(hidpi::Shapes &s, float x0, float y0, float x1, float y1, uint16_t g)
{
    s.push_back({ hidpi::Shape::Polygon, { x0, y0, x1, y0, x1, y1, x0, y1 }, 0, argbGray(g) });
}

/* Points on a quarter circle round (cx, cy) from angle A0 to A1 degrees
   (0 right, 90 down), appended to V. */
void arcPts(std::vector<float> &v, float cx, float cy, float rad, float a0, float a1)
{
    for(int i = 0; i <= 6; i++)
    {
        float a = (a0 + (a1 - a0) * i / 6) * 3.14159265f / 180;
        v.insert(v.end(), { cx + rad * std::cos(a), cy + rad * std::sin(a) });
    }
}

/* A rounded rectangle's outline from its top left corner round, closed. */
std::vector<float> roundRectPts(float l, float t, float r, float b, float rad)
{
    std::vector<float> v;
    arcPts(v, l + rad, t + rad, rad, 180, 270);
    arcPts(v, r - rad, t + rad, rad, 270, 360);
    arcPts(v, r - rad, b - rad, rad, 0, 90);
    arcPts(v, l + rad, b - rad, rad, 90, 180);
    v.insert(v.end(), { v[0], v[1] });
    return v;
}

void pushButtonPixels(const Rect &b, Look look);

/* A push button at the display scale: the face inside the rounded
   outline, the first ring following the corners, the square second ring,
   the outline */
hidpi::Shapes pushButtonShapes(const Rect &b, Look look)
{
    float L = b.left, T = b.top, R = b.right, B = b.bottom;
    hidpi::Shapes s;
    if(look == Look::inactive)
    {
        s.push_back({ hidpi::Shape::Polygon, roundRectPts(L, T, R, B, 3.5f), 0, argbGray(grey(0xd)) });
        s.push_back({ hidpi::Shape::Polyline, roundRectPts(L + 0.5f, T + 0.5f, R - 0.5f, B - 0.5f, 3), 1,
                      argbGray(grey(8)) });
        return s;
    }
    bool p = look == Look::pressed;
    s.push_back({ hidpi::Shape::Polygon, roundRectPts(L, T, R, B, 3.5f), 0, argbGray(grey(p ? 6 : 0xd)) });
    std::vector<float> tl1{ L + 1.5f, B - 3 }, br1{ R - 1.5f, T + 3 };
    arcPts(tl1, L + 3.5f, T + 3.5f, 2, 180, 270);
    tl1.insert(tl1.end(), { R - 3, T + 1.5f });
    arcPts(br1, R - 3.5f, B - 3.5f, 2, 0, 90);
    br1.insert(br1.end(), { L + 3, B - 1.5f });
    s.push_back({ hidpi::Shape::Polyline, tl1, 1, argbGray(grey(p ? 4 : 0xd)) });
    s.push_back({ hidpi::Shape::Polyline, br1, 1, argbGray(grey(p ? 8 : 7)) });
    s.push_back({ hidpi::Shape::Polyline, { L + 2.5f, B - 3, L + 2.5f, T + 2.5f, R - 3, T + 2.5f }, 1,
                  argbGray(grey(p ? 5 : 0xf)) });
    s.push_back({ hidpi::Shape::Polyline, { R - 2.5f, T + 3, R - 2.5f, B - 2.5f, L + 3, B - 2.5f }, 1,
                  argbGray(grey(p ? 7 : 0xa)) });
    s.push_back({ hidpi::Shape::Polyline, roundRectPts(L + 0.5f, T + 0.5f, R - 0.5f, B - 0.5f, 3), 1,
                  argbGray(grey(0)) });
    return s;
}

void pushButton(const Rect &b, Look look)
{
    hidpi::chrome_draw(b, hidpi::active() ? pushButtonShapes(b, look) : hidpi::Shapes{},
                       [&] { pushButtonPixels(b, look); });
}

void pushButtonPixels(const Rect &b, Look look)
{
    int L = b.left, T = b.top, R = b.right - 1, B = b.bottom - 1;
    if(look == Look::inactive)
    {
        fillGray(Rect{ (INTEGER)(T + 1), (INTEGER)(L + 1), (INTEGER)B, (INTEGER)R }, grey(0xd));
        roundOutline(b, grey(8), grey(8));
        return;
    }
    bool p = look == Look::pressed;
    /* the face, then the rings over it: the first follows the corners,
       its ends E1 (top and left) and E2 (right and bottom); the second is
       square, BR2 in its bottom-right corner; the third shows only there
       and in the top left */
    int tl1 = p ? 4 : 0xd, br1 = p ? 8 : 7, e1 = p ? 4 : 0xb, e2 = p ? 7 : 0xb;
    Ring ring2 = p ? Ring{ 5, 7, 6 } : Ring{ 0xf, 0xa, 0xd };
    int br2 = p ? 8 : 7, tl3 = p ? 5 : 0xf, br3 = p ? 7 : 0xa;
    Rect in = b;
    InsetRect(&in, 3, 3);
    fillGray(in, grey(p ? 6 : 0xd));
    hrun(L + 2, R - 2, T + 1, grey(tl1));
    vrun(L + 1, T + 2, B - 2, grey(tl1));
    vrun(R - 1, T + 2, B - 2, grey(br1));
    hrun(L + 2, R - 2, B - 1, grey(br1));
    for(Point e : { Point{ (INTEGER)(T + 1), (INTEGER)(L + 2) }, Point{ (INTEGER)(T + 1), (INTEGER)(R - 2) },
                    Point{ (INTEGER)(T + 2), (INTEGER)(L + 1) }, Point{ (INTEGER)(B - 2), (INTEGER)(L + 1) } })
        pixel(e.h, e.v, grey(e1));
    pixel(R - 1, T + 2, grey(e2));
    pixel(L + 2, B - 1, grey(e2));
    squareRing(b, 2, ring2);
    if(p)
        pixel(L + 2, T + 2, grey(4));
    pixel(R - 2, B - 2, grey(br2));
    pixel(L + 3, T + 3, grey(tl3));
    pixel(R - 3, B - 3, grey(br3));
    roundOutline(b, grey(0), grey(2));
}

/* An arrow button's body (and a popup button's arrow part) over [L, R]:
   corners cut one pixel deep */
void arrowBody(int L, int T, int R, int B, Look look)
{
    if(look == Look::inactive)
    {
        fillGray(Rect{ (INTEGER)(T + 1), (INTEGER)(L + 1), (INTEGER)B, (INTEGER)R }, grey(0xd));
        return;
    }
    bool p = look == Look::pressed;
    fillGray(Rect{ (INTEGER)(T + 3), (INTEGER)(L + 3), (INTEGER)(B - 2), (INTEGER)(R - 2) }, grey(p ? 6 : 0xd));
    /* ring 1 */
    hrun(L + 2, R - 3, T + 1, grey(p ? 4 : 0xd));
    pixel(R - 2, T + 1, grey(p ? 4 : 0xb));
    vrun(L + 1, T + 2, B - 2, grey(p ? 4 : 0xd));
    vrun(R - 1, T + 2, B - 2, grey(p ? 8 : 7));
    if(!p)
        pixel(R - 1, T + 2, grey(0xa));
    hrun(L + 2, R - 2, B - 1, grey(p ? 8 : 7));
    /* ring 2 */
    hrun(L + 2, R - 3, T + 2, grey(p ? 5 : 0xf));
    pixel(R - 2, T + 2, grey(p ? 6 : 0xd));
    vrun(L + 2, T + 3, B - 3, grey(p ? 5 : 0xf));
    pixel(L + 2, B - 2, grey(p ? 6 : 0xd));
    vrun(R - 2, T + 3, B - 3, grey(p ? 7 : 0xa));
    hrun(L + 3, R - 3, B - 2, grey(p ? 7 : 0xa));
    pixel(R - 2, B - 2, grey(p ? 8 : 7));
}

void arrowButton(const Rect &b, Look look)
{
    int L = b.left, T = b.top, R = L + 21, B = b.bottom - 1;
    uint16_t line = grey(look == Look::inactive ? 8 : 0);
    arrowBody(L, T, R, B, look);
    hrun(L + 2, R - 2, T, line);
    hrun(L + 2, R - 2, B, line);
    vrun(L, T + 2, B - 2, line);
    vrun(R, T + 2, B - 2, line);
    for(int h : { L + 1, R - 1 })
        for(int v : { T + 1, B - 1 })
            pixel(h, v, line);
    if(look == Look::inactive)
        pixel(L, T + 1, grey(0xa));
    pixel(L, B - 1, grey(look == Look::pressed ? 7 : 0xa));
    upDownArrows((L + R + 1) / 2 - 1, (T + B + 1) / 2, look == Look::pressed ? kWhite : line);
}

/* A popup button: a push button's left end and an arrow part of 22 at
   the right, a line between */
void popupButton(const Rect &b, Look look)
{
    int L = b.left, T = b.top, R = b.right - 1, B = b.bottom - 1, A = b.right - 22;
    bool p = look == Look::pressed;
    if(look == Look::inactive)
    {
        fillGray(Rect{ (INTEGER)(T + 1), (INTEGER)(L + 1), (INTEGER)B, (INTEGER)R }, grey(0xd));
        vrun(A, T + 1, B - 1, grey(0xa));
    }
    else
    {
        fillGray(Rect{ (INTEGER)(T + 2), (INTEGER)(L + 2), (INTEGER)(B - 1), (INTEGER)A }, grey(p ? 6 : 0xd));
        hrun(L + 2, A - 1, T + 1, grey(p ? 5 : 0xf));
        vrun(L + 1, T + 2, B - 2, grey(p ? 5 : 0xf));
        if(p)
            pixel(L + 2, T + 2, grey(5));
        hrun(L + 2, A - 1, B - 1, grey(p ? 7 : 0xa));
        arrowBody(A, T, R, B, look);
        /* the arrow part's ring 1 runs on over the line, its left side
           a column in */
        pixel(A, T + 1, grey(p ? 6 : 0xd));
        vrun(A, T + 2, B - 1, grey(p ? 7 : 0xa));
        vrun(A + 1, T + 1, B - 2, grey(p ? 4 : 0xd));
        pixel(A + 1, B - 1, grey(p ? 4 : 0xb));
    }
    roundOutline(b, grey(look == Look::inactive ? 8 : 0), grey(look == Look::inactive ? 8 : 2));
    upDownArrows((A + R + 1) / 2 - 1, (T + B + 1) / 2,
                 p ? kWhite : grey(look == Look::inactive ? 8 : 0));
}

/* A check box's or radio button's 12 square, two in from the left,
   centred down the bounds */
Rect markBox(const Rect &b)
{
    int top = (b.top + b.bottom - 12) / 2;
    return Rect{ (INTEGER)top, (INTEGER)(b.left + 2), (INTEGER)(top + 12), (INTEGER)(b.left + 14) };
}

/* The check box's tick at the display scale: its shadow, then the tick */
hidpi::Shapes tickShapes(const Rect &r, Look look)
{
    float L = r.left, T = r.top;
    hidpi::Shapes s;
    if(look != Look::inactive)
        s.push_back({ hidpi::Shape::Polyline, { L + 6.5f, T + 10, L + 12.5f, T + 4 }, 1.5f, argbGray(grey(8)) });
    s.push_back({ hidpi::Shape::Polyline, { L + 2.5f, T + 6, L + 5.5f, T + 9, L + 11.5f, T + 3 }, 2,
                  argbGray(grey(look == Look::inactive ? 8 : 0)) });
    return s;
}

void checkBox(const Rect &b, Look look, ThemeButtonValue value)
{
    Rect r = markBox(b);
    switch(look)
    {
        case Look::active:
            bevelBox(r, { { 0, 0, 0 }, { 0xf, 8, 0xd } }, 0xd);
            break;
        case Look::pressed:
            bevelBox(r, { { 0, 0, 0 }, { 5, 9, 7 } }, 7);
            break;
        case Look::inactive:
            bevelBox(r, { { 8, 8, 8 } }, 0xd);
            break;
    }
    uint16_t ink = grey(look == Look::inactive ? 8 : 0);
    int L = r.left, T = r.top;
    if(value == kThemeButtonOn)
    {
        /* a tick, two pixels thick, out over the top right corner
           (8.6's is anti-aliased: an approximation) */
        Rect tickBox = { (INTEGER)(T + 1), (INTEGER)(L + 1), (INTEGER)(T + 12), (INTEGER)(L + 13) };
        hidpi::chrome_draw(tickBox, hidpi::active() ? tickShapes(r, look) : hidpi::Shapes{}, [&] {
            for(int k = 0; k < 2; k++)
            {
                lineGray(L + 2, T + 5 + k, L + 5, T + 8 + k, ink);
                lineGray(L + 5, T + 8 + k, L + 11, T + 2 + k, ink);
            }
            if(look != Look::inactive)
            {
                lineGray(L + 6, T + 9, L + 12, T + 3, grey(7));
                lineGray(L + 6, T + 10, L + 12, T + 4, grey(0xa));
            }
        });
    }
    else if(value == kThemeButtonMixed)
    {
        /* a bar of two rows, its shadow right and below */
        hrun(L + 3, L + 8, T + 5, ink);
        hrun(L + 3, L + 8, T + 6, ink);
        if(look != Look::inactive)
        {
            vrun(L + 9, T + 5, T + 6, grey(0xa));
            hrun(L + 4, L + 9, T + 7, grey(0xa));
        }
    }
}

/* A radio button: a sphere lit from the top left, its shade stepping
   along the diagonal (the ring just inside the outline darker), in a
   round outline whose ends soften */
void radioPixels(const Rect &r, Look look, ThemeButtonValue value);

/* The radio button at the display scale: the sphere as a radial gradient
   lit from the top left, its outline, the dot */
hidpi::Shapes radioShapes(const Rect &r, Look look, ThemeButtonValue value)
{
    float cx = r.left + 6, cy = r.top + 6;
    uint32_t line = argbGray(grey(look == Look::inactive ? 8 : 0));
    hidpi::Shapes s;
    hidpi::Shape body{ hidpi::Shape::Circle, { cx, cy, 5.5f }, 0, argbGray(grey(0xd)) };
    if(look != Look::inactive)
    {
        body.gx = r.left + 3.5f;
        body.gy = r.top + 3.5f;
        body.gr = 8.5f;
        if(look == Look::pressed)
            body.colors = { argbGray(grey(4)), argbGray(grey(5)), argbGray(grey(7)), argbGray(grey(9)) };
        else
            body.colors = { argbGray(grey(0xf)), argbGray(grey(0xe)), argbGray(grey(0xb)), argbGray(grey(8)) };
        body.pos = { 0, 0.3f, 0.6f, 0.9f };
    }
    s.push_back(body);
    s.push_back({ hidpi::Shape::Circle, { cx, cy, 5.5f }, 1, line });
    if(value == kThemeButtonOn)
        s.push_back({ hidpi::Shape::Circle, { cx, cy, 3 }, 0, line });
    else if(value == kThemeButtonMixed)
        s.push_back({ hidpi::Shape::Polygon, { cx - 3, cy - 1, cx + 3, cy - 1, cx + 3, cy + 1, cx - 3, cy + 1 }, 0, line });
    return s;
}

void radioButton(const Rect &b, Look look, ThemeButtonValue value)
{
    Rect r = markBox(b);
    hidpi::chrome_draw(r, hidpi::active() ? radioShapes(r, look, value) : hidpi::Shapes{},
                       [&] { radioPixels(r, look, value); });
}

void radioPixels(const Rect &r, Look look, ThemeButtonValue value)
{
    /* by h + v from the box's corner: inside, and on the rim */
    static const uint8_t litIn[] = { 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xe, 0xf, 0xf, 0xe, 0xe, 0xd, 0xd, 0xb, 0xb, 0xa, 0xa };
    static const uint8_t litRim[] = { 0xd, 0xd, 0xd, 0xd, 0xb, 0xd, 0xd, 0xd, 0xb, 0xb, 0xd, 8, 0xb, 0xb, 8, 8, 8, 8, 8, 8 };
    static const uint8_t darkIn[] = { 4, 4, 4, 4, 4, 4, 5, 5, 6, 6, 7, 7, 7, 8, 8, 9, 9 };
    static const uint8_t darkRim[] = { 4, 4, 4, 4, 4, 4, 4, 4, 4, 6, 6, 7, 7, 9, 9, 9, 9, 0xb, 0xb, 0xb };
    bool p = look == Look::pressed;
    const uint8_t *in = p ? darkIn : litIn, *rim = p ? darkRim : litRim;
    for(int y = 1; y < 11; y++)
        for(int x = 1; x < 11; x++)
        {
            /* the pixel's centre from the box's, in half pixels */
            int dx = 2 * x - 11, dy = 2 * y - 11, d2 = dx * dx + dy * dy;
            if(d2 >= 100)
                continue;
            int s = x + y;
            int g = look == Look::inactive ? 0xd : d2 >= 64 ? rim[s] : in[std::min(s, 16)];
            if(look == Look::active && s == 9 && d2 >= 50)
                g = 0xf; /* the highlight's ends, out to the rim */
            pixel(r.left + x, r.top + y, grey(g));
        }
    /* the outline: runs of 4 across the top, bottom and sides, two
       pixels at a time round the corners; the pixels where it turns take
       the shade beside them (by corner: top left, top right, bottom left,
       bottom right) */
    uint16_t line = grey(look == Look::inactive ? 8 : 0);
    int L = r.left, T = r.top;
    hrun(L + 4, L + 7, T, line);
    hrun(L + 4, L + 7, T + 11, line);
    vrun(L, T + 4, T + 7, line);
    vrun(L + 11, T + 4, T + 7, line);
    struct Turn
    {
        int8_t h, v;            /* in the top-left corner; mirrored */
        uint8_t lit[4], dark[4];
    };
    static const Turn turns[] = {
        { 4, 0, { 4, 4, 4, 4 }, { 2, 2, 4, 4 } },  /* the runs' ends */
        { 0, 4, { 4, 4, 4, 4 }, { 2, 4, 2, 4 } },
        { 3, 1, { 5, 4, 5, 4 }, { 0, 4, 4, 5 } },
        { 1, 3, { 5, 4, 5, 4 }, { 0, 4, 2, 5 } },
        { 2, 1, { 0, 0, 0, 0 }, { 2, 0, 2, 0 } },
        { 1, 2, { 0, 0, 0, 0 }, { 2, 0, 4, 0 } },
    };
    for(const Turn &t : turns)
        for(int c = 0; c < 4; c++)
        {
            int h = c & 1 ? 11 - t.h : t.h, v = c & 2 ? 11 - t.v : t.v;
            pixel(L + h, T + v, look == Look::inactive ? line : grey((p ? t.dark : t.lit)[c]));
        }
    if(value == kThemeButtonOn)
    {
        /* a dot of 6 (8.6 also darkens the sphere round it: not yet) */
        Rect dot = { (INTEGER)(T + 3), (INTEGER)(L + 3), (INTEGER)(T + 9), (INTEGER)(L + 9) };
        gray(line);
        PaintOval(&dot);
    }
    else if(value == kThemeButtonMixed)
    {
        hrun(L + 3, L + 8, T + 5, line);
        hrun(L + 3, L + 8, T + 6, line);
    }
}

/* A disclosure triangle, 7 by 11, pointing right (or down: the same
   turned), in the accent colours with a shadow below right */
void disclosureTrianglePixels(const Rect &b, Look look, ThemeButtonValue value);

/* The triangle at the display scale: its black outline round the face,
   lit along the base, and its shadow (the 1x one's corners) */
hidpi::Shapes disclosureTriangleShapes(const Rect &b, Look look, ThemeButtonValue value)
{
    bool down = value == kThemeDisclosureDown;
    /* (i, j) as the pixels: i out from the base, j across it */
    float L = b.left + 4, T = (b.top + b.bottom - 11) / 2;
    float dh = b.left, dv = (b.top + b.bottom - 7) / 2 + 2;
    auto pts = [&](std::initializer_list<float> ij) {
        std::vector<float> v;
        for(auto it = ij.begin(); it != ij.end(); it += 2)
        {
            float i = it[0], j = it[1];
            v.push_back(down ? dh + j : L + i);
            v.push_back(down ? dv + i : T + j);
        }
        return v;
    };
    auto rgb = [](uint16_t r, uint16_t g, uint16_t bl) { return argbRGB(RGBColor{ r, g, bl }); };
    hidpi::Shapes s;
    if(look == Look::active)
    {
        /* beyond the far edge from its middle: #888 then #bbb */
        s.push_back({ hidpi::Shape::Polygon, pts({ 5, 6, 7, 6, 2, 11, 2, 12, 1, 12, 1, 10 }), 0, argbGray(0xBBBB) });
        s.push_back({ hidpi::Shape::Polyline, pts({ 5.5f, 6.5f, 1.5f, 10.5f }), 1, argbGray(0x8888) });
    }
    uint32_t outline = argbGray(look == Look::inactive ? 0xAAAA : 0);
    s.push_back({ hidpi::Shape::Polygon, pts({ 0, 0, 6, 5.5f, 6, 5.5f, 0, 11 }), 0, outline });
    if(look != Look::inactive)
    {
        uint32_t face = look == Look::pressed ? rgb(0x3333, 0x3333, 0x9999) : rgb(0x9999, 0x9999, 0xFFFF);
        s.push_back({ hidpi::Shape::Polygon, pts({ 1, 1.6f, 4.6f, 5.5f, 1, 9.4f }), 0, face });
        if(look == Look::active)
        {
            s.push_back({ hidpi::Shape::Polyline, pts({ 1.5f, 2, 1.5f, 8.5f }), 1, rgb(0xCCCC, 0xCCCC, 0xFFFF) });
            s.push_back({ hidpi::Shape::Polyline, pts({ 4.2f, 5.8f, 1.8f, 8.6f }), 1, rgb(0x6666, 0x6666, 0xCCCC) });
        }
    }
    return s;
}

void disclosureTriangle(const Rect &b, Look look, ThemeButtonValue value)
{
    bool down = value == kThemeDisclosureDown;
    Rect box = down ? Rect{ (INTEGER)((b.top + b.bottom - 7) / 2 + 2), b.left, (INTEGER)((b.top + b.bottom - 7) / 2 + 10),
                            (INTEGER)(b.left + 12) }
                    : Rect{ (INTEGER)((b.top + b.bottom - 11) / 2), (INTEGER)(b.left + 4),
                            (INTEGER)((b.top + b.bottom - 11) / 2 + 12), (INTEGER)(b.left + 12) };
    hidpi::chrome_draw(box, hidpi::active() ? disclosureTriangleShapes(b, look, value) : hidpi::Shapes{},
                       [&] { disclosureTrianglePixels(b, look, value); });
}

void disclosureTrianglePixels(const Rect &b, Look look, ThemeButtonValue value)
{
    int L = b.left + 4, T = (b.top + b.bottom - 11) / 2;
    bool down = value == kThemeDisclosureDown;
    RGBColor lit = { 0xCCCC, 0xCCCC, 0xFFFF }, face = { 0x9999, 0x9999, 0xFFFF },
             shade = { 0x6666, 0x6666, 0xCCCC }, pressed = { 0x3333, 0x3333, 0x9999 };
    /* (i, j): j across the triangle's base (0-10), i out from it (0-5) */
    auto plot = [&](int i, int j, RGBColor &c) {
        RGBForeColor(&c);
        /* open, it points down from the rect's left (8.6: appr controls.64.triangle.value1) */
        int h = down ? b.left + j : L + i, v = down ? (b.top + b.bottom - 7) / 2 + 2 + i : T + j;
        MoveTo(h, v);
        LineTo(h, v);
    };
    RGBColor black = { 0, 0, 0 }, inactive = { 0xAAAA, 0xAAAA, 0xAAAA }, s1 = { 0x8888, 0x8888, 0x8888 },
             s2 = { 0xBBBB, 0xBBBB, 0xBBBB };
    for(int j = 0; j <= 10; j++)
    {
        int len = j <= 5 ? j : 10 - j; /* the far edge's i */
        for(int i = 0; i <= len; i++)
        {
            RGBColor *c;
            if(look == Look::inactive)
                c = &inactive;
            else if(i == 0 || i == len)
                c = &black;
            else if(look == Look::pressed)
                c = &pressed;
            else if(i == 1 && j >= 5 && i == len - 1)
                c = &face;
            else if(i == 1)
                c = &lit;
            else if(j >= 5 && i == len - 1)
                c = &shade;
            else
                c = &face;
            plot(i, j, *c);
        }
        if(look == Look::active && j >= 6)
        {
            plot(len + 1, j, s1);
            plot(len + 2, j, s2);
        }
    }
    if(look == Look::active)
        plot(1, 11, s2);
}

/* The little arrows: two halves of 13 by 12, the line between shared */
void incDecButton(const Rect &b, ThemeDrawState state)
{
    Look look = lookOf(state);
    int L = b.left, T = b.top, R = L + 12, M = T + 11, B = T + 22;
    uint16_t line = grey(look == Look::inactive ? 8 : 0);
    for(int half = 0; half < 2; half++)
    {
        int t = half ? M : T, bt = half ? B : M;
        bool p = look == Look::pressed && (state == kThemeStatePressedDown) == (half == 1);
        Rect r = { (INTEGER)(t + 1), (INTEGER)(L + 1), (INTEGER)bt, (INTEGER)R };
        if(look == Look::inactive)
            fillGray(r, grey(0xd));
        else if(p)
            bevelBox(r, { { 5, 9, 7 } }, 7);
        else
            bevelBox(r, { { 0xf, 0xa, 0xd } }, 0xd);
        smallArrow((L + R) / 2, half ? bt - 4 : t + 4, !half, p ? kWhite : line);
    }
    hrun(L + 1, R - 1, T, line);
    hrun(L, R, M, line);
    hrun(L + 1, R - 1, B, line);
    vrun(L, T + 1, B - 1, line);
    vrun(R, T + 1, B - 1, line);
}

void drawButton(const Rect &b, ThemeButtonKind kind, const ThemeButtonDrawInfo &info)
{
    Look look = lookOf(info.state);
    switch(kind)
    {
        case kThemePushButton:
            pushButton(b, look);
            if(info.adornment & kThemeAdornmentDefault)
            {
                Rect r = b;
                InsetRect(&r, -4, -4);
                PenSize(3, 3);
                frameGray(r, grey(look == Look::inactive ? 8 : 0));
                PenSize(1, 1);
            }
            break;
        case kThemeCheckBox:
            checkBox(b, look, info.value);
            break;
        case kThemeRadioButton:
            radioButton(b, look, info.value);
            break;
        case kThemeSmallBevelButton:
        case kThemeMediumBevelButton:
        case kThemeLargeBevelButton:
        {
            bool on = info.value == kThemeButtonOn;
            if(look == Look::inactive)
                bevelBox(b, { { 0xa, 9, 0xa } }, 0xd);
            else if(look == Look::pressed || on)
            {
                if(kind == kThemeSmallBevelButton)
                    bevelBox(b, { { 1, 4, 2 }, { 5, 0xa, 7 } }, 8);
                else if(kind == kThemeMediumBevelButton)
                    bevelBox(b, { { 1, 4, 2 }, { 4, 0xb, 7 }, { 6, 9, 8 } }, 8);
                else
                    bevelBox(b, { { 1, 4, 2 }, { 3, 0xb, 4 }, { 4, 0xa, 7 }, { 6, 9, 8 } }, 8);
            }
            else if(kind == kThemeSmallBevelButton)
                bevelBox(b, { { 6, 3, 5 }, { 0xf, 8, 0xc } }, 0xc);
            else if(kind == kThemeMediumBevelButton)
                bevelBox(b, { { 6, 3, 5 }, { 0xc, 7, 0xa }, { 0xf, 9, 0xc } }, 0xc);
            else
                bevelBox(b, { { 6, 3, 5 }, { 0xc, 5, 0xa }, { 0xe, 7, 0xb }, { 0xf, 9, 0xc } }, 0xc);
            break;
        }
        case kThemePopupButton:
            popupButton(b, look);
            break;
        case kThemeArrowButton:
            arrowButton(b, look);
            break;
        case kThemeDisclosureButton:
            disclosureTriangle(b, look, info.value);
            break;
        case kThemeIncDecButton:
            incDecButton(b, info.state);
            break;
    }
    if(look != Look::inactive && (info.adornment & kThemeAdornmentFocus))
    {
        Rect f = b;
        InsetRect(&f, -2, -2);
        PenSize(2, 2);
        frameGray(f, kDark);
        PenSize(1, 1);
    }
}

/* Calls a definition's erase proc for r, if it gave one. */
void eraseWith(ThemeEraseUPP proc, const Rect &r, UInt32 data)
{
    if(!proc)
        return;
    SInt16 depth;
    Boolean color;
    portDepth(&depth, &color);
    proc(&r, data, depth, color);
}
}

/* ── Tracks ──────────────────────────────────────────────────────────── */

namespace
{
void indeterminateBarPixels(const Rect &bounds, int phase, bool active);

/* Each row's stripe colours, top to bottom: the accent's and the grey's */
const RGBColor indeterminateBlue[] = {
    { 0x0000, 0x0000, 0x8888 }, { 0x3333, 0x3333, 0x9999 }, { 0x6666, 0x6666, 0xCCCC }, { 0x9999, 0x9999, 0xFFFF },
    { 0xCCCC, 0xCCCC, 0xFFFF }, { 0x9999, 0x9999, 0xFFFF }, { 0x6666, 0x6666, 0xCCCC }, { 0x3333, 0x3333, 0x9999 },
    { 0x0000, 0x0000, 0x8888 }, { 0x0000, 0x0000, 0x5555 },
};
const uint16_t indeterminateGrey[] = { 0x5555, 0x7777, 0xAAAA, 0xBBBB, 0xFFFF, 0xDDDD, 0xBBBB, 0x9999, 0x7777, 0x5555 };

/* Where the stripes are: pixel (x, y) of the inside (from its top left)
   is in a blue one when (x - y - 8 - 4 phase) mod 16 < 8 */
int indeterminateOffset(int phase)
{
    return 8 + 4 * phase;
}

/* The indeterminate bar at the display scale: the frame's lines, the grey
   and blue stripes as parallelograms shaded row by row (a vertical
   gradient through the rows' colours) */
hidpi::Shapes indeterminateBarShapes(const Rect &bounds, int phase, bool active)
{
    float L = bounds.left, T = bounds.top, R = bounds.right - 2, B = bounds.bottom - 2; /* inclusive */
    float x0 = L + 2, y0 = T + 2, x1 = R, y1 = B; /* the inside: [x0, x1) x [y0, y1) */
    hidpi::Shapes s;
    auto shaded = [&](std::vector<float> pts, bool blue) {
        hidpi::Shape sh{ hidpi::Shape::Polygon, std::move(pts), 0, 0xFF000000 };
        sh.gx = x0, sh.gy = y0, sh.gx2 = x0, sh.gy2 = y1;
        int n = (int)std::size(indeterminateGrey);
        for(int i = 0; i < n; i++)
        {
            if(!active)
                sh.colors.push_back(blue ? argbRGB({ 0x9999, 0x9999, 0xFFFF }) : argbGray(0xDDDD));
            else
                sh.colors.push_back(blue ? argbRGB(indeterminateBlue[i]) : argbGray(indeterminateGrey[i]));
            sh.pos.push_back((i + 0.5f) / n);
        }
        s.push_back(sh);
    };
    shaded({ x0, y0, x1, y0, x1, y1, x0, y1 }, false);
    /* the blue stripes: x - y = off (mod 16) to off + 8, from the top row's */
    int off = indeterminateOffset(phase) % 16, h = (int)(y1 - y0);
    for(int a = off - 16 - h; a < x1 - x0 + 16; a += 16)
        shaded({ x0 + a, y0, x0 + a + 8, y0, x0 + a + 8 + h, y1, x0 + a + h, y1 }, true);
    if(active)
    {
        rectShape(s, L, T, R + 1, T + 1, 0xAAAA);
        rectShape(s, L, T, L + 1, B + 1, 0xAAAA);
    }
    uint16_t frame = active ? kBlack : 0x5555;
    rectShape(s, L + 1, T + 1, R + 1, T + 2, frame);
    rectShape(s, L + 1, B, R + 1, B + 1, frame);
    rectShape(s, L + 1, T + 1, L + 2, B + 1, frame);
    rectShape(s, R, T + 1, R + 1, B + 1, frame);
    return s;
}

/* A medium indeterminate bar as 8.6 draws it (appr draw32.indeterminate.*):
   the determinate bar's frame round diagonal stripes 8 wide, blue and
   grey, each row its own shade (lightest across the middle), moving 4
   pixels left a phase; inactive #ddd and #99f in a #555 frame. */
void indeterminateBar(const Rect &bounds, int phase, bool active)
{
    Rect box = { bounds.top, bounds.left, (INTEGER)(bounds.bottom - 1), (INTEGER)(bounds.right - 1) };
    hidpi::chrome_draw(box, hidpi::active() ? indeterminateBarShapes(bounds, phase, active) : hidpi::Shapes{},
                       [&] { indeterminateBarPixels(bounds, phase, active); });
}

void indeterminateBarPixels(const Rect &bounds, int phase, bool active)
{
    int L = bounds.left, T = bounds.top, R = bounds.right - 2, B = bounds.bottom - 2; /* inclusive */
    if(active)
    {
        lineGray(L, T, R, T, 0xAAAA);
        lineGray(L, T, L, B, 0xAAAA);
    }
    frameGray(Rect{ (INTEGER)(T + 1), (INTEGER)(L + 1), (INTEGER)(B + 1), (INTEGER)(R + 1) }, active ? kBlack : 0x5555);
    int off = indeterminateOffset(phase);
    for(int y = T + 2; y < B; y++)
    {
        int r = y - (T + 2), n = (int)std::size(indeterminateGrey);
        RGBColor blue = active ? indeterminateBlue[std::min(r, n - 1)] : RGBColor{ 0x9999, 0x9999, 0xFFFF };
        uint16_t g = active ? indeterminateGrey[std::min(r, n - 1)] : 0xDDDD;
        for(int x = L + 2; x < R; x++)
        {
            int m = (((x - (L + 2)) - r - off) % 16 + 16) % 16;
            /* a stripe that would start in the last column isn't drawn
               (8.6: appr draw32.indeterminate.medium.0, row 7) */
            if(m < 8 && !(m == 0 && x == R - 1))
                lineRGB(x, y, x, y, blue);
            else
                lineGray(x, y, x, y, g);
        }
    }
}

/* A determinate horizontal progress bar in BOUNDS, as 8.6 draws CDEF 80
   (tests/guest/difftest appr controls.80.*): the bar a pixel short of
   the bounds' right and bottom, a #aaa line over and left of a black
   frame; the done part a tube in the accent ramp (#005 #339 #66c #99f
   #ccf #eee), lit from the top, its right end darkening, a black line
   where it ends; the rest grey, sunk. Inactive: #555 frame and line,
   #99f done, #ccc rest. */
void progressBar(const Rect &bounds, int32_t min, int32_t max, int32_t value, bool active)
{
    static const RGBColor ramp[] = {
        { 0x0000, 0x0000, 0x5555 }, { 0x3333, 0x3333, 0x9999 }, { 0x6666, 0x6666, 0xCCCC },
        { 0x9999, 0x9999, 0xFFFF }, { 0xCCCC, 0xCCCC, 0xFFFF }, { 0xEEEE, 0xEEEE, 0xEEEE },
    };
    int L = bounds.left, T = bounds.top, R = bounds.right - 2, B = bounds.bottom - 2; /* inclusive */
    int fx = L + 2 + (int)((int64_t)(R - 1 - (L + 2) + 1) * (std::clamp(value, min, max) - (int64_t)min) / ((int64_t)max - min));
    uint16_t frame = active ? kBlack : 0x5555;
    if(active)
    {
        lineGray(L, T, R, T, 0xAAAA);
        lineGray(L, T, L, B, 0xAAAA);
    }
    Rect f = { (INTEGER)(T + 1), (INTEGER)(L + 1), (INTEGER)(B + 1), (INTEGER)(R + 1) };
    frameGray(f, frame);
    if(!active)
    {
        RGBColor done = { 0x9999, 0x9999, 0xFFFF };
        RGBForeColor(&done);
        Rect d = { (INTEGER)(T + 2), (INTEGER)(L + 2), (INTEGER)(B - 0), (INTEGER)fx };
        PaintRect(&d);
        fillGray(Rect{ (INTEGER)(T + 2), (INTEGER)(fx + 1), (INTEGER)B, (INTEGER)R }, 0xCCCC);
        lineGray(fx, T + 2, fx, B - 1, frame);
        return;
    }
    /* the done part, row by row (r 0 at the top) */
    int rows = B - 1 - (T + 2) + 1;
    for(int r = 0; r < rows; r++)
    {
        int mid = std::max(0, 5 - std::abs(r - 4));
        if(r == rows - 1)
            mid = 0;
        int y = T + 2 + r;
        if(fx - 1 >= L + 2)
            lineRGB(L + 2, y, fx - 1, y, ramp[mid]);
        auto px = [&](int x, int idx) {
            if(x >= L + 2 && x < fx)
                lineRGB(x, y, x, y, ramp[idx]);
        };
        px(L + 3, std::min(5, mid + 1));
        px(L + 2, 2);
        px(fx - 3, std::min(mid, 4));
        px(fx - 2, r < 2 ? mid : std::min(mid, 1));
        px(fx - 1, r == 0 ? 1 : 0);
    }
    lineGray(fx, T + 2, fx, B - 1, kBlack);
    /* the rest: sunk */
    if(fx + 1 <= R - 1)
    {
        lineGray(fx + 1, T + 2, fx + 1, B - 1, 0x5555);
        fillGray(Rect{ (INTEGER)(T + 3), (INTEGER)(fx + 2), (INTEGER)(B - 1), (INTEGER)(R - 1) }, 0xBBBB);
        lineGray(fx + 2, T + 2, R - 2, T + 2, 0x8888);
        lineGray(fx + 2, T + 2, fx + 2, B - 1, 0x8888);
        lineGray(R - 1, T + 2, R - 1, T + 2, 0xBBBB);
        lineGray(R - 1, T + 3, R - 1, B - 1, 0xDDDD);
        lineGray(fx + 3, B - 1, R - 1, B - 1, 0xDDDD);
    }
}
}

OSStatus Executor::C_DrawThemeTrack(const ThemeTrackDrawInfo *drawInfo, RgnHandle rgnGhost,
                                    ThemeEraseUPP eraseProc, UInt32 eraseData)
{
    if(!drawInfo)
        return paramErr;
    TrackGeometry g = trackGeometry(*drawInfo);
    bool active = drawInfo->enableState == kThemeTrackActive;
    DrawGuard guard;
    eraseWith(eraseProc, drawInfo->bounds, eraseData);
    switch(drawInfo->kind)
    {
        case kThemeScrollBar:
        case kThemeSmallScrollBar:
        {
            ThemeTrackPressState press = drawInfo->trackInfo.scrollbar.pressState;
            if(active && g.hasThumb)
                drawScrollTrack(g, press & kThemeThumbPressed);
            else
            {
                Rect t = g.track;
                frameGray(t, active || drawInfo->enableState == kThemeTrackNothingToScroll ? kBlack : kShadow);
                InsetRect(&t, 1, 1);
                fillGray(t, kLight);
            }
            break;
        }
        case kThemeSlider:
        {
            Thumb shape = thumbShape(*drawInfo);
            sliderChannel(g, shape, active);
            if(g.hasThumb)
                sliderThumb(g, shape, active, drawInfo->trackInfo.slider.pressState & kThemeThumbPressed);
            break;
        }
        default:
        {
            /* progress bars: horizontal determinate ones platinum, the rest
               the done part in the accent colour */
            if((drawInfo->kind == kThemeMediumProgressBar || drawInfo->kind == kThemeLargeProgressBar)
               && g.horizontal && drawInfo->max > drawInfo->min)
            {
                progressBar(g.bounds, drawInfo->min, drawInfo->max, drawInfo->value, active);
                break;
            }
            /* 8.6 draws the medium indeterminate bar; the large one not at
               all (appr draw32.indeterminate.large.*) */
            if(drawInfo->kind == kThemeMediumIndeterminateBar && g.horizontal)
            {
                indeterminateBar(g.bounds, drawInfo->trackInfo.progress.phase, active);
                break;
            }
            if(drawInfo->kind == kThemeLargeIndeterminateBar)
                break;
            Rect r = g.bounds;
            frameGray(r, active ? kBlack : kShadow);
            InsetRect(&r, 1, 1);
            fillGray(r, kLight);
            if(drawInfo->kind == kThemeMediumProgressBar || drawInfo->kind == kThemeLargeProgressBar)
            {
                int32_t min = drawInfo->min, max = drawInfo->max;
                if(max > min)
                {
                    Rect done = r;
                    int len = g.hi(r) - g.lo(r);
                    int at = g.lo(r) + (int64_t)len * (std::clamp<int32_t>(drawInfo->value, min, max) - (int64_t)min) / ((int64_t)max - min);
                    g.setAxis(done, g.lo(r), at);
                    RGBColor accent = { 0x6666, 0x6666, 0xCCCC };
                    RGBForeColor(&accent);
                    PaintRect(&done);
                }
            }
            else
            {
                /* indeterminate: diagonal stripes, moving with the phase */
                RgnHandle clip = NewRgn();
                GetClip(clip);
                RgnHandle bar = NewRgn();
                RectRgn(bar, &r);
                SectRgn(clip, bar, bar);
                SetClip(bar);
                gray(kShadow);
                int phase = drawInfo->trackInfo.progress.phase % 16;
                for(int x = r.left - (r.bottom - r.top) - 16 + phase; x < r.right; x += 16)
                    for(int k = 0; k < 6; k++)
                    {
                        MoveTo(x + k, r.bottom);
                        LineTo(x + k + (r.bottom - r.top), r.top);
                    }
                SetClip(clip);
                DisposeRgn(bar);
                DisposeRgn(clip);
            }
            break;
        }
    }
    if(rgnGhost && g.hasThumb)
    {
        PenPat(&qdGlobals().gray);
        gray(kBlack);
        FrameRgn(rgnGhost);
        PenNormal();
    }
    return noErr;
}

Boolean Executor::C_HitTestThemeTrack(const ThemeTrackDrawInfo *drawInfo, Point mousePoint,
                                      GUEST<ControlPartCode> *partHit)
{
    if(!drawInfo)
        return false;
    ControlPartCode part = hitTrack(trackGeometry(*drawInfo), mousePoint);
    /* the arrows are HitTestThemeScrollBarArrows's */
    if(part == inUpButton || part == inDownButton)
        part = kControlNoPart;
    if(partHit)
        *partHit = part;
    return part != kControlNoPart;
}

OSStatus Executor::C_GetThemeTrackBounds(const ThemeTrackDrawInfo *drawInfo, Rect *bounds)
{
    if(!drawInfo || !bounds)
        return paramErr;
    *bounds = trackGeometry(*drawInfo).bounds;
    return noErr;
}

OSStatus Executor::C_GetThemeTrackThumbRgn(const ThemeTrackDrawInfo *drawInfo, RgnHandle thumbRgn)
{
    if(!drawInfo || !thumbRgn)
        return paramErr;
    if(isProgress(drawInfo->kind))
        return paramErr; /* no thumb: 8.6 leaves the output alone */
    TrackGeometry g = trackGeometry(*drawInfo);
    if(g.hasThumb && drawInfo->kind == kThemeSlider)
        sliderThumbRgn(g, thumbShape(*drawInfo), thumbRgn);
    else if(g.hasThumb)
        RectRgn(thumbRgn, &g.thumb);
    else
        SetEmptyRgn(thumbRgn);
    return noErr;
}

OSStatus Executor::C_GetThemeTrackDragRect(const ThemeTrackDrawInfo *drawInfo, Rect *dragRect)
{
    if(!drawInfo || !dragRect)
        return paramErr;
    if(isProgress(drawInfo->kind))
        return paramErr; /* no thumb: 8.6 leaves the output alone */
    *dragRect = trackGeometry(*drawInfo).drag;
    return noErr;
}

/* A thumb position: its leading edge's distance from the drag rectangle's. */
OSStatus Executor::C_GetThemeTrackThumbPositionFromOffset(const ThemeTrackDrawInfo *drawInfo, Point thumbOffset,
                                                          GUEST<SInt32> *relativePosition)
{
    if(!drawInfo || !relativePosition)
        return paramErr;
    TrackGeometry g = trackGeometry(*drawInfo);
    *relativePosition = g.lo(g.thumb) - g.lo(g.drag) + (g.horizontal ? thumbOffset.h : thumbOffset.v);
    return noErr;
}

OSStatus Executor::C_GetThemeTrackThumbPositionFromRegion(const ThemeTrackDrawInfo *drawInfo, RgnHandle thumbRgn,
                                                          GUEST<SInt32> *relativePosition)
{
    if(!drawInfo || !thumbRgn || !relativePosition)
        return paramErr;
    TrackGeometry g = trackGeometry(*drawInfo);
    *relativePosition = g.lo((*thumbRgn)->rgnBBox) - g.lo(g.drag);
    return noErr;
}

OSStatus Executor::C_GetThemeTrackLiveValue(const ThemeTrackDrawInfo *drawInfo, SInt32 relativePosition,
                                            GUEST<SInt32> *value)
{
    if(!drawInfo || !value)
        return paramErr;
    if(isProgress(drawInfo->kind))
        return paramErr; /* no thumb: 8.6 leaves the output alone */
    TrackGeometry g = trackGeometry(*drawInfo);
    int32_t min = drawInfo->min, max = drawInfo->max;
    int travel = g.hi(g.drag) - g.lo(g.drag) - g.thumbLength;
    if(travel <= 0 || max <= min)
        *value = min;
    else
        *value = min + (int32_t)std::clamp<int64_t>(((int64_t)relativePosition * ((int64_t)max - min) + travel / 2) / travel,
                                                    0, (int64_t)max - min);
    return noErr;
}

OSStatus Executor::C_DrawThemeTrackTickMarks(const ThemeTrackDrawInfo *drawInfo, ItemCount numTicks,
                                             ThemeEraseUPP eraseProc, UInt32 eraseData)
{
    if(!drawInfo)
        return paramErr;
    TrackGeometry g = trackGeometry(*drawInfo);
    if(numTicks < 2)
        return noErr;
    DrawGuard guard;
    int from = g.lo(g.drag) + g.thumbLength / 2, to = g.hi(g.drag) - g.thumbLength / 2 - 1;
    for(ItemCount i = 0; i < numTicks; i++)
    {
        int at = from + (int64_t)(to - from) * i / (numTicks - 1);
        if(g.horizontal)
            lineGray(at, g.bounds.bottom, at, g.bounds.bottom + 3, kBlack);
        else
            lineGray(g.bounds.right, at, g.bounds.right + 3, at, kBlack);
    }
    return noErr;
}

OSStatus Executor::C_DrawThemeScrollBarArrows(const Rect *bounds, ThemeTrackEnableState enableState,
                                              ThemeTrackPressState pressState, Boolean isHoriz, Rect *trackBounds)
{
    if(!bounds)
        return paramErr;
    TrackGeometry g = scrollBarGeometry(*bounds, isHoriz, true);
    DrawGuard guard;
    drawScrollBarArrows(g, enableState, pressState);
    if(trackBounds)
        *trackBounds = g.track;
    return noErr;
}

Boolean Executor::C_HitTestThemeScrollBarArrows(const Rect *scrollBarBounds, ThemeTrackEnableState enableState,
                                                ThemeTrackPressState pressState, Boolean isHoriz, Point ptHit,
                                                Rect *trackBounds, GUEST<ControlPartCode> *partcode)
{
    if(!scrollBarBounds)
        return false;
    TrackGeometry g = scrollBarGeometry(*scrollBarBounds, isHoriz, true);
    if(trackBounds)
        *trackBounds = g.track;
    ControlPartCode part = kControlNoPart;
    if(g.hasArrows && enableState == kThemeTrackActive)
    {
        if(PtInRect(ptHit, &g.arrowLo))
            part = inUpButton;
        else if(PtInRect(ptHit, &g.arrowHi))
            part = inDownButton;
    }
    if(partcode)
        *partcode = part;
    return part != kControlNoPart;
}

OSStatus Executor::C_GetThemeScrollBarTrackRect(const Rect *bounds, ThemeTrackEnableState enableState,
                                                ThemeTrackPressState pressState, Boolean isHoriz, Rect *trackBounds)
{
    if(!bounds || !trackBounds)
        return paramErr;
    *trackBounds = scrollBarGeometry(*bounds, isHoriz, true).track;
    return noErr;
}

/* ── Buttons ─────────────────────────────────────────────────────────── */

namespace
{
/* What DrawThemeButton sets up for its label proc, by kind and state
   (appr labels.*, 8.6 on the G3): the push-like kinds a title colour on
   their face's, the marks grayishTextOr when inactive; the rest nothing. */
struct LabelLook
{
    bool colors = false, grayish = false;
    uint16_t fg = 0, bg = 0;
};

LabelLook labelLook(ThemeButtonKind kind, ThemeDrawState state)
{
    LabelLook l;
    auto set = [&](uint16_t fg, uint16_t bg) {
        l.colors = true;
        l.fg = fg;
        l.bg = bg;
    };
    switch(kind)
    {
        case kThemePushButton:
        case kThemePopupButton:
        case kThemeArrowButton:
        case kThemeBevelButton:
        case kThemeSmallBevelButton:
        case kThemeLargeBevelButton:
        {
            bool bevel = kind != kThemePushButton && kind != kThemePopupButton && kind != kThemeArrowButton;
            uint16_t face = bevel ? 0xCCCC : 0xDDDD;
            switch(state)
            {
                case kThemeStateActive: set(0, face); break;
                case kThemeStateInactive: set(0x7777, 0xDDDD); break;
                case kThemeStatePressedUp: set(0xFFFF, bevel ? 0x8888 : 0x6666); break;
                case kThemeStatePressedDown:
                case kThemeStateUnavailable:
                    set(bevel || kind == kThemeArrowButton ? 0 : 0x2222, face);
                    break;
            }
            break;
        }
        case kThemeCheckBox:
        case kThemeRadioButton:
        case kThemeDisclosureButton:
        case kThemeIncDecButton:
            l.grayish = state == kThemeStateInactive;
            break;
    }
    return l;
}
}

OSStatus Executor::C_DrawThemeButton(const Rect *inBounds, ThemeButtonKind inKind,
                                     const ThemeButtonDrawInfo *inNewInfo, const ThemeButtonDrawInfo *inPrevInfo,
                                     ThemeEraseUPP inEraseProc, ThemeButtonDrawUPP inLabelProc, UInt32 inUserData)
{
    if(!inBounds || !inNewInfo || !knownButton(inKind))
        return paramErr;
    {
        DrawGuard guard;
        eraseWith(inEraseProc, *inBounds, inUserData);
        drawButton(*inBounds, inKind, *inNewInfo);
    }
    if(inLabelProc)
    {
        Rect content = buttonContent(*inBounds, inKind);
        SInt16 depth;
        Boolean color;
        portDepth(&depth, &color);
        LabelLook look = labelLook(inKind, inNewInfo->state);
        GrafPtr port = qdGlobals().thePort;
        RGBColor fg, bg;
        GetForeColor(&fg);
        GetBackColor(&bg);
        INTEGER mode = PORT_TX_MODE(port);
        if(look.colors)
        {
            RGBColor f = { look.fg, look.fg, look.fg }, b = { look.bg, look.bg, look.bg };
            RGBForeColor(&f);
            RGBBackColor(&b);
        }
        if(look.grayish)
            TextMode(grayishTextOr);
        inLabelProc(&content, inKind, inNewInfo, inUserData, depth, color);
        if(qdGlobals().thePort == port)
        {
            RGBForeColor(&fg);
            RGBBackColor(&bg);
            TextMode(mode);
        }
    }
    return noErr;
}

OSStatus Executor::C_GetThemeButtonRegion(const Rect *inBounds, ThemeButtonKind inKind,
                                          const ThemeButtonDrawInfo *inNewInfo, RgnHandle outRegion)
{
    if(!inBounds || !outRegion || !knownButton(inKind))
        return paramErr;
    Rect r = *inBounds;
    if(inKind == kThemePushButton)
    {
        /* reference: the rounded button, 3 more around a default one */
        if(inNewInfo && (inNewInfo->adornment & kThemeAdornmentDefault))
            InsetRect(&r, -3, -3);
        OpenRgn();
        FrameRoundRect(&r, 8, 8);
        CloseRgn(outRegion);
    }
    else if(inKind == kThemeArrowButton)
    {
        r.right = r.left + 22;
        RectRgn(outRegion, &r);
    }
    else
        RectRgn(outRegion, &r);
    return noErr;
}

OSStatus Executor::C_GetThemeButtonContentBounds(const Rect *inBounds, ThemeButtonKind inKind,
                                                 const ThemeButtonDrawInfo *inDrawInfo, Rect *outBounds)
{
    if(!inBounds || !outBounds || !knownButton(inKind))
        return paramErr;
    *outBounds = buttonContent(*inBounds, inKind);
    return noErr;
}

OSStatus Executor::C_GetThemeButtonBackgroundBounds(const Rect *inBounds, ThemeButtonKind inKind,
                                                    const ThemeButtonDrawInfo *inDrawInfo, Rect *outBounds)
{
    if(!inBounds || !outBounds || !knownButton(inKind))
        return paramErr;
    *outBounds = *inBounds;
    if(inKind == kThemeIncDecButton)
    {
        outBounds->right = outBounds->left + 13;
        outBounds->bottom = outBounds->top + 23;
    }
    return noErr;
}

/* ── Arrows, tabs, chasing arrows ────────────────────────────────────── */

OSStatus Executor::C_DrawThemePopupArrow(const Rect *bounds, ThemeArrowOrientation orientation,
                                         ThemePopupArrowSize size, ThemeDrawState state,
                                         ThemeEraseUPP eraseProc, UInt32 eraseData)
{
    if(!bounds)
        return paramErr;
    /* at the bounds' top left, 3 or 5 across (8.6: kThemeArrow5pt,
       kThemeArrow9pt), not drawn pressed */
    Look look = lookOf(state);
    DrawGuard guard;
    eraseWith(eraseProc, *bounds, eraseData);
    if(look == Look::pressed)
        return noErr;
    int width = size == 1 ? 5 : 3, rows = (width + 1) / 2;
    uint16_t g = grey(look == Look::inactive ? 7 : 0);
    int h = bounds->left, v = bounds->top;
    for(int i = 0; i < rows; i++)
    {
        int a = i, b = width - 1 - i;
        switch(orientation)
        {
            case kThemeArrowUp:
                hrun(h + a, h + b, v + rows - 1 - i, g);
                break;
            case kThemeArrowLeft:
                vrun(h + rows - 1 - i, v + a, v + b, g);
                break;
            case kThemeArrowRight:
                vrun(h + i, v + a, v + b, g);
                break;
            default:
                hrun(h + a, h + b, v + i, g);
                break;
        }
    }
    return noErr;
}

namespace
{
/* A north tab as 8.6 draws it (appr controls.tab80.*, draw*.tab.*): 21
   rows (a front one 24, down over the pane's edge), its sides stepping out
   a pixel every 3 rows from 10 in at the top. Each row: the fill across,
   then the left piece from the left side, then the right piece ending at
   the right side, drawn over the fill from 12 in from the right (a narrow
   tab's pieces overlap: the right one wins). Rows as "LEFT|FILL|RIGHT",
   one hex digit a grey (f white): rows 0-4, then the 3-row steps, the
   last row, a front tab's 3 more (and how far in each of those starts). */
struct TabLook
{
    const char *top[5], *step[3], *last, *below[3];
    int belowInset[3];
};

const TabLook tabLooks[] = {
    /* kThemeTabNonFront */
    { { "20|0|", "20a|c|ba00", "2ac|d|80", "2acd|c|b43", "0cd|c|a0" }, { "2ad|c|b43", "0cd|c|80", "0cd|c|a0" },
      "0ad|c|b40", {} },
    /* kThemeTabNonFrontPressed */
    { { "20|0|", "204|4|4430", "244|5|80", "2445|6|743", "045|6|80" }, { "245|6|743", "045|6|80", "045|6|70" },
      "045|6|740", {} },
    /* kThemeTabNonFrontInactive */
    { { "55|5|", "55|d|55", "5|d|5", "5|d|5", "5|d|5" }, { "5|d|5", "5|d|5", "5|d|5" }, "5|d|5", {} },
    /* kThemeTabFront */
    { { "20|0|", "20b|c|cb00", "2bc|f|d80", "2bff|e|b42", "0cf|e|d80" }, { "2bf|e|b42", "0cf|e|d80", "0cf|e|d80" },
      "2bf|e|b40", { "0cf|e|db0", "cc|e|cc", "|e|" }, { 0, 0, 1 } },
    /* kThemeTabFrontInactive */
    { { "55|5|", "55|e|55", "5|e|5", "5|e|5", "5|e|5" }, { "5|e|5", "5|e|5", "5|e|5" }, "5|e|5", { "|e|", "|e|", "|e|" } },
};

constexpr int tabRows = 21, tabFrontRows = 24;

/* How far in a tab's sides are, row R (of the 21). */
int tabInset(int r)
{
    static const int top[] = { 10, 8, 7, 6, 6 };
    if(r < 5)
        return top[r];
    return 5 - (r - 5) / 3;
}

const char *tabRow(const TabLook &look, int r)
{
    if(r < 5)
        return look.top[r];
    if(r < tabRows - 1)
        return look.step[(r - 5) % 3];
    if(r == tabRows - 1)
        return look.last;
    return look.below[r - tabRows];
}

uint16_t tabGray(char c)
{
    int n = c <= '9' ? c - '0' : c - 'a' + 10;
    return n * 0x1111;
}

/* Tabs facing the other ways, every row as tabLooks' (taken from 8.6 on
   the G3: appr draw32/draw8.tabdir.*), in the north tab's terms: row 0 at
   the tab's far edge, along the tab left to right. South is north turned
   over and east and west north turned sideways, but each shades its own
   edges (the light from the top left), so the rows are their own. */
const char *const tabDirRows[3][5][tabFrontRows] = {
    { /* facing south */
        { /* non-front */
            "2|0|", "20ac|9|800", "2acd|b|840", "2acd|c|b43", "0cd|c|a0", "2ad|c|b43", "0cd|c|80", "0cd|c|a0",
            "2ad|c|b43", "0cd|c|80", "0cd|c|a0", "2ad|c|b43", "0cd|c|80", "0cd|c|a0", "2ad|c|b43", "0cd|c|80",
            "0cd|c|a0", "2ad|c|b43", "0cd|c|80", "0cd|c|a0", "0ad|c|b40",
        },
        { /* non-front pressed */
            "2|0|", "2045|8|30", "2445|7|80", "2445|6|743", "045|6|80", "245|6|743", "045|6|80", "045|6|70",
            "245|6|743", "045|6|80", "045|6|70", "245|6|743", "045|6|80", "045|6|70", "245|6|743", "045|6|80",
            "045|6|70", "245|6|743", "045|6|80", "045|6|70", "045|6|740",
        },
        { /* non-front inactive */
            "|5|", "55|d|55", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5",
            "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5",
        },
        { /* front */
            "2|0|", "20|9|700", "2b|c|b980", "2bcf|e|b42", "0cf|e|d80", "2bf|e|b42", "0cf|e|d80", "0cf|e|d80",
            "2bf|e|b42", "0cf|e|d80", "0cf|e|d80", "2bf|e|b42", "0cf|e|d80", "0cf|e|d80", "2bf|e|b42", "0cf|e|d80",
            "0cf|e|d80", "2bf|e|b42", "0cf|e|d80", "0cf|e|d80", "2bf|e|b40", "0cf|e|db0", "ccf|e|b9", "ff|e|b",
        },
        { /* front inactive */
            "|5|", "55|e|55", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5",
            "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "|e|", "|e|",
            "|e|",
        },
    },
    { /* facing east */
        { /* non-front */
            "2|0|", "20ac|9|800", "2acd|b|840", "2add|c|b42", "0cd|c|80", "2ad|c|b42", "0cd|c|80", "0cd|c|a0",
            "2ad|c|b42", "0cd|c|80", "0cd|c|a0", "2ad|c|b42", "0cd|c|80", "0cd|c|a0", "2ad|c|b42", "0cd|c|80",
            "0cd|c|a0", "2ad|c|b42", "0cd|c|80", "0cd|c|a0", "2ad|c|b62",
        },
        { /* non-front pressed */
            "2|0|", "2044|8|4430", "24455|7|55580", "2445|6|743", "045|6|80", "245|6|743", "045|6|80", "045|6|70",
            "245|6|743", "045|6|80", "045|6|70", "245|6|743", "045|6|80", "045|6|70", "245|6|743", "045|6|80",
            "045|6|70", "245|6|743", "045|6|80", "045|6|70", "045|6|740",
        },
        { /* non-front inactive */
            "|5|", "55|d|55", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5",
            "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5",
        },
        { /* front */
            "2|0|", "20|9|700", "2b|c|b970", "2bcf|e|940", "0cf|e|b80", "2bf|e|940", "0cf|e|b80", "0cf|e|90",
            "2bf|e|b40", "0cf|e|80", "0cf|e|90", "2bf|e|b40", "0cf|e|80", "0cf|e|90", "2bf|e|b40", "0cf|e|80",
            "0cf|e|90", "2bf|e|b40", "0cf|e|80", "0cf|e|90", "2bf|e|b40", "0cf|e|80", "ccf|e|b9", "f|e|b",
        },
        { /* front inactive */
            "|5|", "55|e|55", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5",
            "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5",
            "|e|", "f|e|f",
        },
    },
    { /* facing west */
        { /* non-front */
            "2|0|", "20a|c|a00", "2ac|d|80", "2add|c|b42", "0cd|c|80", "2ad|c|b42", "0cd|c|80", "0cd|c|a0",
            "2ad|c|b42", "0cd|c|80", "0cd|c|a0", "2ad|c|b42", "0cd|c|80", "0cd|c|a0", "2ad|c|b42", "0cd|c|80",
            "0cd|c|a0", "2ad|c|b42", "0cd|c|80", "0cd|c|a0", "2ad|c|b62",
        },
        { /* non-front pressed */
            "2|0|", "20|4|30", "244|5|80", "2445|6|743", "045|6|80", "245|6|743", "045|6|80", "045|6|70",
            "245|6|743", "045|6|80", "045|6|70", "245|6|743", "045|6|80", "045|6|70", "245|6|743", "045|6|80",
            "045|6|70", "245|6|743", "045|6|80", "045|6|70", "045|6|740",
        },
        { /* non-front inactive */
            "|5|", "55|d|55", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5",
            "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5", "5|d|5",
        },
        { /* front */
            "2|0|", "20b|c|b00", "2bc|f|d80", "2bcf|e|b43", "0cf|e|d80", "2bf|e|b43", "0cf|e|d80", "0cf|e|d80",
            "2bf|e|b43", "0cf|e|d80", "0cf|e|d80", "2bf|e|b43", "0cf|e|c80", "0cf|e|d80", "2bf|e|b43", "0cf|e|c80",
            "0cf|e|d80", "2bf|e|b43", "0cf|e|d80", "0cf|e|80", "2bf|e|b40", "0cf|e|80", "ccf|e|cc", "f|e|f",
        },
        { /* front inactive */
            "|5|", "55|e|55", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5",
            "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5", "5|e|5",
            "|e|", "f|e|f",
        },
    },
};

const char *tabDirRow(ThemeTabStyle style, ThemeTabDirection dir, int r)
{
    return dir == kThemeTabNorth ? tabRow(tabLooks[style], r) : tabDirRows[dir - 1][style][r];
}

int tabDirInset(ThemeTabStyle style, ThemeTabDirection dir, int r)
{
    if(r < tabRows)
        return tabInset(r);
    return dir == kThemeTabNorth ? tabLooks[style].belowInset[r - tabRows] : 0;
}

/* Where a tab's north-space points (A along, R across from its far edge)
   are on the screen, for direction DIR in bounds B */
struct TabFrame
{
    Rect b;
    ThemeTabDirection dir;
    int length() const { return dir == kThemeTabNorth || dir == kThemeTabSouth ? b.right - b.left : b.bottom - b.top; }
    /* a pixel */
    Point at(int a, int r) const
    {
        switch(dir)
        {
            case kThemeTabSouth: return Point{ (INTEGER)(b.bottom - 1 - r), (INTEGER)(b.left + a) };
            case kThemeTabEast: return Point{ (INTEGER)(b.top + a), (INTEGER)(b.right - 1 - r) };
            case kThemeTabWest: return Point{ (INTEGER)(b.top + a), (INTEGER)(b.left + r) };
            default: return Point{ (INTEGER)(b.top + r), (INTEGER)(b.left + a) };
        }
    }
    /* a point between pixels */
    void pt(float a, float r, float &x, float &y) const
    {
        switch(dir)
        {
            case kThemeTabSouth: x = b.left + a, y = b.bottom - r; break;
            case kThemeTabEast: x = b.right - r, y = b.top + a; break;
            case kThemeTabWest: x = b.left + r, y = b.top + a; break;
            default: x = b.left + a, y = b.top + r; break;
        }
    }
    /* pixels A1..A2 of row R */
    void run(int a1, int a2, int r, uint16_t g) const
    {
        if(a2 < a1)
            return;
        Point p = at(a1, r), q = at(a2, r);
        lineGray(p.h, p.v, q.h, q.v, g);
    }
};

/* A north tab at the display scale: its outline as a curve into a
   straight slant (the 1x sides' steps), the face inside it, the lit edge
   down the left and across the top, the shade down the right. Colours by
   style (as tabLooks): outline, face, lit, shade (no lit or shade:
   inactive). */
hidpi::Shapes tabShapes(const TabFrame &f, ThemeTabStyle style, int rows)
{
    static const uint8_t looks[][4] = { { 0, 0xc, 0xd, 8 }, { 0, 6, 4, 7 }, { 5, 0xd, 0xff, 0xff },
                                        { 0, 0xe, 0xf, 8 }, { 5, 0xe, 0xff, 0xff } };
    const uint8_t *c = looks[style];
    float W = f.length();
    bool front = rows > tabRows;
    /* the left side D in from the edge, from Y up to the top's middle;
       the right side the same mirrored */
    auto side = [&](float d, float y, bool right) {
        std::vector<float> v;
        auto add = [&](float x, float yy) {
            float px, py;
            f.pt(right ? W - x : x, yy, px, py);
            v.push_back(px);
            v.push_back(py);
        };
        if(y > tabRows)
            add(d, y);
        add(d + 6 - 6 * (std::min<float>(y, tabRows) - 4) / 17, std::min<float>(y, tabRows));
        add(d + 6, 4);
        add(6.6f + 0.7f * d, 2.4f + 0.7f * d);
        add(8 + 0.7f * d, 1 + 0.7f * d);
        add(10, d);
        return v;
    };
    auto path = [&](float d, float y) {
        std::vector<float> v = side(d, y, false), r = side(d, y, true);
        for(size_t i = r.size(); i >= 2; i -= 2)
            v.insert(v.end(), { r[i - 2], r[i - 1] });
        return v;
    };
    hidpi::Shapes s;
    s.push_back({ hidpi::Shape::Polygon, path(0, rows), 0, argbGray(c[1] * 0x1111) });
    if(c[2] != 0xff)
    {
        std::vector<float> lit = side(2.5f, rows, false);
        float ex, ey;
        f.pt(W - 9, 2.5f, ex, ey);
        lit.insert(lit.end(), { ex, ey });
        s.push_back({ hidpi::Shape::Polyline, lit, 1, argbGray(c[2] * 0x1111) });
        std::vector<float> shade = side(1.5f, front ? tabRows + 1 : rows, true);
        s.push_back({ hidpi::Shape::Polyline, shade, 1, argbGray(c[3] * 0x1111) });
    }
    s.push_back({ hidpi::Shape::Polyline, path(0.5f, front ? tabRows + 1 : rows), 1, argbGray(c[0] * 0x1111) });
    return s;
}

void drawTabPixels(const TabFrame &f, ThemeTabStyle style, int rows);

void drawTab(const Rect &bounds, ThemeTabStyle style, ThemeTabDirection dir, int rows)
{
    TabFrame f{ bounds, dir };
    Point p = f.at(0, 0), q = f.at(f.length() - 1, rows - 1);
    const Rect box = { std::min(p.v, q.v), std::min(p.h, q.h), (INTEGER)(std::max(p.v, q.v) + 1),
                       (INTEGER)(std::max(p.h, q.h) + 1) };
    hidpi::chrome_draw(box, hidpi::active() ? tabShapes(f, style, rows) : hidpi::Shapes{},
                       [&] { drawTabPixels(f, style, rows); });
}

void drawTabPixels(const TabFrame &f, ThemeTabStyle style, int rows)
{
    int len = f.length();
    for(int r = 0; r < rows; r++)
    {
        const char *row = tabDirRow(style, f.dir, r);
        const char *bar1 = strchr(row, '|'), *bar2 = strchr(bar1 + 1, '|');
        int e = tabDirInset(style, f.dir, r), left = e, right = len - 1 - e;
        uint16_t fill = tabGray(bar1[1]);
        f.run(left, right, r, fill);
        for(const char *c = row; c < bar1; c++)
            f.run(left + (c - row), left + (c - row), r, tabGray(*c));
        int n = strlen(bar2 + 1);
        if(len - 12 <= right - n)
            f.run(len - 12, right - n, r, fill);
        for(int i = 0; i < n; i++)
            f.run(right - n + 1 + i, right - n + 1 + i, r, tabGray(bar2[1 + i]));
    }
}
}

OSStatus Executor::C_DrawThemeTab(const Rect *inRect, ThemeTabStyle inStyle, ThemeTabDirection inDirection,
                                  ThemeTabTitleDrawUPP labelProc, UInt32 userData)
{
    if(!inRect)
        return paramErr;
    bool front = inStyle == kThemeTabFront || inStyle == kThemeTabFrontInactive || inStyle == kThemeTabFrontUnavailable;
    /* the unavailable styles came after 8.6's Appearance: it draws
       nothing for them (appr controls.tab80.5) */
    if(inStyle < (int)std::size(tabLooks) && inDirection >= kThemeTabNorth && inDirection <= kThemeTabWest)
    {
        DrawGuard guard;
        drawTab(*inRect, inStyle, inDirection, front ? tabFrontRows : tabRows);
    }
    if(labelProc)
    {
        SInt16 depth;
        Boolean color;
        portDepth(&depth, &color);
        labelProc(inRect, inStyle, inDirection, depth, color, userData);
    }
    return noErr;
}

OSStatus Executor::C_DrawThemeTabPane(const Rect *inRect, ThemeDrawState inState)
{
    if(!inRect)
        return paramErr;
    Look look = lookOf(inState);
    if(look == Look::pressed)
        return noErr;
    DrawGuard guard;
    if(look == Look::inactive)
        bevelBox(*inRect, { { 5, 5, 5 } }, 0xe);
    else
        bevelBox(*inRect, { { 0, 0, 0 }, { 0xc, 9, 0xd }, { 0xf, 0xb, 0xe } }, 0xe);
    return noErr;
}

OSStatus Executor::C_GetThemeTabRegion(const Rect *inRect, ThemeTabStyle inStyle, ThemeTabDirection inDirection,
                                       RgnHandle ioRgn)
{
    if(!inRect || !ioRgn)
        return paramErr;
    if(inDirection != kThemeTabNorth)
    {
        RectRgn(ioRgn, inRect);
        return noErr;
    }
    /* the tab's pixels, a row at a time (appr misc.tab.*, the CRCs) */
    bool front = inStyle == kThemeTabFront || inStyle == kThemeTabFrontInactive || inStyle == kThemeTabFrontUnavailable;
    int L = inRect->left, R = inRect->right;
    SetEmptyRgn(ioRgn);
    RgnHandle row = NewRgn();
    for(int r = 0; r < (front ? tabFrontRows : tabRows); r++)
    {
        Rect span;
        int e = r < tabRows ? tabInset(r) : 0;
        SetRect(&span, std::min(L + e, R - 12), inRect->top + r, std::max(R - e, L + 12), inRect->top + r + 1);
        RectRgn(row, &span);
        UnionRgn(ioRgn, row, ioRgn);
    }
    DisposeRgn(row);
    return noErr;
}

OSStatus Executor::C_DrawThemeChasingArrows(const Rect *bounds, UInt32 index, ThemeDrawState state,
                                            ThemeEraseUPP eraseProc, UInt32 eraseData)
{
    if(!bounds)
        return paramErr;
    /* a ring with a gap that goes round with the index */
    DrawGuard guard;
    eraseWith(eraseProc, *bounds, eraseData);
    gray(state == kThemeStateActive ? kBlack : kShadow);
    PenSize(2, 2);
    Rect r = *bounds;
    InsetRect(&r, 1, 1);
    FrameArc(&r, (index % 8) * 45 + 45, 270);
    return noErr;
}

/* ── Backgrounds ─────────────────────────────────────────────────────── */

OSStatus Executor::C_ApplyThemeBackground(ThemeBackgroundKind inKind, const Rect *bounds, ThemeDrawState inState,
                                          SInt16 inDepth, Boolean inColorDev)
{
    /* the face gray for every kind (tab pane, placard, window headers,
       secondary group box) */
    RGBColor c = { kFace, kFace, kFace };
    if(inState != kThemeStateActive && inKind != kThemeBackgroundSecondaryGroupBox)
        c = RGBColor{ kLight, kLight, kLight };
    BackPat(&qdGlobals().white);
    RGBBackColor(&c);
    return noErr;
}

/* ── Headers, placards, frames (Appearance 1.0, $AA74) ───────────────── */

namespace
{
/* A window header or a placard: a bevelled band in a black outline;
   inactive, gray on a lighter face. A placard is drawn pressed too. */
OSStatus band(const Rect *r, ThemeDrawState state, bool canPress)
{
    if(!r)
        return paramErr;
    Look look = lookOf(state);
    if(look == Look::pressed && !canPress)
        return noErr;
    DrawGuard guard;
    if(look == Look::inactive)
        bevelBox(*r, { { 5, 5, 5 } }, 0xe);
    else if(look == Look::pressed)
        bevelBox(*r, { { 0, 0, 0 }, { 5, 0xa, 8 } }, 8);
    else
        bevelBox(*r, { { 0, 0, 0 }, { 0xf, 0xa, 0xd } }, 0xd);
    return noErr;
}

/* A sunken frame just outside r (edit text, list boxes, wells): black
   with a gray above and left; inactive, #555 in a light ring. */
OSStatus sunken(const Rect *r, ThemeDrawState state)
{
    if(!r)
        return paramErr;
    Look look = lookOf(state);
    if(look == Look::pressed)
        return noErr;
    DrawGuard guard;
    Rect f = *r;
    InsetRect(&f, -2, -2);
    if(look == Look::inactive)
    {
        squareRing(f, 0, { 0xd, 0xd, 0xd });
        squareRing(f, 1, { 5, 5, 5 });
    }
    else
    {
        squareRing(f, 0, { 8, 0xf, 0xf });
        squareRing(f, 1, { 0, 0, 0 });
    }
    return noErr;
}
}

OSStatus Executor::C_DrawThemeWindowHeader(const Rect *inRect, ThemeDrawState inState)
{
    return band(inRect, inState, false);
}

OSStatus Executor::C_DrawThemeWindowListViewHeader(const Rect *inRect, ThemeDrawState inState)
{
    return band(inRect, inState, false);
}

OSStatus Executor::C_DrawThemePlacard(const Rect *inRect, ThemeDrawState inState)
{
    return band(inRect, inState, true);
}

OSStatus Executor::C_DrawThemeEditTextFrame(const Rect *inRect, ThemeDrawState inState)
{
    return sunken(inRect, inState);
}

OSStatus Executor::C_DrawThemeListBoxFrame(const Rect *inRect, ThemeDrawState inState)
{
    return sunken(inRect, inState);
}

OSStatus Executor::C_DrawThemeGenericWell(const Rect *inRect, ThemeDrawState inState, Boolean inFillCenter)
{
    if(!inRect)
        return paramErr;
    if(inFillCenter && lookOf(inState) != Look::pressed)
    {
        DrawGuard guard;
        fillGray(*inRect, kWhite);
    }
    return sunken(inRect, inState);
}

/* A two-pixel ring of the accent colour three outside the rectangle, its
   outer corners left out */
OSStatus Executor::C_DrawThemeFocusRect(const Rect *inRect, Boolean inHasFocus)
{
    if(!inRect)
        return paramErr;
    DrawGuard guard;
    RGBColor c = { 0x6666, 0x6666, 0xCCCC };
    if(!inHasFocus)
        GetBackColor(&c); /* off again: the background back under the ring */
    RGBForeColor(&c);
    Rect f = *inRect;
    InsetRect(&f, -3, -3);
    MoveTo(f.left + 1, f.top);
    LineTo(f.right - 2, f.top);
    MoveTo(f.left + 1, f.bottom - 1);
    LineTo(f.right - 2, f.bottom - 1);
    MoveTo(f.left, f.top + 1);
    LineTo(f.left, f.bottom - 2);
    MoveTo(f.right - 1, f.top + 1);
    LineTo(f.right - 1, f.bottom - 2);
    InsetRect(&f, 1, 1);
    FrameRect(&f);
    return noErr;
}

OSStatus Executor::C_DrawThemeFocusRegion(RgnHandle inRegion, Boolean inHasFocus)
{
    if(!inRegion)
        return paramErr;
    Rect r = (*inRegion)->rgnBBox;
    return C_DrawThemeFocusRect(&r, inHasFocus);
}

/* A line down (or across) the middle and a light one beside it, a pixel
   along: #888 and white; inactive, #bbb and #ddd */
OSStatus Executor::C_DrawThemeSeparator(const Rect *inRect, ThemeDrawState inState)
{
    if(!inRect)
        return paramErr;
    Look look = lookOf(inState);
    if(look == Look::pressed)
        return noErr;
    DrawGuard guard;
    const Rect &r = *inRect;
    uint16_t dark = grey(look == Look::inactive ? 0xb : 8), light = grey(look == Look::inactive ? 0xd : 0xf);
    if(r.right - r.left >= r.bottom - r.top)
    {
        int v = (r.top + r.bottom) / 2;
        hrun(r.left + 1, r.right - 1, v + 1, light);
        hrun(r.left, r.right - 2, v, dark);
    }
    else
    {
        int h = (r.left + r.right) / 2;
        vrun(h + 1, r.top + 1, r.bottom - 1, light);
        vrun(h, r.top, r.bottom - 2, dark);
    }
    return noErr;
}

/* A group box: a #888 frame with a white one a pixel down and right
   under it (primary), or only #888 above and left (secondary);
   inactive, #bbb and #ddd */
OSStatus Executor::C_DrawThemePrimaryGroup(const Rect *inRect, ThemeDrawState inState)
{
    if(!inRect)
        return paramErr;
    Look look = lookOf(inState);
    if(look == Look::pressed)
        return noErr;
    DrawGuard guard;
    Rect r = *inRect;
    r.left++;
    r.top++;
    frameGray(r, grey(look == Look::inactive ? 0xd : 0xf));
    OffsetRect(&r, -1, -1);
    frameGray(r, grey(look == Look::inactive ? 0xb : 8));
    return noErr;
}

OSStatus Executor::C_DrawThemeSecondaryGroup(const Rect *inRect, ThemeDrawState inState)
{
    if(!inRect)
        return paramErr;
    Look look = lookOf(inState);
    if(look == Look::pressed)
        return noErr;
    DrawGuard guard;
    if(look == Look::inactive)
        squareRing(*inRect, 0, { 0xb, 0xb, 0xf });
    else
        squareRing(*inRect, 0, { 8, 0xf, 0xf });
    return noErr;
}

/* A slider's tick: 6 long, a #888 shadow below and at its end;
   inactive (and pressed), #777 alone */
OSStatus Executor::C_DrawThemeTickMark(const Rect *bounds, ThemeDrawState state)
{
    if(!bounds)
        return paramErr;
    DrawGuard guard;
    int h = bounds->left + 1, v = bounds->top + 1;
    bool vertical = bounds->bottom - bounds->top > bounds->right - bounds->left;
    if(lookOf(state) != Look::active)
    {
        if(vertical)
            vrun(h, v, v + 5, grey(7));
        else
            hrun(h, h + 5, v, grey(7));
        return noErr;
    }
    if(vertical)
    {
        vrun(h, v, v + 5, grey(0));
        vrun(h + 1, v, v + 6, grey(8));
        pixel(h, v + 6, grey(8));
    }
    else
    {
        hrun(h, h + 5, v, grey(0));
        hrun(h, h + 6, v + 1, grey(8));
        pixel(h + 6, v, grey(8));
    }
    return noErr;
}

/* A standalone grow box, as 8.6 draws it (appr draw32.growdir.*): 16
   square (small, 11) right of ORIGIN, or left of it growing left; down
   from it whichever way it grows. A black outline, a white-lit #aaa-shaded
   bevel, two ridges across the #ccc face; inactive (and pressed), a #555
   outline round a flat face. The no-grow box has no ridges. */
namespace
{
Rect growBoxBounds(Point origin, ThemeGrowDirection dir, bool small)
{
    int size = small ? 11 : 16, left = dir & kThemeGrowLeft ? origin.h - size : origin.h;
    return Rect{ origin.v, (INTEGER)left, (INTEGER)(origin.v + size), (INTEGER)(left + size) };
}

/* the ridges, rows from TOP, columns from 2 (c: the face) */
const char *const growRidges[2][7] = {
    { "cccccffcccc", "ccccfc7accc", "cccfc7affcc", "ccfc7afc7cc", "cca7afc7ccc", "cccafc7cccc", "cccca7ccccc" },
    { "ccffccc", "cfc7acc", "fc7affc", "a7afc7c", "cafc7cc", "cca7ccc" },
};

hidpi::Shapes growBoxShapes(const Rect &b, bool small, bool active, bool ridges)
{
    hidpi::Shapes s;
    float L = b.left, T = b.top, S = b.right - b.left;
    rectShape(s, L, T, L + S, T + S, grey(active ? 0 : 5));
    rectShape(s, L + 1, T + 1, L + S - 1, T + S - 1, grey(0xc));
    if(!active)
        return s;
    rectShape(s, L + 1, T + 1, L + S - 2, T + 2, grey(0xf));
    rectShape(s, L + 1, T + 1, L + 2, T + S - 2, grey(0xf));
    rectShape(s, L + S - 2, T + 2, L + S - 1, T + S - 1, grey(0xa));
    rectShape(s, L + 2, T + S - 2, L + S - 1, T + S - 1, grey(0xa));
    if(!ridges)
        return s;
    /* each ridge a white line and the shade below it, along x + y */
    float x0 = small ? L - 3 : L, y0 = small ? T - 2 : T;
    for(float o : { 0.f, 2.f })
    {
        s.push_back({ hidpi::Shape::Polyline, { x0 + o + 8, y0 + o + 4, x0 + o + 4, y0 + o + 8 }, 1, argbGray(grey(0xf)) });
        s.push_back({ hidpi::Shape::Polyline, { x0 + o + 9.25f, y0 + o + 5.25f, x0 + o + 5.25f, y0 + o + 9.25f }, 1.4f,
                      argbGray(grey(8)) });
    }
    return s;
}

void growBox(Point origin, ThemeGrowDirection dir, bool small, ThemeDrawState state, bool ridges)
{
    DrawGuard guard;
    Rect b = growBoxBounds(origin, dir, small);
    bool active = lookOf(state) == Look::active;
    hidpi::chrome_draw(b, hidpi::active() ? growBoxShapes(b, small, active, ridges) : hidpi::Shapes{}, [&] {
        if(!active)
        {
            bevelBox(b, { { 5, 5, 5 } }, 0xc);
            return;
        }
        bevelBox(b, { { 0, 0, 0 }, { 0xf, 0xa, 0xc } }, 0xc);
        if(!ridges)
            return;
        int top = small ? 2 : 4;
        for(int r = 0; r < (small ? 6 : 7); r++)
            for(const char *c = growRidges[small][r]; *c; c++)
                if(*c != 'c')
                    pixel(b.left + 2 + (c - growRidges[small][r]), b.top + top + r, tabGray(*c));
    });
}
}

OSStatus Executor::C_DrawThemeStandaloneGrowBox(Point origin, ThemeGrowDirection growDirection, Boolean isSmall,
                                                ThemeDrawState state)
{
    growBox(origin, growDirection, isSmall, state, true);
    return noErr;
}

OSStatus Executor::C_DrawThemeStandaloneNoGrowBox(Point origin, ThemeGrowDirection growDirection, Boolean isSmall,
                                                  ThemeDrawState state)
{
    growBox(origin, growDirection, isSmall, state, false);
    return noErr;
}

/* 8.6 takes the direction and the size the other way round here (appr
   misc.growbox.*): any direction gives the small box, and isSmall (1,
   kThemeGrowLeft) puts it left of the origin. */
OSStatus Executor::C_GetThemeStandaloneGrowBoxBounds(Point origin, ThemeGrowDirection growDirection, Boolean isSmall,
                                                     Rect *bounds)
{
    if(!bounds)
        return paramErr;
    *bounds = growBoxBounds(origin, isSmall, growDirection != 0);
    return noErr;
}

OSStatus Executor::C_DrawThemeModelessDialogFrame(const Rect *inRect, ThemeDrawState inState)
{
    if(!inRect)
        return paramErr;
    DrawGuard guard;
    Rect r = *inRect;
    InsetRect(&r, -3, -3);
    frameGray(r, kBlack);
    InsetRect(&r, 1, 1);
    PenSize(2, 2);
    frameGray(r, lookOf(inState) != Look::inactive ? kFace : kLight);
    return noErr;
}

/* ── Menus, measured from 8.6's on a G3 (appr draw32.menu*) ──────────── */

namespace
{
/* What's selected (the default appearance's): a light edge, the fill, a
   dark edge */
struct Rgb
{
    uint16_t r, g, b;
};
constexpr Rgb kSelLight = { 0x6666, 0x6666, 0xCCCC }, kSelFill = { 0x3333, 0x3333, 0x9999 },
              kSelDark = { 0x0000, 0x0000, 0x8888 };

void colorRect(int h1, int v1, int h2, int v2, Rgb c)
{
    if(h2 < h1 || v2 < v1)
        return;
    Rect r = { (INTEGER)v1, (INTEGER)h1, (INTEGER)(v2 + 1), (INTEGER)(h2 + 1) };
    RGBColor rgb = { c.r, c.g, c.b };
    RGBForeColor(&rgb);
    PaintRect(&r);
}

/* The menu bar's top corners, cut away in black: N[k] pixels of row k */
constexpr int kBarCorner[] = { 5, 3, 2, 1, 1 };

void barCorners(int L, int T, int R)
{
    for(int k = 0; k < 5; k++)
    {
        hrun(L, L + kBarCorner[k] - 1, T + k, grey(0));
        hrun(R - kBarCorner[k] + 1, R, T + k, grey(0));
    }
}

/* A pixel and its mirror in the corner's diagonal, (dh, dv) from the corner
   at (h, v); sh: -1 for a right corner */
void cornerPair(int h, int v, int sh, int dh, int dv, int g)
{
    pixel(h + sh * dh, v + dv, grey(g));
    pixel(h + sh * dv, v + dh, grey(g));
}

/* The normal bar: #ddd, a white edge at the top and the left, #999 at the
   right and above the black bottom line, the top corners rounded, shaded
   into the white edge on the left and into the #999 on the right */
void menuBarPixels(int L, int T, int R, int B);

/* A top corner of the bar at the display scale, 8 pixels square: black
   outside an arc of radius 8 (the screen's rounded corner), the bar's
   face inside it, its edge along the arc (left: white; right: shaded
   bands from the top's white down into the side's #999) */
hidpi::Shapes barCornerShapes(int L, int T, int R, bool right)
{
    float x0 = right ? R + 1 - 8 : L, cx = right ? R + 1 - 8 : L + 8, cy = T + 8;
    hidpi::Shapes s;
    rectShape(s, x0, T, x0 + 8, T + 8, grey(0));
    std::vector<float> face{ cx, cy };
    arcPts(face, cx, cy, 8, right ? 270 : 180, right ? 360 : 270);
    s.push_back({ hidpi::Shape::Polygon, face, 0, argbGray(grey(0xd)) });
    if(!right)
    {
        std::vector<float> edge;
        arcPts(edge, cx, cy, 7.5f, 180, 270);
        s.push_back({ hidpi::Shape::Polyline, edge, 1, argbGray(grey(0xf)) });
        return s;
    }
    /* two bands, #888 and #bbb, lighter towards the top */
    for(auto [rad, g] : { std::pair{ 7.5f, 8 }, { 6.5f, 0xb } })
    {
        std::vector<float> edge;
        arcPts(edge, cx, cy, rad, 270, 360);
        hidpi::Shape e{ hidpi::Shape::Polyline, edge, 1, argbGray(grey(g)) };
        e.gx = cx;
        e.gy = T;
        e.gx2 = R + 1;
        e.gy2 = cy;
        e.colors = { argbGray(grey(0xd)), argbGray(grey(g)), argbGray(grey(g)) };
        e.pos = { 0, 0.4f, 1 };
        s.push_back(e);
    }
    return s;
}

void menuBar(int L, int T, int R, int B)
{
    Rect left = { (INTEGER)T, (INTEGER)L, (INTEGER)(T + 8), (INTEGER)(L + 8) },
         right = { (INTEGER)T, (INTEGER)(R + 1 - 8), (INTEGER)(T + 8), (INTEGER)(R + 1) };
    hidpi::chrome_draw(left, hidpi::active() ? barCornerShapes(L, T, R, false) : hidpi::Shapes{}, [&] {
        hidpi::chrome_draw(right, hidpi::active() ? barCornerShapes(L, T, R, true) : hidpi::Shapes{},
                           [&] { menuBarPixels(L, T, R, B); });
    });
}

void menuBarPixels(int L, int T, int R, int B)
{
    Rect in = { (INTEGER)(T + 1), (INTEGER)(L + 1), (INTEGER)(B - 1), (INTEGER)R };
    fillGray(in, grey(0xd));
    hrun(L + 8, R - 8, T, grey(0xf));
    vrun(L, T + 8, B - 3, grey(0xf));
    vrun(R, T + 8, B - 2, grey(9));
    hrun(L + 1, R - 1, B - 2, grey(9));
    pixel(L, B - 2, grey(0xd));
    hrun(L, R, B - 1, grey(0));
    barCorners(L, T, R);
    /* left: the arc's shading, then the white edge along it */
    for(auto [dh, dv, g] : { std::tuple{ 5, 0, 5 }, { 6, 0, 0xa }, { 7, 0, 0xd }, { 3, 1, 5 }, { 4, 1, 0xa },
                             { 2, 2, 5 }, { 3, 2, 0xd } })
        cornerPair(L, T, 1, dh, dv, g);
    for(auto [dh, dv] : { std::pair{ 5, 1 }, { 6, 1 }, { 7, 1 }, { 4, 2 }, { 3, 3 } })
        cornerPair(L, T, 1, dh, dv, 0xf);
    /* right: the arc darkening from the top edge's white into the side's #999 */
    for(auto [dh, dv, g] : { std::tuple{ 4, 0, 2 }, { 5, 0, 5 }, { 6, 0, 0xa }, { 7, 0, 0xd }, { 3, 1, 5 },
                             { 4, 1, 8 }, { 5, 1, 0xb }, { 6, 1, 0xc }, { 2, 2, 5 }, { 3, 2, 8 },
                             { 4, 2, 0xb }, { 1, 3, 5 }, { 2, 3, 8 }, { 3, 3, 0xb }, { 0, 4, 2 },
                             { 1, 4, 8 }, { 2, 4, 0xb }, { 0, 5, 5 }, { 1, 5, 0xa }, { 0, 6, 7 },
                             { 1, 6, 0xb }, { 0, 7, 8 }, { 1, 7, 0xc } })
        pixel(R - dh, T + dv, grey(g));
}

/* The selected bar (and title): the selection's colours, square corners */
void selectedBar(int L, int T, int R, int B)
{
    colorRect(L + 1, T + 1, R - 1, B - 3, kSelFill);
    colorRect(L + 5, T, R - 5, T, kSelLight);
    colorRect(L, T + 5, L, B - 3, kSelLight);
    colorRect(R, T + 5, R, B - 2, kSelDark);
    colorRect(L + 1, B - 2, R - 1, B - 2, kSelDark);
    colorRect(L, B - 2, L, B - 2, kSelFill);
    hrun(L, R, B - 1, grey(0));
    barCorners(L, T, R);
}

void callTitleProc(MenuTitleDrawingUPP proc, const Rect &r, UInt32 data)
{
    if(!proc)
        return;
    SInt16 depth;
    Boolean color;
    portDepth(&depth, &color);
    proc(&r, depth, color, data);
}
}

OSStatus Executor::C_DrawThemeMenuBarBackground(const Rect *inBounds, ThemeMenuBarState inState, UInt32 inFlags)
{
    if(!inBounds)
        return paramErr;
    DrawGuard guard;
    const Rect &b = *inBounds;
    if(inState == kThemeMenuBarSelected)
        selectedBar(b.left, b.top, b.right - 1, b.bottom);
    else
        menuBar(b.left, b.top, b.right - 1, b.bottom);
    return noErr;
}

/* A selected title: the bar's selected look across the title, without its
   corners and side edges; otherwise only the title's own drawing */
OSStatus Executor::C_DrawThemeMenuTitle(const Rect *inMenuBarRect, const Rect *inTitleRect, ThemeMenuState inState,
                                        UInt32 inAttributes, MenuTitleDrawingUPP inTitleProc, UInt32 inTitleData)
{
    if(!inMenuBarRect || !inTitleRect)
        return paramErr;
    {
        DrawGuard guard;
        if(inState == kThemeMenuSelected)
        {
            const Rect &t = *inTitleRect;
            int B = inMenuBarRect->bottom;
            colorRect(t.left, t.top, t.right - 1, t.top, kSelLight);
            colorRect(t.left, t.top + 1, t.right - 1, B - 3, kSelFill);
            colorRect(t.left, B - 2, t.right - 1, B - 2, kSelDark);
        }
    }
    callTitleProc(inTitleProc, *inTitleRect, inTitleData);
    return noErr;
}

/* A menu: #ddd, a white edge at the top and the left, #999 at the right and
   the bottom, a black outline a pixel outside its rectangle and a #222
   shadow below and to the right of that */
OSStatus Executor::C_DrawThemeMenuBackground(const Rect *inMenuRect, ThemeMenuType inMenuType)
{
    if(!inMenuRect || inMenuType > kThemeMenuTypeHierarchical)
        return paramErr;
    DrawGuard guard;
    const Rect &m = *inMenuRect;
    Rect frame = m;
    InsetRect(&frame, -1, -1);
    frameGray(frame, grey(0));
    hrun(m.left + 1, m.right + 1, m.bottom + 1, grey(2));
    vrun(m.right + 1, m.top + 1, m.bottom, grey(2));
    bevelBox(m, { { 0xf, 9, 0xd } }, 0xd);
    return noErr;
}

/* The menu's outline and shadow too */
OSStatus Executor::C_GetThemeMenuBackgroundRegion(const Rect *inMenuRect, ThemeMenuType menuType, RgnHandle region)
{
    if(!inMenuRect || !region || menuType > kThemeMenuTypeHierarchical)
    {
        if(region)
            SetEmptyRgn(region);
        return paramErr;
    }
    const Rect &m = *inMenuRect;
    RgnHandle shadow = NewRgn();
    SetRectRgn(region, m.left - 1, m.top - 1, m.right + 1, m.bottom + 1);
    SetRectRgn(shadow, m.left, m.top, m.right + 2, m.bottom + 2);
    UnionRgn(region, shadow, region);
    DisposeRgn(shadow);
    return noErr;
}

/* An item: #ddd, white at its left and #999 at its right, or the same in
   the selection's colours; a hierarchical
   item's arrow, 6 wide, 12 from its right */
OSStatus Executor::C_DrawThemeMenuItem(const Rect *inMenuRect, const Rect *inItemRect, SInt16 inVirtualMenuTop,
                                       SInt16 inVirtualMenuBottom, ThemeMenuState inState,
                                       ThemeMenuItemType inItemType, MenuItemDrawingUPP inDrawProc,
                                       UInt32 inUserData)
{
    if(!inMenuRect || !inItemRect)
        return paramErr;
    {
        DrawGuard guard;
        const Rect &it = *inItemRect;
        int L = it.left, T = it.top, R = it.right - 1, B = it.bottom - 1;
        if(inState == kThemeMenuSelected)
        {
            colorRect(L, T, L, B, kSelLight);
            colorRect(L + 1, T, R - 1, B, kSelFill);
            colorRect(R, T, R, B, kSelDark);
        }
        else
        {
            Rect f = { (INTEGER)T, (INTEGER)L, (INTEGER)(B + 1), (INTEGER)R };
            fillGray(f, grey(0xd));
            vrun(L, T, B, grey(0xf));
            vrun(R, T, B, grey(9));
            /* the menu's own edges, on its first and last items */
            if(T == inMenuRect->top)
            {
                hrun(L, R - 1, T, grey(0xf));
                pixel(R, T, grey(0xd));
            }
            if(B == inMenuRect->bottom - 1)
            {
                hrun(L + 1, R, B, grey(9));
                pixel(L, B, grey(0xd));
            }
        }
        if((inItemType & ~kThemeMenuItemPopUpBackground) == kThemeMenuItemHierarchical)
        {
            uint16_t g = inState == kThemeMenuSelected ? grey(0xf)
                : inState == kThemeMenuDisabled        ? grey(7)
                                                       : grey(0);
            int h = it.right - 12, mid = (T + B) / 2;
            for(int k = 0; k < 6; k++)
                vrun(h + k, mid - 5 + k, mid + 5 - k, g);
        }
    }
    if(inDrawProc)
    {
        SInt16 depth;
        Boolean color;
        portDepth(&depth, &color);
        inDrawProc(inItemRect, depth, color, inUserData);
    }
    return noErr;
}

/* A #888 line two below the separator item's top, white under it */
OSStatus Executor::C_DrawThemeMenuSeparator(const Rect *inItemRect)
{
    if(!inItemRect)
        return paramErr;
    DrawGuard guard;
    hrun(inItemRect->left, inItemRect->right - 1, inItemRect->top + 2, grey(8));
    hrun(inItemRect->left, inItemRect->right - 1, inItemRect->top + 3, grey(0xf));
    return noErr;
}

/* ── Sounds ──────────────────────────────────────────────────────────── */

/* The theme's sound sets are the Appearance control panel's (none in a
   System without its Sound Sets): silence, as a Mac with "None". */
/* ── Platinum window frames (8.x) ─────────────────────────────────────────
   Measured from 8.6's on a G3 (tests/guest/difftest appr, frames): pixel
   positions relative to the content rectangle (l, t, r, b). */

namespace
{
constexpr uint16_t kPlatFace = 0xCCCC, kPlatShade = 0x9999, kPlatBox = 0x8888, kPlatDark = 0x2222,
                   kPlatStripe = 0x7777, kPlatInactive = 0x5555, kPlatInactiveFace = 0xDDDD;

void pixelGray(int h, int v, uint16_t g)
{
    lineGray(h, v, h, v, g);
}

/* A title-bar box, 13 pixels square at (h, v): an etched ring, a dark
   frame, a bevel and a face whose shade steps from #999 to white along
   the diagonal; PRESSED (8.6 while the button is down in it), the inside
   a darker ramp, #444 to #bbb, its right and bottom edges no lighter
   than #999. GLYPH: 0 none (close), 1 zoom, 2 collapse. */
/* A face of grey steps along the diagonal from (x, y), N pixels square:
   STEPS[k] where the pixel's dx + dy is k, as a linear gradient */
void diagonalShade(hidpi::Shapes &s, float x, float y, int n, std::initializer_list<uint16_t> steps)
{
    hidpi::Shape face{ hidpi::Shape::Polygon, { x, y, x + n, y, x + n, y + n, x, y + n }, 0, argbGray(*steps.begin()) };
    face.gx = x + 0.5f;
    face.gy = y + 0.5f;
    face.gx2 = x + n - 0.5f;
    face.gy2 = y + n - 0.5f;
    int k = 0;
    for(uint16_t g : steps)
    {
        face.colors.push_back(argbGray(g));
        face.pos.push_back((float)k++ / (steps.size() - 1));
    }
    s.push_back(face);
}

void titleBoxPixels(int h, int v, int glyph, bool pressed);

/* A title-bar box at the display scale: its lines as they are, the face's
   steps a smooth shade */
hidpi::Shapes titleBoxShapes(int h, int v, int glyph, bool pressed)
{
    hidpi::Shapes s;
    rectShape(s, h, v, h + 13, v + 13, kPlatFace);
    rectShape(s, h, v, h + 12, v + 1, kPlatBox);
    rectShape(s, h, v, h + 1, v + 12, kPlatBox);
    rectShape(s, h + 12, v + 1, h + 13, v + 13, kWhite);
    rectShape(s, h + 1, v + 12, h + 13, v + 13, kWhite);
    rectShape(s, h + 1, v + 1, h + 12, v + 12, kPlatDark);
    if(pressed)
    {
        /* 0x4444 + 0x1111 * ((i + j) / 2) over the 9 x 9 inside */
        diagonalShade(s, h + 2, v + 2, 9, { 0x4444, 0xCCCC });
        rectShape(s, h + 10, v + 4, h + 11, v + 11, kPlatShade);
        rectShape(s, h + 4, v + 10, h + 11, v + 11, kPlatShade);
    }
    else
    {
        rectShape(s, h + 2, v + 2, h + 11, v + 11, kPlatBox);
        rectShape(s, h + 2, v + 2, h + 11, v + 3, kPlatFace);
        rectShape(s, h + 2, v + 2, h + 3, v + 11, kPlatFace);
        rectShape(s, h + 2, v + 2, h + 3, v + 3, kWhite);
        diagonalShade(s, h + 3, v + 3, 7, { 0x9999, 0xFFFF });
    }
    if(glyph == 1)
    {
        rectShape(s, h + 7, v + 1, h + 8, v + 8, kPlatDark);
        rectShape(s, h + 1, v + 7, h + 8, v + 8, kPlatDark);
    }
    else if(glyph == 2)
    {
        rectShape(s, h + 1, v + 5, h + 12, v + 6, kPlatDark);
        rectShape(s, h + 1, v + 7, h + 12, v + 8, kPlatDark);
    }
    return s;
}

void titleBox(int h, int v, int glyph, bool pressed = false)
{
    Rect box = { (INTEGER)v, (INTEGER)h, (INTEGER)(v + 13), (INTEGER)(h + 13) };
    hidpi::chrome_draw(box, hidpi::active() ? titleBoxShapes(h, v, glyph, pressed) : hidpi::Shapes{},
                       [&] { titleBoxPixels(h, v, glyph, pressed); });
}

void titleBoxPixels(int h, int v, int glyph, bool pressed)
{
    /* the square's other corners are the face's (an alert's bar is tinted
       round them: 8.6) */
    pixelGray(h + 12, v, kPlatFace);
    pixelGray(h, v + 12, kPlatFace);
    lineGray(h, v, h + 11, v, kPlatBox);
    lineGray(h, v, h, v + 11, kPlatBox);
    lineGray(h + 12, v + 1, h + 12, v + 12, kWhite);
    lineGray(h + 1, v + 12, h + 12, v + 12, kWhite);
    Rect frame = { (INTEGER)(v + 1), (INTEGER)(h + 1), (INTEGER)(v + 12), (INTEGER)(h + 12) };
    frameGray(frame, kPlatDark);
    if(pressed)
    {
        for(int j = 0; j < 9; j++)
            for(int i = 0; i < 9; i++)
            {
                uint16_t g = 0x4444 + 0x1111 * ((i + j) / 2);
                if(i == 8 || j == 8)
                    g = std::min<uint16_t>(g, kPlatShade);
                pixelGray(h + 2 + i, v + 2 + j, g);
            }
    }
    else
    {
        pixelGray(h + 2, v + 2, kWhite);
        lineGray(h + 3, v + 2, h + 10, v + 2, kPlatFace);
        lineGray(h + 2, v + 3, h + 2, v + 10, kPlatFace);
        lineGray(h + 3, v + 10, h + 10, v + 10, kPlatBox);
        lineGray(h + 10, v + 3, h + 10, v + 9, kPlatBox);
        for(int dy = 0; dy < 7; dy++)
            for(int dx = 0; dx < 7; dx++)
                pixelGray(h + 3 + dx, v + 3 + dy, (uint16_t)std::min(0xFFFF, 0x9999 + 0x1111 * ((dx + dy) / 2)));
    }
    if(glyph == 1)
    {
        lineGray(h + 7, v + 1, h + 7, v + 7, kPlatDark);
        lineGray(h + 1, v + 7, h + 7, v + 7, kPlatDark);
    }
    else if(glyph == 2)
    {
        lineGray(h + 1, v + 5, h + 11, v + 5, kPlatDark);
        lineGray(h + 1, v + 7, h + 11, v + 7, kPlatDark);
    }
}

/* The title-bar stripes from h1 to h2: white rows, and #777 ones a pixel
   down and right */
/* the red an alert's bar is tinted with (8.6's DrawThemeWindowFrame):
   face, shade, stripes */
const RGBColor kAlertFace = { 0xFFFF, 0xCCCC, 0xCCCC }, kAlertLight = { 0xFFFF, 0x9999, 0x9999 },
               kAlertDark = { 0xFFFF, 0x6666, 0x6666 };

void stripes(int h1, int h2, int top, bool alert = false)
{
    if(h2 < h1)
        return;
    for(int k = 0; k < 6; k++)
        lineGray(h1, top + 2 * k, h2, top + 2 * k, kWhite);
    for(int k = 0; k < 6; k++)
        lineRGB(h1 + 1, top + 1 + 2 * k, h2 + 1, top + 1 + 2 * k,
                alert ? kAlertDark : RGBColor{ kPlatStripe, kPlatStripe, kPlatStripe });
}

/* The grow box in the content's bottom-right corner: a notch of frame
   with three ridges */
void growNotch(int r, int b, bool active = true)
{
    Rect face = { (INTEGER)(b - 14), (INTEGER)(r - 14), (INTEGER)(b + 2), (INTEGER)(r + 2) };
    fillGray(face, active ? kPlatFace : kPlatInactiveFace);
    lineGray(r - 15, b - 15, r, b - 15, active ? kBlack : kPlatInactive);
    lineGray(r - 15, b - 15, r - 15, b, active ? kBlack : kPlatInactive);
    if(!active)
        return;
    lineGray(r - 14, b - 14, r + 1, b - 14, kWhite);
    lineGray(r - 14, b - 14, r - 14, b + 1, kWhite);
    int h0 = r - 14, v0 = b - 14;
    for(int k = 0; k < 3; k++)
    {
        int h = h0 + 2 * k, v = v0 + 2 * k;
        pixelGray(h + 9, v + 3, kWhite);
        lineGray(h + 8, v + 3, h + 3, v + 8, kWhite);
        lineGray(h + 9, v + 4, h + 4, v + 9, kPlatStripe);
        pixelGray(h + 3, v + 9, 0xAAAA);
    }
}
}

void Executor::appearance::documentGrowBox(const Rect &c)
{
    DrawGuard guard;
    growNotch(c.right, c.bottom);
}

/* The modal dialog box: an outline (two pixels on the right and bottom,
   the second inset two from the corners), a bevel (#bbb in, #999 and #555
   out), white between it and the content; inactive, the outline #555 and
   a flat #ddd ring. Thin (WDEF 65): the outline 3 pixels out, the bevel
   against the content. */
namespace
{
/* the dialog frame; a movable modal's (TITLED) has #ddd for the bevel's
   light and #ccc for the inactive ring */
void dialogBox(const Rect &c, bool active, bool thin, bool titled, bool alert = false)
{
    int l = c.left, t = c.top, r = c.right, b = c.bottom;
    int in = thin ? 3 : 6;          /* the outline's distance top/left */
    int out = thin ? 2 : 5;         /* and right/bottom (its first pixel) */
    uint16_t outline = active ? kBlack : kPlatInactive;
    uint16_t light = titled ? kPlatInactiveFace : 0xBBBB;
    /* white between the frame and the content */
    if(!thin)
    {
        Rect bands[] = {
            { (INTEGER)(t - 4), (INTEGER)(l - 4), (INTEGER)t, (INTEGER)(r + 3) },
            { (INTEGER)b, (INTEGER)(l - 4), (INTEGER)(b + 3), (INTEGER)(r + 3) },
            { (INTEGER)t, (INTEGER)(l - 4), (INTEGER)b, (INTEGER)l },
            { (INTEGER)t, (INTEGER)r, (INTEGER)b, (INTEGER)(r + 3) },
        };
        for(const Rect &band : bands)
            fillGray(band, kWhite);
    }
    else
    {
        lineGray(l - 1, t - 1, r - 1, t - 1, kWhite);
        lineGray(l - 1, t - 1, l - 1, b - 1, kWhite);
    }
    lineGray(l - in, t - in, r + out, t - in, outline);
    lineGray(l - in, t - in, l - in, b + out, outline);
    lineGray(r + out, t - in, r + out, b + out, outline);
    lineGray(l - in, b + out, r + out, b + out, outline);
    lineGray(r + out + 1, t - in + 2, r + out + 1, b + out + 1, outline);
    lineGray(l - in + 2, b + out + 1, r + out + 1, b + out + 1, outline);
    if(active)
    {
        /* an alert's light and dark edges are red (8.6's DrawThemeWindowFrame) */
        RGBColor lit = { light, light, light }, dark = { kPlatInactive, kPlatInactive, kPlatInactive };
        if(alert)
        {
            lit = { 0xFFFF, 0x9999, 0x9999 };
            dark = { 0xFFFF, 0x6666, 0x6666 };
        }
        lineRGB(l - in + 1, t - in + 1, r + out - 1, t - in + 1, lit);
        lineRGB(l - in + 1, t - in + 1, l - in + 1, b + out - 1, lit);
        lineGray(r + out - 2, t - in + 3, r + out - 2, b + out - 2, kPlatShade);
        lineGray(l - in + 3, b + out - 2, r + out - 2, b + out - 2, kPlatShade);
        lineRGB(r + out - 2, t - in + 2, r + out - 2, t - in + 2, lit);
        lineRGB(l - in + 2, b + out - 2, l - in + 2, b + out - 2, lit);
        lineRGB(r + out - 1, t - in + 2, r + out - 1, b + out - 1, dark);
        lineRGB(l - in + 2, b + out - 1, r + out - 1, b + out - 1, dark);
    }
    else
    {
        uint16_t ring = titled ? kPlatFace : kPlatInactiveFace;
        Rect rr = { (INTEGER)(t - in + 1), (INTEGER)(l - in + 1), (INTEGER)(b + out), (INTEGER)(r + out) };
        frameGray(rr, ring);
        InsetRect(&rr, 1, 1);
        frameGray(rr, ring);
    }
}

/* a window's title in the system font, centred on the frame from L to R,
   its baseline at V; where it is in *LEFT, *RIGHT */
void frameTitle(ConstStringPtr title, int L, int R, int v, bool active, int *left, int *right)
{
    GUEST<INTEGER> font = PORT_TX_FONT(qdGlobals().thePort), size = PORT_TX_SIZE(qdGlobals().thePort),
                   mode = PORT_TX_MODE(qdGlobals().thePort);
    Style face = PORT_TX_FACE(qdGlobals().thePort);
    TextFont(0);
    TextSize(0);
    TextFace(0);
    TextMode(srcOr);
    int w = StringWidth(title);
    *left = (L + R - w) / 2;
    *right = *left + w;
    gray(active ? kBlack : kPlatStripe);
    MoveTo(*left, v);
    DrawString(title);
    TextFont(font);
    TextSize(size);
    TextFace(face);
    TextMode(mode);
}
}

void Executor::appearance::dialogFrame(const Rect &c, bool active, bool thin, bool alert)
{
    DrawGuard guard;
    dialogBox(c, active, thin, false, alert);
}

/* The movable modal dialog: a title bar 21 pixels tall (an outline, a
   bevel, stripes) whose bottom is the dialog frame's top line. */
namespace
{
/* An active document title bar's boxes and stripes round the title (from
   TITLELEFT to TITLERIGHT, if HASTITLE), for content C: the boxes 4 in
   from the outline, the stripes 5 from them. */
void documentBar(const Rect &c, const WindowParts &parts, bool hasTitle, int titleLeft, int titleRight,
                 bool alert = false)
{
    int l = c.left, t = c.top, r = c.right;
    int boxTop = t - 18;
    int left = l + 2, right = r + 2;
    if(parts.close)
    {
        titleBox(l - 2, boxTop, 0);
        left = l + 15;
    }
    if(parts.collapse)
    {
        titleBox(r - 11, boxTop, 2);
        right = r - 17;
    }
    if(parts.zoom)
    {
        Rect z = Executor::appearance::documentZoomBox(c, parts);
        titleBox(z.left, boxTop, 1);
        right = z.left - 6;
    }
    if(hasTitle)
    {
        stripes(left, titleLeft - 5, boxTop, alert);
        stripes(titleRight + 2, right, boxTop, alert);
    }
    else
        stripes(left, right, boxTop, alert);
}
}

/* As 8.6 draws it (appr draw32.windowframe.popup.*): a black outline 4
   out from the content on the left, 3 on the right, the bottom on the
   content's last row; inside it white lit, #ccc, shaded #999 and #aaa
   down to the content's black edge, #eee beside it on the right; a #444
   shadow on the right alone. The tab: rounded top corners, white, #ccc,
   #999 on its right and bottom. Inactive: the outline and shadow #555,
   everything else #ddd. */
void Executor::appearance::popupFrame(const Rect &c, bool active, int x0, int w, int titleHeight)
{
    DrawGuard guard;
    int L = c.left, T = c.top, R = c.right, B = c.bottom;
    int fl = L - 4, fr = R + 3, tt = T - 10 - titleHeight, bt = T - 6;
    /* 8.6's tab shadow starts a row lower on a titled tab, and stops
       short of the frame's top */
    int shadowTop = tt + (titleHeight > 0 ? 4 : 3);
    char barShadow = titleHeight > 0 ? '0' : '4';
    auto run = [&](int h1, int h2, int v, char g) {
        uint16_t gr = active ? tabGray(g) : grey(g == '0' || g == '4' ? 5 : 0xd);
        hrun(h1, h2, v, gr);
    };
    int xr = x0 + w - 1;
    run(x0 + 2, xr - 2, tt, '0');
    run(x0 + 1, x0 + 1, tt + 1, '0');
    run(x0 + 2, xr - 2, tt + 1, 'f');
    run(xr - 1, xr - 1, tt + 1, '0');
    for(int v = tt + 2; v < bt; v++)
    {
        run(x0, x0, v, '0');
        run(x0 + 1, x0 + (v == tt + 2 ? 2 : 1), v, 'f');
        run(x0 + (v == tt + 2 ? 3 : 2), xr - 2, v, 'c');
        run(xr - 1, xr - 1, v, '9');
        run(xr, xr, v, '0');
        if(v >= shadowTop)
            run(xr + 1, xr + 1, v, '4');
    }
    run(fl, x0, bt, '0');
    run(x0 + 1, x0 + 1, bt, 'c');
    run(x0 + 2, xr - 1, bt, '9');
    run(xr, xr, bt, '0');
    run(xr + 1, xr + 1, bt, barShadow);
    run(xr + 2, fr, bt, '0');
    for(int v = bt + 1; v < B; v++)
    {
        run(fl, fl, v, '0');
        run(fr, fr, v, '0');
        if(v == bt + 1)
        {
            run(fl + 1, fr - 2, v, 'f');
            run(fr - 1, fr - 1, v, 'c');
            continue;
        }
        run(fr + 1, fr + 1, v, '4');
        run(fl + 1, fl + 1, v, 'f');
        run(fr - 1, fr - 1, v, '9');
        if(v < T - 2)
            run(fl + 2, fr - 2, v, 'c');
        else if(v == T - 2)
        {
            run(fl + 2, R, v, 'a');
            run(R + 1, R + 1, v, 'c');
        }
        else
        {
            run(fl + 2, fl + 2, v, 'a');
            run(R + 1, R + 1, v, 'e');
            if(v == T - 1)
                run(L - 1, R, v, '0');
            else
            {
                run(L - 1, L - 1, v, '0');
                run(R, R, v, '0');
            }
        }
    }
    run(fl, fr, B, '0');
    run(fr + 1, fr + 1, B, '4');
}

void Executor::appearance::movableDialogFrame(const Rect &c, bool active, bool thin, ConstStringPtr title,
                                              const WindowParts *parts, bool alert)
{
    DrawGuard guard;
    dialogBox(c, active, thin, true, alert);
    int in = thin ? 3 : 6, out = thin ? 2 : 5;
    int L = c.left - in, R = c.right + out, T = c.top - in - 21;
    uint16_t outline = active ? kBlack : kPlatInactive;
    lineGray(L, T, R, T, outline);
    lineGray(L, T, L, c.top - in, outline);
    lineGray(R, T, R, c.top - in, outline);
    lineGray(R + 1, T + 2, R + 1, c.top - in, outline);
    if(active)
    {
        RGBColor face = alert ? kAlertFace : RGBColor{ kPlatFace, kPlatFace, kPlatFace },
                 shade = alert ? kAlertLight : RGBColor{ kPlatShade, kPlatShade, kPlatShade };
        RGBForeColor(&face);
        Rect fr = { (INTEGER)(T + 2), (INTEGER)(L + 2), (INTEGER)(T + 20), (INTEGER)(R - 1) };
        PaintRect(&fr);
        lineGray(L + 1, T + 1, R - 2, T + 1, kWhite);
        lineGray(L + 1, T + 1, L + 1, T + 19, kWhite);
        lineRGB(L + 2, T + 20, R - 1, T + 20, shade);
        lineRGB(R - 1, T + 2, R - 1, T + 20, shade);
        lineRGB(R - 1, T + 1, R - 1, T + 1, face);
        lineRGB(L + 1, T + 20, L + 1, T + 20, face);
    }
    else
    {
        Rect face = { (INTEGER)(T + 1), (INTEGER)(L + 1), (INTEGER)(T + 21), (INTEGER)R };
        fillGray(face, kPlatInactiveFace);
    }
    int left = 0, right = 0;
    bool hasTitle = title && title[0];
    if(hasTitle)
        frameTitle(title, L, R + 1, T + 15, active, &left, &right);
    if(!active)
        return;
    if(parts)
    {
        /* boxes (DrawThemeWindowFrame's attributes): a document window's
           bar, placed by the outline; its shadow unbroken down the side */
        pixelGray(R + 1, c.top - in + 1, outline);
        Rect bar = { (INTEGER)(T + 22), (INTEGER)(L + 6), (INTEGER)(T + 22), (INTEGER)(R - 5) };
        documentBar(bar, *parts, hasTitle, left, right, alert);
    }
    else if(hasTitle)
    {
        stripes(L + 5, left - 5, T + 4);
        stripes(right + 2, R - 6, T + 4);
    }
    else
        stripes(L + 5, R - 6, T + 4);
}

void Executor::appearance::dialogStructure(const Rect &c, bool thin, RgnHandle rgn, bool titled)
{
    int in = thin ? 3 : 6, out = thin ? 2 : 5;
    int top = c.top - in - (titled ? 21 : 0);
    RgnHandle part = NewRgn();
    SetRectRgn(rgn, c.left - in, top, c.right + out + 1, c.bottom + out + 1);
    SetRectRgn(part, c.right + out + 1, top + 2, c.right + out + 2, c.bottom + out + 2);
    UnionRgn(rgn, part, rgn);
    SetRectRgn(part, c.left - in + 2, c.bottom + out + 1, c.right + out + 2, c.bottom + out + 2);
    UnionRgn(rgn, part, rgn);
    DisposeRgn(part);
}

Rect Executor::appearance::documentStructure(const Rect &c)
{
    return { (INTEGER)(c.top - 22), (INTEGER)(c.left - 6), (INTEGER)(c.bottom + 7), (INTEGER)(c.right + 7) };
}

/* The outline and shadow; collapsed, the outline closes under the title
   bar (t - 1) and the shadow follows it (8.6 on a G3) */
void Executor::appearance::documentStructureRgn(const Rect &c, bool collapsed, RgnHandle rgn)
{
    int bottom = collapsed ? c.top - 6 : (int)c.bottom;
    RgnHandle part = NewRgn();
    SetRectRgn(rgn, c.left - 6, c.top - 22, c.right + 6, bottom + 6);
    SetRectRgn(part, c.right + 6, c.top - 20, c.right + 7, bottom + 7);
    UnionRgn(rgn, part, rgn);
    SetRectRgn(part, c.left - 4, bottom + 6, c.right + 7, bottom + 7);
    UnionRgn(rgn, part, rgn);
    DisposeRgn(part);
}

Rect Executor::appearance::documentCloseBox(const Rect &c)
{
    return { (INTEGER)(c.top - 18), (INTEGER)(c.left - 2), (INTEGER)(c.top - 5), (INTEGER)(c.left + 11) };
}

Rect Executor::appearance::documentCollapseBox(const Rect &c)
{
    return { (INTEGER)(c.top - 18), (INTEGER)(c.right - 11), (INTEGER)(c.top - 5), (INTEGER)(c.right + 2) };
}

Rect Executor::appearance::documentZoomBox(const Rect &c, const WindowParts &parts)
{
    int h = parts.collapse ? c.right - 27 : c.right - 11;
    return { (INTEGER)(c.top - 18), (INTEGER)h, (INTEGER)(c.top - 5), (INTEGER)(h + 13) };
}

void Executor::appearance::documentBox(const Rect &c, const WindowParts &parts, int which, bool pressed)
{
    DrawGuard guard;
    switch(which)
    {
        case 0:
        {
            Rect r = documentCloseBox(c);
            titleBox(r.left, r.top, 0, pressed);
            break;
        }
        case 1:
        {
            Rect r = documentZoomBox(c, parts);
            titleBox(r.left, r.top, 1, pressed);
            break;
        }
        case 2:
        {
            Rect r = documentCollapseBox(c);
            titleBox(r.left, r.top, 2, pressed);
            break;
        }
    }
}


void Executor::appearance::documentFrame(const Rect &c, bool active, const WindowParts &parts,
                                         ConstStringPtr title)
{
    DrawGuard guard;
    int l = c.left, t = c.top, r = c.right, b = c.bottom;
    uint16_t outline = active ? kBlack : kPlatInactive;
    if(parts.collapsed)
    {
        /* the title bar alone: the frame's outline and bevel closing
           under it, the content and its groove gone */
        b = t - 6;
        Rect outer = { (INTEGER)(t - 22), (INTEGER)(l - 6), (INTEGER)(b + 6), (INTEGER)(r + 6) };
        frameGray(outer, outline);
        lineGray(l - 4, b + 6, r + 6, b + 6, outline);
        lineGray(r + 6, t - 20, r + 6, b + 5, outline);
        Rect face = { (INTEGER)(t - 21), (INTEGER)(l - 5), (INTEGER)(b + 5), (INTEGER)(r + 5) };
        fillGray(face, active ? kPlatFace : kPlatInactiveFace);
        if(active)
        {
            lineGray(l - 5, t - 21, r + 3, t - 21, kWhite);
            lineGray(l - 5, t - 21, l - 5, b + 3, kWhite);
            lineGray(l - 4, b + 4, r + 4, b + 4, kPlatShade);
            lineGray(r + 4, t - 20, r + 4, b + 4, kPlatShade);
        }
    }
    else
    {
        /* the outline and its shadow */
        Rect outer = { (INTEGER)(t - 22), (INTEGER)(l - 6), (INTEGER)(b + 6), (INTEGER)(r + 6) };
        frameGray(outer, outline);
        lineGray(l - 4, b + 6, r + 6, b + 6, outline);
        lineGray(r + 6, t - 20, r + 6, b + 5, outline);

        /* the face, round the content (which isn't touched: hiliting draws
           the frame alone) */
        auto fillFace = [&](uint16_t g) {
            Rect bands[] = {
                { (INTEGER)(t - 21), (INTEGER)(l - 5), (INTEGER)(t - 1), (INTEGER)(r + 5) },
                { (INTEGER)(b + 1), (INTEGER)(l - 5), (INTEGER)(b + 5), (INTEGER)(r + 5) },
                { (INTEGER)(t - 1), (INTEGER)(l - 5), (INTEGER)(b + 1), (INTEGER)(l - 1) },
                { (INTEGER)(t - 1), (INTEGER)(r + 1), (INTEGER)(b + 1), (INTEGER)(r + 5) },
            };
            for(const Rect &band : bands)
                fillGray(band, g);
        };
        if(active)
        {
            fillFace(kPlatFace);
            lineGray(l - 5, t - 21, r + 3, t - 21, kWhite);
            lineGray(l - 5, t - 21, l - 5, b + 3, kWhite);
            lineGray(l - 4, b + 4, r + 4, b + 4, kPlatShade);
            lineGray(r + 4, t - 20, r + 4, b + 4, kPlatShade);
            /* the content sunk in a groove */
            lineGray(l - 2, t - 2, r, t - 2, kPlatShade);
            lineGray(l - 2, t - 2, l - 2, b, kPlatShade);
            lineGray(l - 1, b + 1, r + 1, b + 1, kWhite);
            lineGray(r + 1, t - 1, r + 1, b + 1, kWhite);
        }
        else
            fillFace(kPlatInactiveFace);
        Rect border = { (INTEGER)(t - 1), (INTEGER)(l - 1), (INTEGER)(b + 1), (INTEGER)(r + 1) };
        frameGray(border, outline);
        if(parts.grow)
            growNotch(r, b, active);
    }

    /* the title, centred on the frame */
    int titleLeft = 0, titleRight = 0;
    bool hasTitle = title && title[0];
    if(hasTitle)
        frameTitle(title, l - 6, r + 6, t - 7, active, &titleLeft, &titleRight);

    if(!active)
        return;
    documentBar(c, parts, hasTitle, titleLeft, titleRight);
}

/* ── Floating windows (WDEF 66/67), measured from 8.6's 1057, 1059 and
   1073 on a G3: a 16-pixel title bar (on the left for the side variant,
   14 wide there), 10-pixel boxes, dotted stripes, the title in the small
   emphasized system font. Round the content (l, t, r, b) the frame is
   one pixel thinner than a document window's. */
namespace
{
/* the outline's corners (inclusive) */
Rect floatOutline(const Rect &c, bool side, bool collapsed = false, bool thin = false)
{
    /* collapsed, the outline closes under the title bar (beside a side
       one): 8.6 on a G3 */
    return { (INTEGER)(c.top - (side ? 4 : thin ? 14 : 16)), (INTEGER)(c.left - (side ? 14 : 4)),
             (INTEGER)(collapsed && !side ? c.top - 1 : c.bottom + 3),
             (INTEGER)(collapsed && side ? c.left - 1 : c.right + 3) };
}

/* A floating window's box, 10 pixels square at (h, v): titleBox's ring,
   frame and bevel round a 4-pixel face. GLYPH: 0 none (close), 1 zoom,
   2 collapse (its lines across the bar: vertical in a SIDE bar), 4 zoom
   vertically (a line across the middle), 8 horizontally (down it), 12
   both (a corner). PRESSED (8.6, the mouse down in it): the inside dark,
   #444 lightening down to the right, #999 along its right and bottom. */
void smallTitleBoxPixels(int h, int v, int glyph, bool side, bool pressed);

/* A floating window's box at the display scale: its lines as they are,
   the face's steps a smooth shade */
hidpi::Shapes smallTitleBoxShapes(int h, int v, int glyph, bool side, bool pressed)
{
    hidpi::Shapes s;
    rectShape(s, h, v, h + 9, v + 1, kPlatBox);
    rectShape(s, h, v, h + 1, v + 9, kPlatBox);
    rectShape(s, h + 9, v + 1, h + 10, v + 10, kWhite);
    rectShape(s, h + 1, v + 9, h + 10, v + 10, kWhite);
    rectShape(s, h + 1, v + 1, h + 9, v + 9, kPlatDark);
    if(pressed)
    {
        rectShape(s, h + 2, v + 2, h + 8, v + 8, kPlatShade);
        diagonalShade(s, h + 2, v + 2, 5, { 0x4444, 0xBBBB });
    }
    else
    {
        rectShape(s, h + 2, v + 2, h + 8, v + 8, kPlatBox);
        rectShape(s, h + 2, v + 2, h + 8, v + 3, kPlatFace);
        rectShape(s, h + 2, v + 2, h + 3, v + 8, kPlatFace);
        rectShape(s, h + 2, v + 2, h + 3, v + 3, kWhite);
        diagonalShade(s, h + 3, v + 3, 4, { 0x9999, 0x9999, 0xAAAA, 0xCCCC, 0xDDDD, 0xEEEE, 0xFFFF });
    }
    if(glyph == 2 && !side)
    {
        rectShape(s, h + 1, v + 3, h + 9, v + 4, kPlatDark);
        rectShape(s, h + 1, v + 5, h + 9, v + 6, kPlatDark);
    }
    else if(glyph == 2)
    {
        rectShape(s, h + 3, v + 1, h + 4, v + 9, kPlatDark);
        rectShape(s, h + 5, v + 1, h + 6, v + 9, kPlatDark);
    }
    if(glyph == 12)
    {
        rectShape(s, h + 5, v + 1, h + 6, v + 6, kPlatDark);
        rectShape(s, h + 1, v + 5, h + 6, v + 6, kPlatDark);
    }
    else if(glyph & 4)
        rectShape(s, h + 1, v + 4, h + 9, v + 5, kPlatDark);
    else if(glyph & 8)
        rectShape(s, h + 4, v + 1, h + 5, v + 9, kPlatDark);
    return s;
}

/* the box's inside unpressed: a white corner, the face lit and shaded,
   its shade along the diagonal (8.6's steps: no #bbb) */
void smallTitleBoxFace(int h, int v)
{
    pixelGray(h + 2, v + 2, kWhite);
    lineGray(h + 3, v + 2, h + 7, v + 2, kPlatFace);
    lineGray(h + 2, v + 3, h + 2, v + 7, kPlatFace);
    lineGray(h + 3, v + 7, h + 7, v + 7, kPlatBox);
    lineGray(h + 7, v + 3, h + 7, v + 6, kPlatBox);
    static const uint16_t shade[] = { 0x9999, 0x9999, 0xAAAA, 0xCCCC, 0xDDDD, 0xEEEE, 0xFFFF };
    for(int dy = 0; dy < 4; dy++)
        for(int dx = 0; dx < 4; dx++)
            pixelGray(h + 3 + dx, v + 3 + dy, shade[dx + dy]);
}

void smallTitleBox(int h, int v, int glyph, bool side, bool pressed = false)
{
    Rect box = { (INTEGER)v, (INTEGER)h, (INTEGER)(v + 10), (INTEGER)(h + 10) };
    hidpi::chrome_draw(box, hidpi::active() ? smallTitleBoxShapes(h, v, glyph, side, pressed) : hidpi::Shapes{},
                       [&] { smallTitleBoxPixels(h, v, glyph, side, pressed); });
}

void smallTitleBoxPixels(int h, int v, int glyph, bool side, bool pressed)
{
    lineGray(h, v, h + 8, v, kPlatBox);
    lineGray(h, v, h, v + 8, kPlatBox);
    lineGray(h + 9, v + 1, h + 9, v + 9, kWhite);
    lineGray(h + 1, v + 9, h + 9, v + 9, kWhite);
    Rect frame = { (INTEGER)(v + 1), (INTEGER)(h + 1), (INTEGER)(v + 9), (INTEGER)(h + 9) };
    frameGray(frame, kPlatDark);
    if(pressed)
    {
        for(int dy = 0; dy < 6; dy++)
            for(int dx = 0; dx < 6; dx++)
                pixelGray(h + 2 + dx, v + 2 + dy,
                          dx < 5 && dy < 5 ? grey(std::max(4, 3 + dx + dy)) : kPlatShade);
    }
    else
        smallTitleBoxFace(h, v);
    if(glyph == 2 && !side)
    {
        lineGray(h + 1, v + 3, h + 8, v + 3, kPlatDark);
        lineGray(h + 1, v + 5, h + 8, v + 5, kPlatDark);
    }
    else if(glyph == 2)
    {
        lineGray(h + 3, v + 1, h + 3, v + 8, kPlatDark);
        lineGray(h + 5, v + 1, h + 5, v + 8, kPlatDark);
    }
    if(glyph == 12)
    {
        /* both ways: a corner (8.6's utility window) */
        lineGray(h + 5, v + 1, h + 5, v + 5, kPlatDark);
        lineGray(h + 1, v + 5, h + 5, v + 5, kPlatDark);
    }
    else if(glyph & 4)
        lineGray(h + 1, v + 4, h + 8, v + 4, kPlatDark);
    else if(glyph & 8)
        lineGray(h + 4, v + 1, h + 4, v + 8, kPlatDark);
}

/* The dots from A to B along the bar (x across a top bar, y down a side
   one), on a 3-pixel grid from A0: white ones in three rows (columns),
   #777 ones a pixel down and right. */
void floatDots(int a0, int a, int b, int across, bool side)
{
    for(int x = a0 + std::max(0, (a - a0 + 2) / 3 * 3); x <= b; x += 3)
        for(int k = 0; k < 3; k++)
        {
            int y = across + 3 * k;
            if(x >= a)
                side ? pixelGray(y, x, kWhite) : pixelGray(x, y, kWhite);
            if(x + 1 >= a && x + 1 <= b)
                side ? pixelGray(y + 1, x + 1, kPlatStripe) : pixelGray(x + 1, y + 1, kPlatStripe);
        }
}

/* the floating title in the small emphasized system font, centred on L
   to R, its baseline at V */
void floatTitle(ConstStringPtr title, int L, int R, int v, bool active, int *left, int *right)
{
    GUEST<INTEGER> font = PORT_TX_FONT(qdGlobals().thePort), size = PORT_TX_SIZE(qdGlobals().thePort),
                   mode = PORT_TX_MODE(qdGlobals().thePort);
    Style face = PORT_TX_FACE(qdGlobals().thePort);
    C_UseThemeFont(kThemeSmallEmphasizedSystemFont, 0); /* smSystemScript */
    TextMode(srcOr);
    int w = StringWidth(title);
    *left = (L + R - w + 1) / 2; /* the odd pixel on the left */
    *right = *left + w;
    gray(active ? kBlack : kPlatStripe);
    MoveTo(*left, v);
    DrawString(title);
    TextFont(font);
    TextSize(size);
    TextFace(face);
    TextMode(mode);
}
}

Rect Executor::appearance::floatingCloseBox(const Rect &c, const FloatParts &parts)
{
    int h = parts.side ? c.left - 12 : c.left - 2, v = parts.side ? c.top - 2 : c.top - (parts.thin ? 12 : 13);
    return { (INTEGER)v, (INTEGER)h, (INTEGER)(v + 10), (INTEGER)(h + 10) };
}

Rect Executor::appearance::floatingCollapseBox(const Rect &c, const FloatParts &parts)
{
    int h = parts.side ? c.left - 12 : c.right - 8, v = parts.side ? c.bottom - 8 : c.top - (parts.thin ? 12 : 13);
    return { (INTEGER)v, (INTEGER)h, (INTEGER)(v + 10), (INTEGER)(h + 10) };
}

Rect Executor::appearance::floatingZoomBox(const Rect &c, const FloatParts &parts)
{
    /* 11 before the collapse box (8.6's 1061) */
    Rect r = floatingCollapseBox(c, parts);
    if(parts.collapse)
        parts.side ? OffsetRect(&r, 0, -11) : OffsetRect(&r, -11, 0);
    return r;
}

Rect Executor::appearance::floatingGrowBox(const Rect &c)
{
    return { (INTEGER)(c.bottom - 10), (INTEGER)(c.right - 10), c.bottom, c.right };
}

void Executor::appearance::floatingStructure(const Rect &c, const FloatParts &parts, RgnHandle rgn)
{
    Rect o = floatOutline(c, parts.side, parts.collapsed, parts.thin && !parts.side);
    RgnHandle part = NewRgn();
    SetRectRgn(rgn, o.left, o.top, o.right + 1, o.bottom + 1);
    SetRectRgn(part, o.right + 1, o.top + 2, o.right + 2, o.bottom + 2);
    UnionRgn(rgn, part, rgn);
    SetRectRgn(part, o.left + 2, o.bottom + 1, o.right + 2, o.bottom + 2);
    UnionRgn(rgn, part, rgn);
    DisposeRgn(part);
}

void Executor::appearance::floatingBox(const Rect &c, const FloatParts &parts, int which, bool pressed)
{
    DrawGuard guard;
    Rect r = which == 0 ? floatingCloseBox(c, parts) : which == 1 ? floatingZoomBox(c, parts) : floatingCollapseBox(c, parts);
    int glyph = which == 0 ? 0 : which == 2 ? 2 : parts.zoomKind ? parts.zoomKind : 12;
    smallTitleBox(r.left, r.top, glyph, parts.side, pressed);
}

void Executor::appearance::floatingFrame(const Rect &c, bool active, const FloatParts &parts,
                                         ConstStringPtr title)
{
    DrawGuard guard;
    int l = c.left, t = c.top, r = c.right, b = c.bottom;
    Rect o = floatOutline(c, parts.side, parts.collapsed, parts.thin && !parts.side);
    int L = o.left, T = o.top, R = o.right, B = o.bottom;
    uint16_t outline = active ? kBlack : kPlatInactive;

    /* the outline and its shadow */
    Rect outer = { (INTEGER)T, (INTEGER)L, (INTEGER)(B + 1), (INTEGER)(R + 1) };
    frameGray(outer, outline);
    lineGray(R + 1, T + 2, R + 1, B + 1, outline);
    lineGray(L + 2, B + 1, R + 1, B + 1, outline);

    /* the face round the content (collapsed: the bar alone) */
    if(parts.collapsed)
    {
        Rect face = { (INTEGER)(T + 1), (INTEGER)(L + 1), (INTEGER)B, (INTEGER)R };
        fillGray(face, active ? kPlatFace : kPlatInactiveFace);
    }
    else
    {
        Rect bands[] = {
            { (INTEGER)(T + 1), (INTEGER)(L + 1), (INTEGER)(t - 1), (INTEGER)R },
            { (INTEGER)(b + 1), (INTEGER)(L + 1), (INTEGER)B, (INTEGER)R },
            { (INTEGER)(t - 1), (INTEGER)(L + 1), (INTEGER)(b + 1), (INTEGER)(l - 1) },
            { (INTEGER)(t - 1), (INTEGER)(r + 1), (INTEGER)(b + 1), (INTEGER)R },
        };
        for(const Rect &band : bands)
            fillGray(band, active ? kPlatFace : kPlatInactiveFace);
    }
    if(active)
    {
        lineGray(L + 1, T + 1, R - 2, T + 1, kWhite);
        lineGray(L + 1, T + 1, L + 1, B - 2, kWhite);
        lineGray(R - 1, T + 2, R - 1, B - 1, kPlatShade);
        lineGray(L + 2, B - 1, R - 1, B - 1, kPlatShade);
    }
    if(active && !parts.collapsed)
    {
        /* the content sunk in a groove */
        lineGray(l - 2, t - 2, r, t - 2, kPlatShade);
        lineGray(l - 2, t - 2, l - 2, b, kPlatShade);
        lineGray(l - 1, b + 1, r + 1, b + 1, kWhite);
        lineGray(r + 1, t - 1, r + 1, b + 1, kWhite);
    }
    if(!parts.collapsed)
    {
        Rect border = { (INTEGER)(t - 1), (INTEGER)(l - 1), (INTEGER)(b + 1), (INTEGER)(r + 1) };
        frameGray(border, outline);
    }

    if(parts.grow && !parts.collapsed)
    {
        if(active)
        {
            Rect face = { (INTEGER)(b - 8), (INTEGER)(r - 8), (INTEGER)(b + 2), (INTEGER)(r + 2) };
            fillGray(face, kPlatFace);
            lineGray(r - 10, b - 10, r, b - 10, kBlack);
            lineGray(r - 10, b - 10, r - 10, b, kBlack);
            lineGray(r - 9, b - 9, r + 1, b - 9, kWhite);
            lineGray(r - 9, b - 9, r - 9, b + 1, kWhite);
            for(int k = 0; k < 2; k++)
            {
                int h = r - 7 + 2 * k, v = b - 7 + 2 * k;
                pixelGray(h + 3, v, kWhite);
                lineGray(h + 2, v, h, v + 2, kWhite);
                lineGray(h + 3, v + 1, h + 1, v + 3, kPlatStripe);
                pixelGray(h, v + 3, 0xAAAA);
            }
        }
        else
        {
            Rect face = { (INTEGER)(b - 9), (INTEGER)(r - 9), (INTEGER)(b + 2), (INTEGER)(r + 2) };
            fillGray(face, kPlatInactiveFace);
            lineGray(r - 10, b - 10, r, b - 10, kPlatInactive);
            lineGray(r - 10, b - 10, r - 10, b, kPlatInactive);
        }
    }

    /* the title (a side bar has none) */
    int titleLeft = 0, titleRight = 0;
    bool hasTitle = !parts.side && title && title[0];
    if(hasTitle)
        floatTitle(title, L, R + 1, t - (parts.thin ? 3 : 4), active, &titleLeft, &titleRight);
    if(!active)
        return;

    /* the boxes, and the dots from the close box to the collapse (or
       zoom) box, round the title */
    int a0 = parts.side ? t : l;
    int a = a0, end = parts.side ? b - 1 : r - 1;
    if(parts.close)
    {
        Rect box = floatingCloseBox(c, parts);
        smallTitleBox(box.left, box.top, 0, parts.side);
        a0 = a = (parts.side ? box.bottom : box.right) + 2;
    }
    if(parts.collapse)
    {
        Rect box = floatingCollapseBox(c, parts);
        smallTitleBox(box.left, box.top, 2, parts.side);
        end = (parts.side ? box.top : box.left) - 4;
    }
    if(parts.zoom)
    {
        Rect box = floatingZoomBox(c, parts);
        smallTitleBox(box.left, box.top, parts.zoomKind ? parts.zoomKind : 12, parts.side);
        end = parts.side ? box.top - 3 : box.left - 4;
    }
    int across = parts.side ? l - 11 : t - (parts.thin ? 11 : 12);
    if(hasTitle)
    {
        floatDots(a0, a, titleLeft - 4, across, false);
        /* right of it a run of its own, from 2 past the title (8.6) */
        floatDots(titleRight + 2, titleRight + 2, end, across, false);
    }
    else
        floatDots(a0, a, end, across, parts.side);
}

OSStatus Executor::C_PlayThemeSound(ThemeSoundKind kind)
{
    return noErr;
}

OSStatus Executor::C_BeginThemeDragSound(ThemeDragSoundKind kind)
{
    return noErr;
}

OSStatus Executor::C_EndThemeDragSound()
{
    return noErr;
}
