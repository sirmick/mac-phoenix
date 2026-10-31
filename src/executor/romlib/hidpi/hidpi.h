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
#include <vdriver/dirtyrect.h>

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
inline void text_draw(const TextParams &, const std::function<void()> &blit) { blit(); }
inline void compose(const uint32_t *, int, int, const DirtyRects::Rects &, uint32_t *, int, int) {}
inline void scaled_size(int w, int h, int *ow, int *oh) { *ow = w; *oh = h; }

#endif
}
