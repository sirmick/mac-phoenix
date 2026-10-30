/* MacPhoenix: the Appearance Manager (Mac OS 8; docs/executor/Appearance.md).
 * Executor's own AppearanceLib and _AppearanceDispatch ($AA74), from
 * Appearance.h (Universal Interfaces 3.4) and a real Mac's answers
 * (tests/guest/appearance/reference-9.0.4.txt, 9.0.4 on the G3): the
 * platinum theme's colours, fonts and shapes. (appearance.cpp is something
 * else: ARDI's choice of look for Executor's own definition procedures.) */

#include <base/common.h>
#include <Appearance.h>
#include <CQuickDraw.h>
#include <QuickDraw.h>
#include <ProcessMgr.h>
#include <FontMgr.h>
#include <ToolboxUtil.h>
#include <MemoryMgr.h>
#include <WindowMgr.h>
#include <WindowMgr8.h>
#include <MenuMgr.h>
#include <quickdraw/cquick.h>
#include <wind/wind.h>
#include "appearance_draw.h"
#include <base/functions.impl.h>
#include <set>

using namespace Executor;

namespace
{

/* BRUSH -2..48 at depth 32, 8 and 1 (reference-9.0.4.txt); index + 2. */
const RGBColor brushes[3][51] = {
    { /* depth 32 */
        { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 }, { 0, 0, 0 }, { 0xdddd, 0xdddd, 0xdddd },
        { 0xdddd, 0xdddd, 0xdddd }, { 0xdddd, 0xdddd, 0xdddd }, { 0xdddd, 0xdddd, 0xdddd }, { 0xdddd, 0xdddd, 0xdddd },
        { 0xdddd, 0xdddd, 0xdddd }, { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff }, { 0xdddd, 0xdddd, 0xdddd },
        { 0xeeee, 0xeeee, 0xeeee }, { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 },
        { 0x6666, 0x6666, 0xcccc }, { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 },
        { 0x5555, 0x5555, 0x5555 }, { 0x6666, 0x6666, 0xcccc }, { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff },
        { 0x8888, 0x8888, 0x8888 }, { 0xffff, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0xeeee, 0xeeee, 0xeeee },
        { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 }, { 0x8888, 0x8888, 0x8888 }, { 0xdddd, 0xdddd, 0xdddd },
        { 0xdddd, 0xdddd, 0xdddd }, { 0x6666, 0x6666, 0x6666 }, { 0x7777, 0x7777, 0x7777 }, { 0xdddd, 0xdddd, 0xdddd },
        { 0x9999, 0x9999, 0x9999 }, { 0xffff, 0xffff, 0xffff }, { 0xdddd, 0xdddd, 0xdddd }, { 0xdddd, 0xdddd, 0xdddd },
        { 0xdddd, 0xdddd, 0xdddd }, { 0xdddd, 0xdddd, 0xdddd }, { 0x4444, 0x4444, 0x4444 }, { 0x8888, 0x8888, 0x8888 },
        { 0x5555, 0x5555, 0x5555 }, { 0x7777, 0x7777, 0x7777 }, { 0xffff, 0xffff, 0xffff }, { 0x8888, 0x8888, 0x8888 },
        { 0xdddd, 0xdddd, 0xdddd }, { 0xbbbb, 0xbbbb, 0xbbbb }, { 0xffff, 0xffff, 0xc6c6 },
    },
    { /* depth 8 */
        { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 }, { 0, 0, 0 }, { 0xdddd, 0xdddd, 0xdddd },
        { 0xdddd, 0xdddd, 0xdddd }, { 0xdddd, 0xdddd, 0xdddd }, { 0xdddd, 0xdddd, 0xdddd }, { 0xdddd, 0xdddd, 0xdddd },
        { 0xdddd, 0xdddd, 0xdddd }, { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff }, { 0xdddd, 0xdddd, 0xdddd },
        { 0xeeee, 0xeeee, 0xeeee }, { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 },
        { 0x6666, 0x6666, 0xcccc }, { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 },
        { 0x5555, 0x5555, 0x5555 }, { 0x6666, 0x6666, 0xcccc }, { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff },
        { 0x8888, 0x8888, 0x8888 }, { 0xffff, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0xeeee, 0xeeee, 0xeeee },
        { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 }, { 0x8888, 0x8888, 0x8888 }, { 0xdddd, 0xdddd, 0xdddd },
        { 0xdddd, 0xdddd, 0xdddd }, { 0x6666, 0x6666, 0x6666 }, { 0x7777, 0x7777, 0x7777 }, { 0xdddd, 0xdddd, 0xdddd },
        { 0x9999, 0x9999, 0x9999 }, { 0xffff, 0xffff, 0xffff }, { 0xdddd, 0xdddd, 0xdddd }, { 0xdddd, 0xdddd, 0xdddd },
        { 0xdddd, 0xdddd, 0xdddd }, { 0xdddd, 0xdddd, 0xdddd }, { 0x4444, 0x4444, 0x4444 }, { 0x8888, 0x8888, 0x8888 },
        { 0x5555, 0x5555, 0x5555 }, { 0x7777, 0x7777, 0x7777 }, { 0xffff, 0xffff, 0xffff }, { 0x8888, 0x8888, 0x8888 },
        { 0xdddd, 0xdddd, 0xdddd }, { 0xbbbb, 0xbbbb, 0xbbbb }, { 0xffff, 0xffff, 0xc6c6 },
    },
    { /* depth 1 */
        { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 }, { 0, 0, 0 }, { 0xffff, 0xffff, 0xffff },
        { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff },
        { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff },
        { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 },
        { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 },
        { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff },
        { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff },
        { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff },
        { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff },
        { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff },
        { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff },
        { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 },
        { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff },
    },
};
/* TEXT -2..47 at depth 32, 8 and 1 (reference-9.0.4.txt); index + 2. */
const RGBColor text_colors[3][50] = {
    { /* depth 32 */
        { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 }, { 0, 0, 0 }, { 0x0000, 0x0000, 0x0000 },
        { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 }, { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 },
        { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 }, { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 },
        { 0x7777, 0x7777, 0x7777 }, { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 }, { 0x7777, 0x7777, 0x7777 },
        { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 }, { 0x7777, 0x7777, 0x7777 }, { 0xffff, 0xffff, 0xffff },
        { 0x0000, 0x0000, 0x0000 }, { 0x7777, 0x7777, 0x7777 }, { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 },
        { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 },
        { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 }, { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 },
        { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff }, { 0x7777, 0x7777, 0x7777 },
        { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff }, { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 },
        { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff },
        { 0x7777, 0x7777, 0x7777 }, { 0x7777, 0x7777, 0x7777 }, { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 },
        { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 },
    },
    { /* depth 8 */
        { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 }, { 0, 0, 0 }, { 0x0000, 0x0000, 0x0000 },
        { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 }, { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 },
        { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 }, { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 },
        { 0x7777, 0x7777, 0x7777 }, { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 }, { 0x7777, 0x7777, 0x7777 },
        { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 }, { 0x7777, 0x7777, 0x7777 }, { 0xffff, 0xffff, 0xffff },
        { 0x0000, 0x0000, 0x0000 }, { 0x7777, 0x7777, 0x7777 }, { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 },
        { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 },
        { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 }, { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 },
        { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff }, { 0x7777, 0x7777, 0x7777 },
        { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff }, { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 },
        { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff },
        { 0x7777, 0x7777, 0x7777 }, { 0x7777, 0x7777, 0x7777 }, { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 },
        { 0x7777, 0x7777, 0x7777 }, { 0x0000, 0x0000, 0x0000 },
    },
    { /* depth 1 */
        { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 }, { 0, 0, 0 }, { 0x0000, 0x0000, 0x0000 },
        { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 },
        { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 },
        { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 },
        { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff },
        { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 },
        { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 },
        { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 },
        { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 },
        { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 },
        { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff },
        { 0x0000, 0x0000, 0x0000 }, { 0x0000, 0x0000, 0x0000 }, { 0xffff, 0xffff, 0xffff }, { 0xffff, 0xffff, 0xffff },
        { 0xffff, 0xffff, 0xffff }, { 0x0000, 0x0000, 0x0000 },
    },
};

/* Which reference table a depth uses: 1-bit (and 2) its own, else colour. */
int depth_row(SInt16 depth)
{
    return depth <= 2 ? 2 : depth <= 8 ? 1 : 0;
}

bool brush_color(ThemeBrush brush, SInt16 depth, RGBColor *c)
{
    int i = (int16_t)brush + 2;
    if(i < 0 || i >= (int)std::size(brushes[0]) || (int16_t)brush == 0)
        return false;
    *c = brushes[depth_row(depth)][i];
    return true;
}

bool text_color(ThemeTextColor color, SInt16 depth, RGBColor *c)
{
    int i = (int16_t)color + 2;
    if(i < 0 || i >= (int)std::size(text_colors[0]) || (int16_t)color == 0)
        return false;
    *c = text_colors[depth_row(depth)][i];
    return true;
}

/* The processes that called RegisterAppearanceClient (by PSN). */
std::set<std::pair<uint32_t, uint32_t>> clients;

std::pair<uint32_t, uint32_t> current_process()
{
    ProcessSerialNumber psn;
    GetCurrentProcess(&psn);
    return { psn.highLongOfPSN, psn.lowLongOfPSN };
}

/* The theme's fonts (reference: the others answer paramErr in 1.1.1). */
struct ThemeFontEntry
{
    const char *name; /* Pascal */
    int16_t size;
    Style style;
};
const ThemeFontEntry theme_fonts[] = {
    { "\010Charcoal", 12, 0 }, /* kThemeSystemFont */
    { "\006Geneva", 10, 0 }, /* kThemeSmallSystemFont */
    { "\006Geneva", 10, bold }, /* kThemeSmallEmphasizedSystemFont */
    { "\006Geneva", 10, 0 }, /* kThemeViewsFont */
};

/* A window type's frame around its content, and where its parts are
   (reference WINREGION lines: content 100,100 - 300,400). Boxes: an offset
   from the frame's top left (left/top) or right (negative left), a size. */
struct Box
{
    int16_t top, left, size; /* left < 0: from the frame's right edge */
};
struct WindowShape
{
    bool valid;
    int16_t top, left, bottom, right; /* frame widths around the content */
    int16_t title;                    /* title bar height (0: none) */
    bool side_title;                  /* the title bar is down the left */
    Box close, zoom, collapse;        /* size 0: none */
    int16_t grow;                     /* the grow box's size at the frame's
                                         bottom right (< 0: inside the
                                         content, the magnitude its size) */
    int16_t shadow;                   /* the drop shadow, right and below */
};
const WindowShape window_shapes[] = {
    /* kThemeDocumentWindow */
    { true, 22, 6, 7, 7, 22, false, { 5, 5, 11 }, { 5, -33, 11 }, { 5, -17, 11 }, 22, 1 },
    /* kThemeDialogWindow */
    { true, 3, 3, 4, 4, 0, false, {}, {}, {}, 0, 1 },
    /* kThemeMovableDialogWindow */
    { true, 24, 3, 4, 4, 22, false, { 5, 5, 11 }, { 5, -33, 11 }, { 5, -17, 11 }, -14, 1 },
    /* kThemeAlertWindow */
    { true, 3, 3, 4, 4, 0, false, {}, {}, {}, 0, 1 },
    /* kThemeMovableAlertWindow */
    { true, 24, 3, 4, 4, 22, false, { 5, 5, 11 }, { 5, -33, 11 }, { 5, -17, 11 }, -14, 1 },
    /* kThemePlainDialogWindow */
    { true, 1, 1, 1, 1, 0, false, {}, {}, {}, 0, 0 },
    /* kThemeShadowDialogWindow */
    { true, 1, 1, 3, 3, 0, false, {}, {}, {}, 0, 2 },
    /* kThemePopupWindow (its tab on top: title bar 5 high, 27 wide) */
    { true, 10, 4, 1, 5, 0, false, {}, {}, {}, 0, 1 },
    /* kThemeUtilityWindow */
    { true, 14, 4, 5, 5, 14, false, { 3, 3, 8 }, { 3, -23, 8 }, { 3, -12, 8 }, 15, 1 },
    /* kThemeUtilitySideWindow */
    { true, 4, 14, 5, 5, 14, true, { 3, 3, 8 }, { -23, 3, 8 }, { -12, 3, 8 }, 15, 1 },
};

/* The title text's rectangle in a title bar ft..ft + title, fl..fr: the
   metrics' width centred, kept clear of the boxes. */
Rect title_text_rect(const WindowShape &w, int16_t ft, int16_t fl, int16_t fr,
                     const ThemeWindowMetrics *metrics, ThemeWindowAttributes attributes)
{
    int16_t width = metrics ? (int16_t)metrics->titleWidth : 0;
    int16_t lo = fl + (w.close.size ? w.close.left + w.close.size + 8 : 8);
    int16_t hi = fr - (w.zoom.size ? -w.zoom.left + 8 : 8);
    Rect r;
    r.top = ft + 3;
    r.bottom = ft + w.title - 3;
    r.left = std::max<int16_t>(lo, (fl + fr) / 2 - width / 2);
    r.right = std::min<int16_t>(hi, r.left + width);
    if(!(attributes & kThemeWindowHasTitleText))
        r.right = r.left;
    return r;
}

void set_rect_rgn(RgnHandle rgn, int16_t top, int16_t left, int16_t bottom, int16_t right)
{
    Rect r;
    SetRect(&r, left, top, right, bottom);
    RectRgn(rgn, &r);
}
}

