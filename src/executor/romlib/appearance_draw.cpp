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

using namespace Executor;
using namespace Executor::appearance;

namespace
{
/* ── Tracks ──────────────────────────────────────────────────────────── */

constexpr int arrowSize = 16; /* a scroll bar's arrow boxes, its width */
constexpr int sliderThumbLength = 15, sliderInset = 5;

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
    else
    {
        /* sliders and progress bars: inset 2 across (reference TRACK 2), the
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
        g.thumbLength = sliderThumbLength;
        g.hasThumb = info.kind == kThemeSlider;
    }
    g.thumb = g.drag;
    if(g.hasThumb)
    {
        int travel = g.hi(g.drag) - g.lo(g.drag) - g.thumbLength;
        int pos = g.lo(g.drag);
        if(max > min)
            pos += (int64_t)travel * (std::clamp(value, min, max) - (int64_t)min) / ((int64_t)max - min);
        g.setAxis(g.thumb, pos, pos + g.thumbLength);
    }
    else
        SetRect(&g.thumb, 0, 0, 0, 0);
    return g;
}

/* An arrow box, its arrow pointing away from the track. */
void drawArrowBox(const Rect &r, int dir, bool pressed, bool enabled)
{
    boxFace(r, pressed, true);
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

void drawThumb(const TrackGeometry &g, bool pressed)
{
    boxFace(g.thumb, pressed);
    if(g.thumbLength >= 20)
    {
        /* three grip lines across the middle */
        int mid = (g.lo(g.thumb) + g.hi(g.thumb)) / 2;
        for(int d = -2; d <= 2; d += 2)
        {
            if(g.horizontal)
                lineGray(mid + d, g.thumb.top + 4, mid + d, g.thumb.bottom - 5, kDark);
            else
                lineGray(g.thumb.left + 4, mid + d, g.thumb.right - 5, mid + d, kDark);
        }
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

bool knownButton(ThemeButtonKind kind)
{
    return kind <= kThemeListHeaderButton || kind == kThemeSmallCheckBox
           || kind == kThemeSmallRadioButton || kind == kThemeRoundedBevelButton;
}

/* A check box's or radio button's mark box: 12 square, vertically
   centred at the left. */
Rect markBox(const Rect &b, int size = 12)
{
    int top = (b.top + b.bottom - size) / 2;
    Rect r = { (int16_t)top, b.left, (int16_t)(top + size), (int16_t)(b.left + size) };
    return r;
}

void drawButton(const Rect &b, ThemeButtonKind kind, const ThemeButtonDrawInfo &info)
{
    bool active = info.state != kThemeStateInactive && info.state != kThemeStateUnavailable
                  && info.state != kThemeStateUnavailableInactive;
    bool pressed = info.state == kThemeStatePressed;
    uint16_t ink = active ? kBlack : kShadow;
    switch(kind)
    {
        case kThemePushButton:
        {
            Rect r = b;
            gray(pressed ? kShadow : kFace);
            PaintRoundRect(&r, 8, 8);
            gray(ink);
            FrameRoundRect(&r, 8, 8);
            if(info.adornment & kThemeAdornmentDefault)
            {
                InsetRect(&r, -4, -4);
                PenSize(3, 3);
                FrameRoundRect(&r, 16, 16);
                PenSize(1, 1);
            }
            break;
        }
        case kThemeCheckBox:
        case kThemeSmallCheckBox:
        {
            Rect r = markBox(b, kind == kThemeSmallCheckBox ? 10 : 12);
            fillGray(r, pressed ? kShadow : kWhite);
            frameGray(r, ink);
            if(info.value == kThemeButtonOn)
            {
                lineGray(r.left + 2, r.top + 2, r.right - 3, r.bottom - 3, ink);
                lineGray(r.right - 3, r.top + 2, r.left + 2, r.bottom - 3, ink);
            }
            else if(info.value == kThemeButtonMixed)
                lineGray(r.left + 2, (r.top + r.bottom) / 2, r.right - 3, (r.top + r.bottom) / 2, ink);
            break;
        }
        case kThemeRadioButton:
        case kThemeSmallRadioButton:
        {
            Rect r = markBox(b, kind == kThemeSmallRadioButton ? 10 : 12);
            gray(pressed ? kShadow : kWhite);
            PaintOval(&r);
            gray(ink);
            FrameOval(&r);
            if(info.value == kThemeButtonOn)
            {
                InsetRect(&r, 3, 3);
                PaintOval(&r);
            }
            else if(info.value == kThemeButtonMixed)
                lineGray(r.left + 3, (r.top + r.bottom) / 2, r.right - 4, (r.top + r.bottom) / 2, ink);
            break;
        }
        case kThemeBevelButton:
        case kThemeSmallBevelButton:
        case kThemeLargeBevelButton:
        case kThemeRoundedBevelButton:
            boxFace(b, pressed || info.value == kThemeButtonOn, active);
            break;
        case kThemeListHeaderButton:
        {
            /* a list view's column title: the sorted one darker */
            Rect r = b;
            fillGray(r, info.value == kThemeButtonOn || pressed ? kTrack : kFace);
            bevel(r, kWhite, kShadow);
            lineGray(r.left, r.bottom - 1, r.right - 1, r.bottom - 1, kDark);
            lineGray(r.right - 1, r.top, r.right - 1, r.bottom - 1, kDark);
            break;
        }
        case kThemePopupButton:
        {
            Rect r = b;
            fillGray(r, pressed ? kShadow : kFace);
            frameGray(r, ink);
            Rect a = r;
            a.left = r.right - 22;
            lineGray(a.left, r.top + 1, a.left, r.bottom - 2, kShadow);
            arrow(a, kThemeArrowDown, 7, ink);
            break;
        }
        case kThemeArrowButton:
            arrow(b, kThemeArrowDown, 7, ink);
            break;
        case kThemeDisclosureButton:
        {
            Rect r = markBox(b, 12);
            int dir = info.value == kThemeDisclosureDown ? kThemeArrowDown
                                                        : (info.value == kThemeDisclosureLeft ? kThemeArrowLeft : kThemeArrowRight);
            arrow(r, dir, 9, pressed ? kShadow : ink);
            break;
        }
        case kThemeIncDecButton:
        {
            /* two halves, up and down, 13 x 23 (reference background) */
            Rect r = { b.top, b.left, (int16_t)(b.top + 23), (int16_t)(b.left + 13) };
            Rect up = r, down = r;
            up.bottom = down.top = r.top + 12;
            up.bottom++;
            boxFace(up, pressed && info.value == kThemeButtonOn, active);
            boxFace(down, pressed && info.value != kThemeButtonOn, active);
            arrow(up, kThemeArrowUp, 5, ink);
            arrow(down, kThemeArrowDown, 5, ink);
            break;
        }
    }
    if(active && (info.adornment & kThemeAdornmentFocus))
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
            Rect t = g.track;
            frameGray(t, active || drawInfo->enableState == kThemeTrackNothingToScroll ? kBlack : kShadow);
            InsetRect(&t, 1, 1);
            fillGray(t, g.hasThumb ? kTrack : kLight);
            if(g.hasThumb)
                drawThumb(g, press & kThemeThumbPressed);
            break;
        }
        case kThemeSlider:
        {
            /* a channel 4 across the middle, the thumb over it */
            Rect ch = g.bounds;
            int mid = g.horizontal ? (ch.top + ch.bottom) / 2 : (ch.left + ch.right) / 2;
            if(g.horizontal)
                ch.top = mid - 2, ch.bottom = mid + 2;
            else
                ch.left = mid - 2, ch.right = mid + 2;
            fillGray(ch, active ? kTrack : kLight);
            frameGray(ch, active ? kDark : kShadow);
            if(g.hasThumb)
                drawThumb(g, drawInfo->trackInfo.slider.pressState & kThemeThumbPressed);
            break;
        }
        default:
        {
            /* progress bars: the done part in the accent colour */
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
    TrackGeometry g = trackGeometry(*drawInfo);
    if(g.hasThumb)
        RectRgn(thumbRgn, &g.thumb);
    else
        SetEmptyRgn(thumbRgn);
    return noErr;
}

OSStatus Executor::C_GetThemeTrackDragRect(const ThemeTrackDrawInfo *drawInfo, Rect *dragRect)
{
    if(!drawInfo || !dragRect)
        return paramErr;
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
        inLabelProc(&content, inKind, inNewInfo, inUserData, depth, color);
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
    static const int widths[] = { 7, 5, 9 }; /* kThemeArrow7pt, 5pt, 9pt */
    DrawGuard guard;
    eraseWith(eraseProc, *bounds, eraseData);
    arrow(*bounds, orientation, size < 3 ? widths[size] : 7,
          state == kThemeStateActive || state == kThemeStatePressed ? kBlack : kShadow);
    return noErr;
}

OSStatus Executor::C_DrawThemeTab(const Rect *inRect, ThemeTabStyle inStyle, ThemeTabDirection inDirection,
                                  ThemeTabTitleDrawUPP labelProc, UInt32 userData)
{
    if(!inRect)
        return paramErr;
    bool front = inStyle == kThemeTabFront || inStyle == kThemeTabFrontInactive || inStyle == kThemeTabFrontUnavailable;
    bool active = inStyle == kThemeTabFront || inStyle == kThemeTabNonFront || inStyle == kThemeTabNonFrontPressed;
    {
        DrawGuard guard;
        Rect r = *inRect;
        fillGray(r, front ? kFace : (inStyle == kThemeTabNonFrontPressed ? kShadow : kTrack));
        uint16_t ink = active ? kBlack : kShadow;
        lineGray(r.left, r.bottom - 1, r.left, r.top + 2, ink);
        LineTo(r.left + 2, r.top);
        LineTo(r.right - 3, r.top);
        LineTo(r.right - 1, r.top + 2);
        LineTo(r.right - 1, r.bottom - 1);
        if(!front)
            lineGray(r.left, r.bottom - 1, r.right - 1, r.bottom - 1, ink);
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
    DrawGuard guard;
    Rect r = *inRect;
    fillGray(r, kFace);
    frameGray(r, inState == kThemeStateActive ? kBlack : kShadow);
    InsetRect(&r, 1, 1);
    bevel(r, kWhite, kShadow);
    return noErr;
}

OSStatus Executor::C_GetThemeTabRegion(const Rect *inRect, ThemeTabStyle inStyle, ThemeTabDirection inDirection,
                                       RgnHandle ioRgn)
{
    if(!inRect || !ioRgn)
        return paramErr;
    RectRgn(ioRgn, inRect);
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
bool isActive(ThemeDrawState s)
{
    return s == kThemeStateActive || s == kThemeStatePressed;
}

/* A gray band with a bevel and a dark line along its bottom: a window
   header, a list view's header, a placard. */
OSStatus band(const Rect *r, ThemeDrawState state, bool pressed)
{
    if(!r)
        return paramErr;
    DrawGuard guard;
    Rect b = *r;
    fillGray(b, pressed ? kShadow : (isActive(state) ? kFace : kLight));
    if(isActive(state) && !pressed)
        bevel(b, kWhite, kShadow);
    lineGray(b.left, b.bottom - 1, b.right - 1, b.bottom - 1, isActive(state) ? kDark : kShadow);
    return noErr;
}

/* A sunken frame just outside r: dark above and left, white below and
   right (edit text, list boxes, wells). */
OSStatus sunken(const Rect *r, ThemeDrawState state)
{
    if(!r)
        return paramErr;
    DrawGuard guard;
    Rect f = *r;
    InsetRect(&f, -1, -1);
    frameGray(f, isActive(state) ? kBlack : kShadow);
    InsetRect(&f, -1, -1);
    lineGray(f.left, f.bottom - 1, f.left, f.top, kShadow);
    LineTo(f.right - 1, f.top);
    lineGray(f.right - 1, f.top + 1, f.right - 1, f.bottom - 1, kWhite);
    LineTo(f.left + 1, f.bottom - 1);
    return noErr;
}

/* An etched line pair (separators, group boxes). */
void etched(int h1, int v1, int h2, int v2, bool active)
{
    lineGray(h1, v1, h2, v2, active ? kShadow : kLight);
    lineGray(h1 + (v1 == v2 ? 0 : 1), v1 + (v1 == v2 ? 1 : 0), h2 + (v1 == v2 ? 0 : 1), v2 + (v1 == v2 ? 1 : 0), kWhite);
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
    if(!inRect)
        return paramErr;
    band(inRect, inState, inState == kThemeStatePressed);
    DrawGuard guard;
    frameGray(*inRect, isActive(inState) ? kBlack : kShadow);
    return noErr;
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
    if(inFillCenter)
    {
        DrawGuard guard;
        fillGray(*inRect, kWhite);
    }
    return sunken(inRect, inState);
}

OSStatus Executor::C_DrawThemeFocusRect(const Rect *inRect, Boolean inHasFocus)
{
    if(!inRect)
        return paramErr;
    DrawGuard guard;
    Rect f = *inRect;
    InsetRect(&f, -3, -3);
    PenSize(2, 2);
    if(inHasFocus)
    {
        RGBColor accent = { 0x6666, 0x6666, 0xCCCC };
        RGBForeColor(&accent);
        FrameRect(&f);
    }
    else
    {
        /* off again: the background back under the ring */
        PenMode(patCopy);
        PenPat(&qdGlobals().white);
        RGBColor back;
        GetBackColor(&back);
        RGBForeColor(&back);
        FrameRect(&f);
    }
    return noErr;
}

OSStatus Executor::C_DrawThemeFocusRegion(RgnHandle inRegion, Boolean inHasFocus)
{
    if(!inRegion)
        return paramErr;
    Rect r = (*inRegion)->rgnBBox;
    return C_DrawThemeFocusRect(&r, inHasFocus);
}

OSStatus Executor::C_DrawThemeSeparator(const Rect *inRect, ThemeDrawState inState)
{
    if(!inRect)
        return paramErr;
    DrawGuard guard;
    const Rect &r = *inRect;
    if(r.right - r.left >= r.bottom - r.top)
        etched(r.left, r.top, r.right - 1, r.top, isActive(inState));
    else
        etched(r.left, r.top, r.left, r.bottom - 1, isActive(inState));
    return noErr;
}

OSStatus Executor::C_DrawThemePrimaryGroup(const Rect *inRect, ThemeDrawState inState)
{
    if(!inRect)
        return paramErr;
    DrawGuard guard;
    Rect r = *inRect;
    r.right--;
    r.bottom--;
    frameGray(r, isActive(inState) ? kShadow : kLight);
    OffsetRect(&r, 1, 1);
    gray(kWhite);
    MoveTo(r.left, r.bottom - 1);
    LineTo(r.left, r.top);
    LineTo(r.right - 1, r.top);
    MoveTo(r.right - 1, r.top);
    LineTo(r.right - 1, r.bottom - 1);
    LineTo(r.left, r.bottom - 1);
    return noErr;
}

OSStatus Executor::C_DrawThemeSecondaryGroup(const Rect *inRect, ThemeDrawState inState)
{
    return C_DrawThemePrimaryGroup(inRect, inState);
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
    frameGray(r, isActive(inState) ? kFace : kLight);
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
   the diagonal. GLYPH: 0 none (close), 1 zoom, 2 collapse. */
void titleBox(int h, int v, int glyph)
{
    lineGray(h, v, h + 11, v, kPlatBox);
    lineGray(h, v, h, v + 11, kPlatBox);
    lineGray(h + 12, v + 1, h + 12, v + 12, kWhite);
    lineGray(h + 1, v + 12, h + 12, v + 12, kWhite);
    Rect frame = { (INTEGER)(v + 1), (INTEGER)(h + 1), (INTEGER)(v + 12), (INTEGER)(h + 12) };
    frameGray(frame, kPlatDark);
    pixelGray(h + 2, v + 2, kWhite);
    lineGray(h + 3, v + 2, h + 10, v + 2, kPlatFace);
    lineGray(h + 2, v + 3, h + 2, v + 10, kPlatFace);
    lineGray(h + 3, v + 10, h + 10, v + 10, kPlatBox);
    lineGray(h + 10, v + 3, h + 10, v + 9, kPlatBox);
    for(int dy = 0; dy < 7; dy++)
        for(int dx = 0; dx < 7; dx++)
            pixelGray(h + 3 + dx, v + 3 + dy, (uint16_t)std::min(0xFFFF, 0x9999 + 0x1111 * ((dx + dy) / 2)));
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
void stripes(int h1, int h2, int top)
{
    if(h2 < h1)
        return;
    for(int k = 0; k < 6; k++)
        lineGray(h1, top + 2 * k, h2, top + 2 * k, kWhite);
    for(int k = 0; k < 6; k++)
        lineGray(h1 + 1, top + 1 + 2 * k, h2 + 1, top + 1 + 2 * k, kPlatStripe);
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
void dialogBox(const Rect &c, bool active, bool thin, bool titled)
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
        lineGray(l - in + 1, t - in + 1, r + out - 1, t - in + 1, light);
        lineGray(l - in + 1, t - in + 1, l - in + 1, b + out - 1, light);
        lineGray(r + out - 2, t - in + 3, r + out - 2, b + out - 2, kPlatShade);
        lineGray(l - in + 3, b + out - 2, r + out - 2, b + out - 2, kPlatShade);
        pixelGray(r + out - 2, t - in + 2, light);
        pixelGray(l - in + 2, b + out - 2, light);
        lineGray(r + out - 1, t - in + 2, r + out - 1, b + out - 1, kPlatInactive);
        lineGray(l - in + 2, b + out - 1, r + out - 1, b + out - 1, kPlatInactive);
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

void Executor::appearance::dialogFrame(const Rect &c, bool active, bool thin)
{
    DrawGuard guard;
    dialogBox(c, active, thin, false);
}

/* The movable modal dialog: a title bar 21 pixels tall (an outline, a
   bevel, stripes) whose bottom is the dialog frame's top line. */
void Executor::appearance::movableDialogFrame(const Rect &c, bool active, bool thin, ConstStringPtr title)
{
    DrawGuard guard;
    dialogBox(c, active, thin, true);
    int in = thin ? 3 : 6, out = thin ? 2 : 5;
    int L = c.left - in, R = c.right + out, T = c.top - in - 21;
    uint16_t outline = active ? kBlack : kPlatInactive;
    lineGray(L, T, R, T, outline);
    lineGray(L, T, L, c.top - in, outline);
    lineGray(R, T, R, c.top - in, outline);
    lineGray(R + 1, T + 2, R + 1, c.top - in, outline);
    if(active)
    {
        Rect face = { (INTEGER)(T + 2), (INTEGER)(L + 2), (INTEGER)(T + 20), (INTEGER)(R - 1) };
        fillGray(face, kPlatFace);
        lineGray(L + 1, T + 1, R - 2, T + 1, kWhite);
        lineGray(L + 1, T + 1, L + 1, T + 19, kWhite);
        lineGray(L + 2, T + 20, R - 1, T + 20, kPlatShade);
        lineGray(R - 1, T + 2, R - 1, T + 20, kPlatShade);
        pixelGray(R - 1, T + 1, kPlatFace);
        pixelGray(L + 1, T + 20, kPlatFace);
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
    if(hasTitle)
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

void Executor::appearance::documentFrame(const Rect &c, bool active, const WindowParts &parts,
                                         ConstStringPtr title)
{
    DrawGuard guard;
    int l = c.left, t = c.top, r = c.right, b = c.bottom;
    uint16_t outline = active ? kBlack : kPlatInactive;

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

    /* the title, centred on the frame */
    int titleLeft = 0, titleRight = 0;
    bool hasTitle = title && title[0];
    if(hasTitle)
        frameTitle(title, l - 6, r + 6, t - 7, active, &titleLeft, &titleRight);

    if(!active)
        return;
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
        Rect z = documentZoomBox(c, parts);
        titleBox(z.left, boxTop, 1);
        right = z.left - 6;
    }
    if(hasTitle)
    {
        stripes(left, titleLeft - 5, boxTop);
        stripes(titleRight + 2, right, boxTop);
    }
    else
        stripes(left, right, boxTop);
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
