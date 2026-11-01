#pragma once
/* MacPhoenix: what hidpi.cpp (recording, Executor's side) and
   hidpi_skia.cpp (drawing, no Executor headers) share. Plain C++. */

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Executor::hidpi
{
/* A piece of chrome as vector geometry, in 1x pixel coordinates (pixel x
   covers [x, x + 1)): what a theme drawing call draws with QuickDraw's
   pixel steps, drawn anti-aliased at the display scale instead (phase 2,
   "chrome"). Filled, or stroked STROKE wide; in COLOR, or a radial
   gradient round (gx, gy) of radius gr through COLORS at POS (gr 0: a
   linear one from (gx, gy) to (gx2, gy2)). */
struct Shape
{
    enum Kind : uint8_t
    {
        Circle,   /* pts: cx, cy, r */
        Polygon,  /* pts: x, y, ... (closed) */
        Polyline, /* pts: x, y, ... (open; round joins and caps) */
    } kind;
    std::vector<float> pts;
    float stroke = 0;
    uint32_t color = 0xFF000000; /* 0xAARRGGBB */
    float gx = 0, gy = 0, gr = 0;
    float gx2 = 0, gy2 = 0;
    std::vector<uint32_t> colors;
    std::vector<float> pos;
    bool operator==(const Shape &) const = default;
};
using Shapes = std::vector<Shape>;
}

namespace Executor::hidpi::impl
{
/* An outline font's bytes (an 'sfnt' resource copied out of the guest)
   and its Macintosh Roman character map. Never freed: one per font. */
struct FontData
{
    std::vector<uint8_t> bytes;
    std::vector<uint16_t> cmap; /* 256 glyph ids */
};

struct IRect
{
    int top, left, bottom, right;
};

enum class Mode
{
    Or,   /* srcOr */
    Bic,  /* srcBic: the text in the background colour */
    Copy, /* srcCopy: the background box first */
    Xor,  /* srcXor: inverts, not anti-aliased */
    Grey, /* grayishTextOr: grey instead of a dither */
};

enum Synth
{
    synthBold = 1,
    synthItalic = 2,
};

/* A piece of text QuickDraw drew on the screen, in framebuffer
   coordinates (1x pixels). Immutable once recorded: a change makes a new
   one. */
struct Run
{
    IRect box;                       /* what StdBits drew (clipped to the screen) */
    int bpp;                         /* the framebuffer's, when recorded */
    int x0;                          /* the first pixel of the stored rows */
    int rowLen;                      /* their bytes */
    std::vector<uint8_t> before;     /* the box's rows before the text, */
    std::vector<uint8_t> after;      /* and after (raw framebuffer bytes) */
    Mode mode;
    uint32_t fg, bk;                 /* 0xAARRGGBB */
    std::shared_ptr<const FontData> font;
    float size;                      /* points (1x pixels per em) */
    int synth;                       /* Synth: what the outline lacks */
    int bold;                        /* synthBold: QuickDraw's smear (1x pixels) */
    bool underline, outline;
    int ulThick, shadow;
    float baseline;
    float ulLeft, ulRight;           /* the pen's extent */
    std::vector<uint16_t> glyphs;
    std::vector<float> xs;           /* each glyph's origin */
    std::vector<float> advances;     /* and the pen's advance after it (1x) */
    std::vector<IRect> clip;         /* where QuickDraw could draw: vis & clip & box */
    Shapes shapes;                   /* chrome (no glyphs): drawn instead of its pixels */
};

using Runs = std::vector<std::shared_ptr<const Run>>;

/* A pixel's raw value in a row of BPP bits a pixel. */
inline uint32_t raw_pixel(const uint8_t *row, int x, int bpp)
{
    switch(bpp)
    {
        case 1: return (row[x >> 3] >> (7 - (x & 7))) & 1;
        case 2: return (row[x >> 2] >> (6 - 2 * (x & 3))) & 3;
        case 4: return (row[x >> 1] >> (4 - 4 * (x & 1))) & 15;
        case 8: return row[x];
        case 16: return row[2 * x] << 8 | row[2 * x + 1];
        default:
            return (uint32_t)row[4 * x] << 24 | row[4 * x + 1] << 16 | row[4 * x + 2] << 8 | row[4 * x + 3];
    }
}

inline void set_raw_pixel(uint8_t *row, int x, int bpp, uint32_t v)
{
    switch(bpp)
    {
        case 1: row[x >> 3] = (row[x >> 3] & ~(0x80 >> (x & 7))) | (v & 1) << (7 - (x & 7)); break;
        case 2: row[x >> 2] = (row[x >> 2] & ~(0xC0 >> 2 * (x & 3))) | (v & 3) << (6 - 2 * (x & 3)); break;
        case 4: row[x >> 1] = (row[x >> 1] & ~(0xF0 >> 4 * (x & 1))) | (v & 15) << (4 - 4 * (x & 1)); break;
        case 8: row[x] = v; break;
        case 16: row[2 * x] = v >> 8; row[2 * x + 1] = v; break;
        default:
            row[4 * x] = v >> 24; row[4 * x + 1] = v >> 16; row[4 * x + 2] = v >> 8; row[4 * x + 3] = v;
    }
}

/* The screen's framebuffer, as the render thread sees it. */
struct Screen
{
    const uint8_t *data;
    int width, height, rowBytes, bpp;
    std::function<uint32_t(uint32_t)> argb; /* a raw pixel's colour */
};

/* Whether the screen still shows RUN: the pixels it changed are as it
   left them (srcCopy: its whole box). */
bool run_valid(const Run &run, const Screen &screen);

/* hidpi_skia.cpp: the presentation surface's RECTS (1x) redrawn: ARGB
   scaled up, RUNS drawn over it. */
void draw(double scale, const uint32_t *argb, int w, int h, const std::vector<IRect> &rects,
          uint32_t *out, int ow, int oh, const Runs &runs, const Screen &screen);
}