/* ── Clients ─────────────────────────────────────────────────────────── */

OSStatus Executor::C_RegisterAppearanceClient()
{
    return clients.insert(current_process()).second ? noErr : themeProcessRegisteredErr;
}

OSStatus Executor::C_UnregisterAppearanceClient()
{
    return clients.erase(current_process()) ? noErr : themeProcessNotRegisteredErr;
}

Boolean Executor::C_IsAppearanceClient(const ProcessSerialNumber *process)
{
    return process && clients.count({ process->highLongOfPSN, process->lowLongOfPSN });
}

/* ── Brushes and colours ─────────────────────────────────────────────── */

Boolean Executor::C_IsThemeInColor(SInt16 inDepth, Boolean inIsColorDevice)
{
    return inDepth > 2; /* reference: 8 and 32 bits in colour, colour device or not */
}

OSStatus Executor::C_GetThemeBrushAsColor(ThemeBrush inBrush, SInt16 inDepth, Boolean inColorDev,
                                          RGBColor *outColor)
{
    RGBColor c;
    if(!brush_color(inBrush, inDepth, &c))
        return themeInvalidBrushErr;
    if(outColor)
        *outColor = c;
    return noErr;
}

OSStatus Executor::C_GetThemeTextColor(ThemeTextColor inColor, SInt16 inDepth, Boolean inColorDev,
                                       RGBColor *outColor)
{
    RGBColor c;
    if(!text_color(inColor, inDepth, &c))
        return themeBadTextColorErr;
    if(outColor)
        *outColor = c;
    return noErr;
}

