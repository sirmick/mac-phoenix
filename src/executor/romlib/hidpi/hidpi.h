#pragma once
/* MacPhoenix: high-resolution rendering (docs/executor/HiDPI.md), phase 1:
   text on the screen.

   The 1x framebuffer stays what QuickDraw draws and programs read. Beside
   it, a presentation surface at the display's scale: the 1x pixels scaled
   up, with the text QuickDraw drew on the screen redrawn over them from
   the font's outline (Skia). Each piece of text drawn is recorded with the
   1x pixels it left; it is shown while the screen still has those pixels
   there, else the 1x pixels are.

   Built with -DEXECUTOR_SKIA=ON (EXECUTOR_HIDPI=1); without it every call
   here is an inline no-op. */

#include <ExMacTypes.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <vdriver/dirtyrect.h>
#include <hidpi/hidpi_internal.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace Executor
{
struct Rect;
}

namespace Executor::hidpi
{
/* A character of a text run: its code and the pen's position before it
   (port coordinates, 16.16). */
struct Glyph
{
    uint8_t c;
    Fixed x;
};

/* What StdText is drawing: the font the Font Manager chose and how. */
struct TextParams
{
    INTEGER family;     /* the family whose strike draws it (WidthTable fID) */
    INTEGER size;       /* points */
    int face;           /* txFace asked for */
    int bold;           /* bold's smear in pixels (FMOutput) */
    int ulThick;        /* underline thickness (FMOutput) */
    int shadow;         /* shadow width (FMOutput) */
    INTEGER baseline;   /* port coordinates */
    const Rect *dst;    /* the 1x rectangle StdBits draws (port coordinates) */
    std::vector<Glyph> glyphs;
    Fixed end;          /* the pen after the last one */
};

#if EXECUTOR_HIDPI

/* The display scale (1: off). Set once, before the screen is. */
void set_scale(double scale);
double scale();
inline bool active() { return scale() > 1.0; }

/* Whether thePort draws on the screen. */
bool drawing_on_screen();

/* StdText's drawing on the screen: BLIT draws the 1x pixels (StdBits);
   the run is recorded around it. */
void text_draw(const TextParams &p, const std::function<void()> &blit);

/* Whether text drawn in thePort now is recorded: the screen, or an
   offscreen pixel buffer (a GWorld's: phase 4, its runs carried to the
   screen by CopyBits). */
bool recording();

/* CopyBits' copy of SRC's SR to DST's DR (canonical pixmaps, their pixels
   locked; DST thePort's): BLIT copies the 1x pixels. The runs SRC still
   shows there go with them, where the copy lands. */
void copy_bits(const PixMap *src, const PixMap *dst, const Rect *sr, const Rect *dr, int mode,
               RgnHandle mask, const std::function<void()> &blit);

/* GW is gone (DisposeGWorld). */
void forget_offscreen(GWorldPtr gw);

/* Chrome drawn on the screen inside R (port coordinates): BLIT draws its
   1x pixels; SHAPES (port coordinates) are what the presentation surface
   draws instead, while the screen still has them. Build SHAPES only when
   active(). */
void chrome_draw(const Rect &r, Shapes shapes, const std::function<void()> &blit);

/* PORT's visRgn was computed again (CalcVis): its runs on the screen
   are clipped to it. A window put in front of them hides them where its
   pixels happen to be what theirs were (an alert's white over a text
   window's). */
void vis_changed(GrafPtr port);

/* The window PORT is hidden or closed: its runs on the screen go (what's
   behind is drawn there; a closed alert's pressed Cancel left behind
   showed wherever the pixels came back the same). */
void window_gone(GrafPtr port);

/* The presentation surface for the 1x frame ARGB (W x H, rows of W):
   RECTS (1x) changed since the last call, OUT (OW x OH) is redrawn there.
   On the front end's render thread, after ARGB is converted from the
   framebuffer. */
void compose(const uint32_t *argb, int w, int h, const DirtyRects::Rects &rects,
             uint32_t *out, int ow, int oh);

/* The size of the surface for a W x H screen. */
void scaled_size(int w, int h, int *ow, int *oh);

#else

inline void set_scale(double) {}
inline double scale() { return 1.0; }
inline bool active() { return false; }
inline bool drawing_on_screen() { return false; }
inline bool recording() { return false; }
inline void copy_bits(const PixMap *, const PixMap *, const Rect *, const Rect *, int, RgnHandle,
                      const std::function<void()> &blit) { blit(); }
inline void forget_offscreen(GWorldPtr) {}
inline void text_draw(const TextParams &, const std::function<void()> &blit) { blit(); }
inline void chrome_draw(const Rect &, Shapes, const std::function<void()> &blit) { blit(); }
inline void vis_changed(GrafPtr) {}
inline void window_gone(GrafPtr) {}
inline void compose(const uint32_t *, int, int, const DirtyRects::Rects &, uint32_t *, int, int) {}
inline void scaled_size(int w, int h, int *ow, int *oh) { *ow = w; *oh = h; }

#endif
}
