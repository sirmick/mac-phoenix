/* MacPhoenix: the Appearance Manager's drawing helpers (appearance_mgr.cpp,
 * appearance_draw.cpp): QuickDraw in the theme's grays, the pen and colours
 * put back afterwards. Executor's look so far: System 7's black outlines
 * and plain boxes, in the platinum shapes and grays (Appearance.md). */
#pragma once

#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <Appearance.h>
#include <quickdraw/cquick.h>

namespace Executor
{
namespace appearance
{
/* The grays, as the platinum brushes have them */
constexpr uint16_t kWhite = 0xFFFF, kLight = 0xEEEE, kFace = 0xDDDD, kTrack = 0xBBBB,
                   kShadow = 0x8888, kDark = 0x5555, kBlack = 0x0000;

/* Saves the port's pen and colours, puts them back. */
class DrawGuard
{
    PenState pen;
    RGBColor fore, back;

public:
    DrawGuard()
    {
        GetPenState(&pen);
        GetForeColor(&fore);
        GetBackColor(&back);
        PenNormal();
    }
    ~DrawGuard()
    {
        SetPenState(&pen);
        RGBForeColor(&fore);
        RGBBackColor(&back);
    }
};

inline void gray(uint16_t g)
{
    RGBColor c = { g, g, g };
    RGBForeColor(&c);
}

inline void fillGray(const Rect &r, uint16_t g)
{
    gray(g);
    PaintRect(&r);
}

inline void frameGray(const Rect &r, uint16_t g)
{
    gray(g);
    FrameRect(&r);
}

inline void lineGray(int h1, int v1, int h2, int v2, uint16_t g)
{
    gray(g);
    MoveTo(h1, v1);
    LineTo(h2, v2);
}

/* Light along the top and left inside r, shade along the bottom and right. */
inline void bevel(const Rect &r, uint16_t lit, uint16_t shade)
{
    lineGray(r.left, r.bottom - 2, r.left, r.top, lit);
    LineTo(r.right - 2, r.top);
    lineGray(r.right - 1, r.top + 1, r.right - 1, r.bottom - 1, shade);
    LineTo(r.left + 1, r.bottom - 1);
}

/* A box face: outlined, filled, bevelled; pressed, darker and flat. */
inline void boxFace(const Rect &r, bool pressed, bool active = true)
{
    frameGray(r, active ? kBlack : kShadow);
    Rect in = r;
    InsetRect(&in, 1, 1);
    fillGray(in, pressed ? kShadow : kFace);
    if(!pressed && in.right - in.left > 2 && in.bottom - in.top > 2)
        bevel(in, kWhite, kShadow);
}

/* A solid triangle pointing `dir` (kThemeArrowLeft, Down, Right, Up),
   `size` across, centred in r. */
inline void arrow(const Rect &r, int dir, int size, uint16_t g)
{
    gray(g);
    int depth = (size + 1) / 2;
    int ch = (r.left + r.right) / 2, cv = (r.top + r.bottom) / 2;
    /* row i from the tip, 2i + 1 long */
    for(int i = 0; i < depth; i++)
    {
        int at;
        switch(dir)
        {
            case kThemeArrowUp:
            case kThemeArrowDown:
                at = cv - depth / 2 + (dir == kThemeArrowUp ? i : depth - 1 - i);
                MoveTo(ch - i, at);
                LineTo(ch + i, at);
                break;
            case kThemeArrowLeft:
            case kThemeArrowRight:
                at = ch - depth / 2 + (dir == kThemeArrowLeft ? i : depth - 1 - i);
                MoveTo(at, cv - i);
                LineTo(at, cv + i);
                break;
        }
    }
}

/* ── Platinum window frames (8.x), round a content rectangle in global
   coordinates, in the current port (the Window Manager's) ── */

struct WindowParts
{
    bool close, zoom, collapse;
    bool grow;      /* the grow box in the content's corner (WDEF 64's) */
};

/* The rectangle a platinum document window's frame occupies (structure,
   its shadow included) round content c. */
Rect documentStructure(const Rect &c);

/* A platinum document window's frame and title (TITLE may be nullptr). */
void documentFrame(const Rect &c, bool active, const WindowParts &parts, ConstStringPtr title);

/* A platinum modal dialog box's frame (dBoxProc; WDEF 65's modal and
   alert variants are THIN: 3 pixels against 6). */
void dialogFrame(const Rect &c, bool active, bool thin);
/* A movable modal dialog's: a title bar over the same frame. */
void movableDialogFrame(const Rect &c, bool active, bool thin, ConstStringPtr title);
/* their structure regions */
void dialogStructure(const Rect &c, bool thin, RgnHandle rgn, bool titled = false);

/* The grow box alone (DrawGrowIcon on a document window). */
void documentGrowBox(const Rect &c);

/* Where its boxes are (global), for hit testing. */
Rect documentCloseBox(const Rect &c);
Rect documentZoomBox(const Rect &c, const WindowParts &parts);
Rect documentCollapseBox(const Rect &c);

/* The current port's depth and whether its device is in colour, what the
   definitions' callbacks are told. */
inline void portDepth(SInt16 *depth, Boolean *color)
{
    GDHandle gd = LM(TheGDevice);
    *depth = gd ? (SInt16)(*GD_PMAP(gd))->pixelSize : 1;
    *color = gd && *depth > 2;
}
}
}