OSStatus Executor::C_SetThemePen(ThemeBrush inBrush, SInt16 inDepth, Boolean inIsColorDevice)
{
    RGBColor c;
    if(!brush_color(inBrush, inDepth, &c))
        return themeInvalidBrushErr;
    PenPat(&qdGlobals().black);
    RGBForeColor(&c);
    return noErr;
}

OSStatus Executor::C_SetThemeBackground(ThemeBrush inBrush, SInt16 inDepth, Boolean inIsColorDevice)
{
    RGBColor c;
    if(!brush_color(inBrush, inDepth, &c))
        return themeInvalidBrushErr;
    BackPat(&qdGlobals().white);
    RGBBackColor(&c);
    return noErr;
}

OSStatus Executor::C_SetThemeTextColor(ThemeTextColor inColor, SInt16 inDepth, Boolean inIsColorDevice)
{
    RGBColor c;
    if(!text_color(inColor, inDepth, &c))
        return themeBadTextColorErr;
    RGBForeColor(&c);
    return noErr;
}

OSStatus Executor::C_SetThemeWindowBackground(WindowRef inWindow, ThemeBrush inBrush, Boolean inUpdate)
{
    RGBColor c;
    if(!inWindow)
        return paramErr;
    if(!brush_color(inBrush, 32, &c))
        return themeInvalidBrushErr;
    GUEST<GrafPtr> save;
    GetPort(&save);
    SetPort(inWindow);
    BackPat(&qdGlobals().white);
    RGBBackColor(&c);
    if(inUpdate)
    {
        Rect r = PORT_RECT(inWindow);
        EraseRect(&r);
        InvalRect(&r);
    }
    SetPort(save);
    return noErr;
}

OSStatus Executor::C_SetThemeTextColorForWindow(WindowRef window, Boolean isActive, SInt16 depth,
                                                Boolean isColorDev)
{
    if(!window)
        return paramErr;
    return C_SetThemeTextColor(isActive ? kThemeTextColorDialogActive : kThemeTextColorDialogInactive,
                               depth, isColorDev);
}

OSStatus Executor::C_GetThemeAccentColors(GUEST<CTabHandle> *outColors)
{
    static const uint16_t blues[8][3] = {
        { 0xEEEE, 0xEEEE, 0xEEEE }, { 0xCCCC, 0xCCCC, 0xFFFF }, { 0x9999, 0x9999, 0xFFFF },
        { 0x6666, 0x6666, 0xCCCC }, { 0x3333, 0x3333, 0x9999 }, { 0x0000, 0x0000, 0x8888 },
        { 0x0000, 0x0000, 0x5555 }, { 0x0000, 0x0000, 0x0000 },
    };
    if(!outColors)
        return paramErr;
    CTabHandle ct = (CTabHandle)NewHandleClear(sizeof(ColorTable) + 7 * sizeof(ColorSpec));
    if(!ct)
        return memFullErr;
    (*ct)->ctSeed = GetCTSeed();
    (*ct)->ctSize = 7;
    for(int i = 0; i < 8; i++)
    {
        (*ct)->ctTable[i].value = i;
        (*ct)->ctTable[i].rgb.red = blues[i][0];
        (*ct)->ctTable[i].rgb.green = blues[i][1];
        (*ct)->ctTable[i].rgb.blue = blues[i][2];
    }
    *outColors = ct;
    return noErr;
}

/* ── Fonts ───────────────────────────────────────────────────────────── */

OSStatus Executor::C_GetThemeFont(ThemeFontID inFontID, ScriptCode inScript, Str255 outFontName,
                                  GUEST<SInt16> *outFontSize, Style *outStyle)
{
    if(inFontID >= std::size(theme_fonts))
        return paramErr;
    const ThemeFontEntry &f = theme_fonts[inFontID];
    if(outFontName)
        memcpy(outFontName, f.name, f.name[0] + 1);
    if(outFontSize)
        *outFontSize = f.size;
    if(outStyle)
        *outStyle = f.style;
    return noErr;
}

OSStatus Executor::C_UseThemeFont(ThemeFontID inFontID, ScriptCode inScript)
{
    if(inFontID >= std::size(theme_fonts))
        return paramErr;
    const ThemeFontEntry &f = theme_fonts[inFontID];
    GUEST<INTEGER> num;
    GetFNum((StringPtr)f.name, &num);
    TextFont(num); /* 0, the system font, when the theme's isn't installed */
    TextSize(f.size);
    TextFace(f.style);
    return noErr;
}

/* ── Menus ───────────────────────────────────────────────────────────── */

OSStatus Executor::C_GetThemeMenuBarHeight(GUEST<SInt16> *outHeight)
{
    if(!outHeight)
        return paramErr;
    *outHeight = 20;
    return noErr;
}

OSStatus Executor::C_GetThemeMenuSeparatorHeight(GUEST<SInt16> *outHeight)
{
    if(!outHeight)
        return paramErr;
    *outHeight = 6;
    return noErr;
}

OSStatus Executor::C_GetThemeMenuItemExtra(ThemeMenuItemType inItemType, GUEST<SInt16> *outHeight,
                                           GUEST<SInt16> *outWidth)
{
    if(outHeight)
        *outHeight = 0;
    if(outWidth)
        *outWidth = inItemType == kThemeMenuItemHierarchical ? 28 : 4;
    return noErr;
}

OSStatus Executor::C_GetThemeMenuTitleExtra(GUEST<SInt16> *outWidth, Boolean inIsSquished)
{
    if(!outWidth)
        return paramErr;
    *outWidth = inIsSquished ? 9 : 13;
    return noErr;
}

/* ── Styles ──────────────────────────────────────────────────────────── */

OSStatus Executor::C_GetThemeScrollBarThumbStyle(GUEST<ThemeScrollBarThumbStyle> *outStyle)
{
    if(!outStyle)
        return paramErr;
    *outStyle = kThemeScrollBarThumbProportional;
    return noErr;
}

OSStatus Executor::C_GetThemeScrollBarArrowStyle(GUEST<ThemeScrollBarArrowStyle> *outStyle)
{
    if(!outStyle)
        return paramErr;
    *outStyle = kThemeScrollBarArrowsLowerRight;
    return noErr;
}

OSStatus Executor::C_GetThemeCheckBoxStyle(GUEST<ThemeCheckBoxStyle> *outStyle)
{
    if(!outStyle)
        return paramErr;
    *outStyle = kThemeCheckBoxCheckMark;
    return noErr;
}

/* ── Cursors ─────────────────────────────────────────────────────────── */

OSStatus Executor::C_SetThemeCursor(ThemeCursor inCursor)
{
    /* The System's cursors where it has one ('CURS' 1-4: I-beam, cross,
       plus, watch), the arrow for the rest. */
    int16_t curs;
    switch(inCursor)
    {
        case kThemeIBeamCursor: curs = iBeamCursor; break;
        case kThemeCrossCursor: curs = crossCursor; break;
        case kThemePlusCursor: curs = plusCursor; break;
        case kThemeWatchCursor: curs = watchCursor; break;
        default:
            if(inCursor > kThemeResizeLeftRightCursor)
                return themeBadCursorIndexErr;
            SetCursor(&qdGlobals().arrow);
            return noErr;
    }
    if(CursHandle h = GetCursor(curs))
        SetCursor(*h);
    else
        SetCursor(&qdGlobals().arrow);
    return noErr;
}

OSStatus Executor::C_SetAnimatedThemeCursor(ThemeCursor inCursor, UInt32 inAnimationStep)
{
    return C_SetThemeCursor(inCursor);
}

/* ── Windows ─────────────────────────────────────────────────────────── */

OSStatus Executor::C_GetThemeWindowRegion(ThemeWindowType flavor, const Rect *contRect,
                                          ThemeDrawState state, const ThemeWindowMetrics *metrics,
                                          ThemeWindowAttributes attributes, WindowRegionCode winRegion,
                                          RgnHandle rgn)
{
    if(!contRect || !rgn || flavor >= std::size(window_shapes))
        return paramErr;
    const WindowShape &w = window_shapes[flavor];
    Rect c = *contRect;
    int16_t ft = c.top - w.top, fl = c.left - w.left, fb = c.bottom + w.bottom, fr = c.right + w.right;
    /* A box the attributes leave out is an empty region. */
    auto box = [&](const Box &b, ThemeWindowAttributes needs) -> OSStatus {
        if(!b.size)
            return errWindowRegionCodeInvalid;
        if(!(attributes & needs))
        {
            SetEmptyRgn(rgn);
            return noErr;
        }
        int16_t top = b.top < 0 ? fb + b.top : ft + b.top;
        int16_t left = b.left < 0 ? fr + b.left : fl + b.left;
        set_rect_rgn(rgn, top, left, top + b.size, left + b.size);
        return noErr;
    };
    switch(winRegion)
    {
        case kWindowStructureRgn:
        {
            /* The frame and its drop shadow: the frame's rectangle with the
               same offset by the shadow (reference: a two-notched region). */
            set_rect_rgn(rgn, ft, fl, fb - w.shadow, fr - w.shadow);
            if(w.shadow)
            {
                RgnHandle s = NewRgn();
                set_rect_rgn(s, ft + w.shadow, fl + w.shadow, fb, fr);
                UnionRgn(rgn, s, rgn);
                DisposeRgn(s);
            }
            return noErr;
        }
        case kWindowDragRgn:
        {
            /* The frame outside the content and the title bar's boxes. */
            if(!w.title)
                return errWindowRegionCodeInvalid;
            C_GetThemeWindowRegion(flavor, contRect, state, metrics, attributes, kWindowStructureRgn, rgn);
            RgnHandle part = NewRgn();
            RectRgn(part, &c);
            DiffRgn(rgn, part, rgn);
            for(WindowRegionCode code : { kWindowCloseBoxRgn, kWindowZoomBoxRgn, kWindowCollapseBoxRgn })
                if(C_GetThemeWindowRegion(flavor, contRect, state, metrics, attributes, code, part) == noErr)
                    DiffRgn(rgn, part, rgn);
            DisposeRgn(part);
            return noErr;
        }
        case kWindowContentRgn:
        {
            /* Less a grow box that reaches into it. */
            RectRgn(rgn, &c);
            if(w.grow)
            {
                RgnHandle g = NewRgn();
                C_GetThemeWindowRegion(flavor, contRect, state, metrics, attributes, kWindowGrowRgn, g);
                DiffRgn(rgn, g, rgn);
                DisposeRgn(g);
            }
            return noErr;
        }
        case kWindowTitleBarRgn:
            if(!w.title)
                return errWindowRegionCodeInvalid;
            if(w.side_title)
                set_rect_rgn(rgn, ft, fl, fb - 1, fl + w.title);
            else
                set_rect_rgn(rgn, ft, fl, ft + w.title, fr - 1);
            return noErr;
        case kWindowCloseBoxRgn:
            return box(w.close, kThemeWindowHasCloseBox);
        case kWindowZoomBoxRgn:
            return box(w.zoom, kThemeWindowHasFullZoom);
        case kWindowCollapseBoxRgn:
            return box(w.collapse, kThemeWindowHasCollapseBox);
        case kWindowTitleTextRgn:
        case kWindowTitleProxyIconRgn:
        {
            /* The title centred in the title bar, metrics->titleWidth wide,
               between the boxes; the proxy icon (16 x 16) left of it. */
            if(!w.title || w.side_title)
                return errWindowRegionCodeInvalid;
            Rect text = title_text_rect(w, ft, fl, fr, metrics, attributes);
            if(winRegion == kWindowTitleProxyIconRgn)
                set_rect_rgn(rgn, ft + (w.title - 16) / 2, text.left - 20,
                             ft + (w.title - 16) / 2 + 16, text.left - 4);
            else
                RectRgn(rgn, &text);
            return noErr;
        }
        case kWindowGrowRgn:
            if(!w.grow)
                return errWindowRegionCodeInvalid;
            if(!(attributes & kThemeWindowHasGrow))
            {
                SetEmptyRgn(rgn);
                return noErr;
            }
            if(w.grow > 0)
                set_rect_rgn(rgn, fb - w.grow, fr - w.grow, fb, fr);
            else
                set_rect_rgn(rgn, c.bottom + w.grow, c.right + w.grow, c.bottom, c.right);
            return noErr;
        default:
            return errWindowRegionCodeInvalid;
    }
}

namespace
{
using namespace appearance;

/* A title bar box (close, zoom, collapse), up or pressed. */
void draw_widget(const Rect &r, ThemeTitleBarWidget widget, bool pressed)
{
    boxFace(r, pressed);
    if(widget == kThemeWidgetZoomBox)
        frameGray(Rect{ r.top, r.left, (int16_t)(r.top + 6), (int16_t)(r.left + 6) }, kBlack);
    else if(widget == kThemeWidgetCollapseBox)
    {
        int16_t mid = (r.top + r.bottom) / 2;
        lineGray(r.left + 1, mid - 1, r.right - 2, mid - 1, kBlack);
        lineGray(r.left + 1, mid + 1, r.right - 2, mid + 1, kBlack);
    }
}

/* The parts of a window type's frame around a content rectangle. */
struct Frame
{
    const WindowShape &w;
    Rect content, outline; /* the frame without its shadow */
    int16_t ft, fl, fb, fr;

    Frame(const WindowShape &shape, const Rect &c)
        : w(shape), content(c)
    {
        ft = c.top - w.top;
        fl = c.left - w.left;
        fb = c.bottom + w.bottom;
        fr = c.right + w.right;
        SetRect(&outline, fl, ft, fr - w.shadow, fb - w.shadow);
    }
    bool box(const Box &b, Rect *r) const
    {
        if(!b.size)
            return false;
        int16_t top = b.top < 0 ? fb + b.top : ft + b.top;
        int16_t left = b.left < 0 ? fr + b.left : fl + b.left;
        SetRect(r, left, top, left + b.size, top + b.size);
        return true;
    }
    Rect titleBar() const
    {
        Rect r;
        if(w.side_title)
            SetRect(&r, fl + 1, ft + 1, fl + w.title, fb - w.shadow - 1);
        else
            SetRect(&r, fl + 1, ft + 1, fr - w.shadow - 1, ft + w.title);
        return r;
    }
};

bool widget_rect(const Frame &f, ThemeTitleBarWidget widget, ThemeWindowAttributes attributes, Rect *r)
{
    switch(widget)
    {
        case kThemeWidgetCloseBox:
        case kThemeWidgetDirtyCloseBox:
            return (attributes & kThemeWindowHasCloseBox) && f.box(f.w.close, r);
        case kThemeWidgetZoomBox:
            return (attributes & kThemeWindowHasFullZoom) && f.box(f.w.zoom, r);
        case kThemeWidgetCollapseBox:
            return (attributes & kThemeWindowHasCollapseBox) && f.box(f.w.collapse, r);
    }
    return false;
}
}

/* Executor's look: a black outline and drop shadow, the frame in the face
   gray with a bevel, the content edged in dark gray; an active title bar
   striped, its boxes drawn, the title's room left clear for the window
   definition's titleProc. */
OSStatus Executor::C_DrawThemeWindowFrame(ThemeWindowType flavor, const Rect *contRect,
                                          ThemeDrawState state, const ThemeWindowMetrics *metrics,
                                          ThemeWindowAttributes attributes,
                                          WindowTitleDrawingUPP titleProc, UInt32 titleData)
{
    if(!contRect || flavor >= std::size(window_shapes))
        return paramErr;
    const WindowShape &w = window_shapes[flavor];
    Frame f(w, *contRect);
    bool active = state == kThemeStateActive || state == kThemeStatePressed;
    {
        DrawGuard guard;
        Rect r = f.outline;
        frameGray(r, kBlack);
        if(w.shadow)
        {
            lineGray(r.left + w.shadow, r.bottom, r.right, r.bottom, kBlack);
            LineTo(r.right, r.top + w.shadow);
        }
        /* the frame between the outline and the content's edge */
        InsetRect(&r, 1, 1);
        RgnHandle band = NewRgn(), inner = NewRgn();
        RectRgn(band, &r);
        Rect c = *contRect;
        InsetRect(&c, -1, -1);
        RectRgn(inner, &c);
        DiffRgn(band, inner, band);
        gray(active ? kFace : kLight);
        PaintRgn(band);
        DisposeRgn(band);
        DisposeRgn(inner);
        if(active && r.right - r.left > 2)
            bevel(r, kWhite, kShadow);
        if(c.right - c.left > 0 && (w.top > 1 || w.left > 1))
            frameGray(c, active ? kDark : kShadow);

        if(w.title)
        {
            Rect bar = f.titleBar();
            Rect text = { 0, 0, 0, 0 };
            if(!w.side_title)
                text = title_text_rect(w, f.ft, f.fl, f.fr, metrics, attributes);
            if(active)
            {
                /* the stripes, then the boxes and the title cleared of them */
                if(w.side_title)
                    for(int16_t x = bar.left + 3; x < bar.right - 3; x += 2)
                    {
                        lineGray(x, bar.top + 2, x, bar.bottom - 3, kWhite);
                        lineGray(x + 1, bar.top + 3, x + 1, bar.bottom - 2, kShadow);
                    }
                else
                    for(int16_t y = bar.top + 3; y < bar.bottom - 3; y += 2)
                    {
                        lineGray(bar.left + 2, y, bar.right - 3, y, kWhite);
                        lineGray(bar.left + 3, y + 1, bar.right - 2, y + 1, kShadow);
                    }
                for(ThemeTitleBarWidget wd : { kThemeWidgetCloseBox, kThemeWidgetZoomBox, kThemeWidgetCollapseBox })
                {
                    Rect b;
                    if(widget_rect(f, wd, attributes, &b))
                    {
                        Rect clear = b;
                        InsetRect(&clear, -2, -2);
                        fillGray(clear, kFace);
                        draw_widget(b, wd, false);
                    }
                }
                if(text.right > text.left)
                {
                    Rect clear = text;
                    InsetRect(&clear, -6, 0);
                    clear.top = bar.top + 1;
                    clear.bottom = bar.bottom - 1;
                    fillGray(clear, kFace);
                }
            }
            if(w.grow > 0 && (attributes & kThemeWindowHasGrow) && active)
            {
                /* the corner square inside the content, two boxes in it */
                Rect g = { (int16_t)(contRect->bottom - w.grow + w.bottom), (int16_t)(contRect->right - w.grow + w.right),
                           contRect->bottom, contRect->right };
                fillGray(g, kFace);
                lineGray(g.left - 1, g.top - 1, g.right - 1, g.top - 1, kDark);
                lineGray(g.left - 1, g.top - 1, g.left - 1, g.bottom - 1, kDark);
                Rect small = { (int16_t)(g.top + 5), (int16_t)(g.left + 5), (int16_t)(g.top + 11), (int16_t)(g.left + 11) };
                Rect big = { (int16_t)(g.top + 3), (int16_t)(g.left + 3), (int16_t)(g.top + 9), (int16_t)(g.left + 9) };
                frameGray(small, kBlack);
                fillGray(Rect{ (int16_t)(big.top + 1), (int16_t)(big.left + 1), (int16_t)(big.bottom - 1), (int16_t)(big.right - 1) }, kFace);
                frameGray(big, kBlack);
            }
            if(text.right > text.left && titleProc)
            {
                SInt16 depth;
                Boolean color;
                portDepth(&depth, &color);
                gray(active ? kBlack : kShadow);
                titleProc(&text, depth, color, titleData);
            }
        }
    }
    return noErr;
}

OSStatus Executor::C_DrawThemeTitleBarWidget(ThemeWindowType flavor, const Rect *contRect,
                                             ThemeDrawState state, const ThemeWindowMetrics *metrics,
                                             ThemeWindowAttributes attributes, ThemeTitleBarWidget widget)
{
    if(!contRect || flavor >= std::size(window_shapes))
        return paramErr;
    Frame f(window_shapes[flavor], *contRect);
    Rect b;
    /* asked for, the box is there whatever the attributes say */
    if(!widget_rect(f, widget, ~0u, &b))
        return paramErr;
    DrawGuard guard;
    draw_widget(b, widget, state == kThemeStatePressed);
    return noErr;
}

Boolean Executor::C_GetThemeWindowRegionHit(ThemeWindowType flavor, const Rect *inContRect,
                                            ThemeDrawState state, const ThemeWindowMetrics *metrics,
                                            ThemeWindowAttributes inAttributes, Point inPoint,
                                            GUEST<WindowRegionCode> *outRegionHit)
{
    if(!inContRect || flavor >= std::size(window_shapes))
        return false;
    RgnHandle rgn = NewRgn();
    bool hit = false;
    for(WindowRegionCode code : { kWindowCloseBoxRgn, kWindowZoomBoxRgn, kWindowCollapseBoxRgn,
                                  kWindowGrowRgn, kWindowContentRgn, kWindowDragRgn })
    {
        if(C_GetThemeWindowRegion(flavor, inContRect, state, metrics, inAttributes, code, rgn) == noErr
           && PtInRgn(inPoint, rgn))
        {
            *outRegionHit = code;
            hit = true;
            break;
        }
    }
    DisposeRgn(rgn);
    return hit;
}

OSStatus Executor::C_DrawThemeScrollBarDelimiters(ThemeWindowType flavor, const Rect *inContRect,
                                                  ThemeDrawState state, ThemeWindowAttributes attributes)
{
    /* The lines where a document window's scroll bars meet its frame:
       Executor's scroll bars draw their own outlines. */
    return inContRect ? noErr : paramErr;
}

/* ── AppearanceLib's window and menu calls (MacWindows.h, Menus.h) ───── */

/* A window's regions in global coordinates, from the Window Manager's
   record: the structure, the content, the update region, the port. The
   parts a window definition knows (title bar, boxes) need its
   kWindowMsgGetRegion, not yet. */
OSStatus Executor::C_GetWindowRegion(WindowRef window, WindowRegionCode inRegionCode, RgnHandle ioWinRgn)
{
    if(!window || !ioWinRgn)
        return paramErr;
    switch(inRegionCode)
    {
        case kWindowStructureRgn:
            CopyRgn(WINDOW_STRUCT_REGION(window), ioWinRgn);
            return noErr;
        case kWindowContentRgn:
            CopyRgn(WINDOW_CONT_REGION(window), ioWinRgn);
            return noErr;
        case kWindowUpdateRgn:
            CopyRgn(WINDOW_UPDATE_REGION(window), ioWinRgn);
            return noErr;
        case kWindowGlobalPortRgn:
        {
            Rect r = PORT_RECT(window);
            OffsetRect(&r, -PORT_BOUNDS(window).left, -PORT_BOUNDS(window).top);
            RectRgn(ioWinRgn, &r);
            return noErr;
        }
        default:
            return errWindowRegionCodeInvalid;
    }
}

/* Executor doesn't collapse windows (window shading) yet. */
Boolean Executor::C_IsWindowCollapsed(WindowRef window)
{
    return false;
}

Boolean Executor::C_IsWindowCollapsable(WindowRef window)
{
    return false;
}

/* A command key's glyph (Menus.h kMenu*Glyph): accepted; Executor's menu
   definition draws the command character. */
OSErr Executor::C_SetMenuItemKeyGlyph(MenuRef inMenu, SInt16 inItem, SInt16 inGlyph)
{
    return inMenu ? noErr : paramErr;
}

OSStatus Executor::C_GetViewsFont(ScriptCode script, StringPtr outFontName, GUEST<SInt16> *outFontSize)
{
    GUEST<SInt16> size;
    Style style;
    OSStatus err = C_GetThemeFont(kThemeViewsFont, script, outFontName, &size, &style);
    if(err == noErr && outFontSize)
        *outFontSize = size;
    return err;
}

/* ── Drawing state ───────────────────────────────────────────────────── */

namespace
{
/* What GetThemeDrawingState keeps of the current port: the pen, the
   colours, the background pattern, the text settings. A guest pointer
   (ThemeDrawingState is opaque to the caller). */
struct SavedDrawingState
{
    PenState pen;
    RGBColor fore, back;
    GUEST<PixPatHandle> back_pixpat; /* a colour port's */
    Pattern back_pat;                /* a black-and-white port's */
    GUEST<INTEGER> font, size, mode;
    Style face;
};
}

OSStatus Executor::C_GetThemeDrawingState(GUEST<ThemeDrawingState> *outState)
{
    if(!outState)
        return paramErr;
    GrafPtr port = qdGlobals().thePort;
    auto *s = (SavedDrawingState *)NewPtrClear(sizeof(SavedDrawingState));
    if(!s)
        return memFullErr;
    GetPenState(&s->pen);
    GetForeColor(&s->fore);
    GetBackColor(&s->back);
    if(CGrafPort_p(port))
        s->back_pixpat = CPORT_BK_PIXPAT(port);
    else
        s->back_pat = PORT_BK_PAT(port);
    s->font = PORT_TX_FONT(port);
    s->size = PORT_TX_SIZE(port);
    s->mode = PORT_TX_MODE(port);
    s->face = PORT_TX_FACE(port);
    *outState = (ThemeDrawingState)s;
    return noErr;
}

OSStatus Executor::C_SetThemeDrawingState(ThemeDrawingState inState, Boolean inDisposeNow)
{
    auto *s = (SavedDrawingState *)inState;
    if(!s)
        return paramErr;
    GrafPtr port = qdGlobals().thePort;
    SetPenState(&s->pen);
    RGBForeColor(&s->fore);
    RGBBackColor(&s->back);
    if(CGrafPort_p(port))
    {
        if(s->back_pixpat)
            BackPixPat(s->back_pixpat);
    }
    else
        BackPat(&s->back_pat);
    TextFont(s->font);
    TextSize(s->size);
    TextMode(s->mode);
    TextFace(s->face);
    if(inDisposeNow)
        DisposePtr((Ptr)s);
    return noErr;
}

OSStatus Executor::C_DisposeThemeDrawingState(ThemeDrawingState inState)
{
    if(!inState)
        return paramErr;
    DisposePtr((Ptr)inState);
    return noErr;
}

/* The state the theme draws from: a normal pen, black on white, the
   system font in srcOr. */
OSStatus Executor::C_NormalizeThemeDrawingState()
{
    PenNormal();
    ForeColor(blackColor);
    BackColor(whiteColor);
    BackPat(&qdGlobals().white);
    TextFont(0);
    TextSize(0);
    TextFace(0);
    TextMode(srcOr);
    return noErr;
}
