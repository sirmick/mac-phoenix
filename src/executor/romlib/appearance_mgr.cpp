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
#include <mman/mman.h>
#include <FileMgr.h>
#include <AliasMgr.h>
#include <Displays.h>
#include <wind/wind.h>
#include "appearance_draw.h"
#include <rsys/appearance.h>
#include <base/functions.impl.h>
#include <set>
#include <map>
#include <Collections.h>
#include <ResourceMgr.h>
#include <MemoryMgr.h>

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
    ROMlib_set_window_theme_background(inWindow, c);
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
    *outStyle = kThemeScrollBarThumbNormal; /* 8.6's default: Smart Scrolling off */
    return noErr;
}

OSStatus Executor::C_GetThemeScrollBarArrowStyle(GUEST<ThemeScrollBarArrowStyle> *outStyle)
{
    if(!outStyle)
        return paramErr;
    *outStyle = kThemeScrollBarArrowsSingle; /* an arrow at each end, as 8.6 */
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
    /* no metrics: paramErr, nothing drawn (8.6) */
    if(!contRect || !metrics || flavor >= std::size(window_shapes))
        return paramErr;
    const WindowShape &w = window_shapes[flavor];
    Frame f(w, *contRect);
    bool active = state == kThemeStateActive || state == kThemeStatePressed;
    /* the platinum frames the window definitions draw (appearance_draw.cpp),
       the title left to titleProc */
    bool close = attributes & kThemeWindowHasCloseBox, collapse = attributes & kThemeWindowHasCollapseBox;
    int zoom = ((attributes & kThemeWindowHasVerticalZoom) ? 4 : 0) | ((attributes & kThemeWindowHasHorizontalZoom) ? 8 : 0);
    switch(flavor)
    {
        case kThemeDocumentWindow:
            appearance::documentFrame(*contRect, active, { close, zoom != 0, collapse, false }, nullptr);
            break;
        case kThemeDialogWindow:
        case kThemeAlertWindow:
            appearance::dialogFrame(*contRect, active, true, flavor == kThemeAlertWindow);
            break;
        case kThemeMovableDialogWindow:
        case kThemeMovableAlertWindow:
        {
            appearance::WindowParts parts = { close, zoom != 0, collapse, false };
            appearance::movableDialogFrame(*contRect, active, true, nullptr, &parts,
                                           flavor == kThemeMovableAlertWindow);
            break;
        }
        case kThemePlainDialogWindow:
        case kThemeShadowDialogWindow:
        {
            /* a black outline round the content; the shadowed one's shadow
               2 deep, 2 in from the corners (8.6) */
            DrawGuard guard;
            const Rect &c = *contRect;
            Rect outline = { (INTEGER)(c.top - 1), (INTEGER)(c.left - 1), (INTEGER)(c.bottom + 1), (INTEGER)(c.right + 1) };
            frameGray(outline, kBlack);
            if(flavor == kThemeShadowDialogWindow)
            {
                fillGray(Rect{ (INTEGER)(c.top + 1), (INTEGER)(c.right + 1), (INTEGER)(c.bottom + 3), (INTEGER)(c.right + 3) }, kBlack);
                fillGray(Rect{ (INTEGER)(c.bottom + 1), (INTEGER)(c.left + 1), (INTEGER)(c.bottom + 3), (INTEGER)(c.right + 3) }, kBlack);
            }
            break;
        }
        case kThemePopupWindow:
        {
            /* the tab 27 wide unless told (the title's width doesn't
               widen it), at the offset from the frame's left, centred on
               the window, or centred on the offset, kept on the frame
               (appr draw32.windowframe.popup.*) */
            int fl = contRect->left - 4, fr = contRect->right + 3;
            int width = metrics->popupTabWidth, left = fl + metrics->popupTabOffset;
            if(width <= 0)
                width = 27;
            if(metrics->popupTabPosition == kThemePopupTabCenterOnWindow)
                left = (fl + fr + 1) / 2 - width / 2;
            else if(metrics->popupTabPosition == kThemePopupTabCenterOnOffset)
                left -= width / 2;
            left = std::max(fl, std::min(left, fr + 1 - width));
            appearance::popupFrame(*contRect, active, left, width, std::max<int>(0, metrics->titleHeight));
            break;
        }
        case kThemeUtilityWindow:
        case kThemeUtilitySideWindow:
            /* (the side one has no collapse box: 8.6) */
            appearance::floatingFrame(*contRect, active,
                                      { close, zoom != 0, collapse && flavor != kThemeUtilitySideWindow, false,
                                        flavor == kThemeUtilitySideWindow, zoom,
                                        false, true },
                                      nullptr);
            break;
        default:
            goto executor_look;
    }
    if(Rect text = title_text_rect(w, f.ft, f.fl, f.fr, metrics, attributes);
       text.right > text.left && titleProc && !w.side_title)
    {
        SInt16 depth;
        Boolean color;
        portDepth(&depth, &color);
        gray(active ? kBlack : kShadow);
        titleProc(&text, depth, color, titleData);
    }
    return noErr;
executor_look:
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

/* A window's regions in global coordinates: the structure and content its
   definition makes, the update region, the port; the frame's parts as
   Executor's definitions have them (windMgr8.cpp). */
OSStatus Executor::C_GetWindowRegion(WindowRef window, WindowRegionCode inRegionCode, RgnHandle ioWinRgn)
{
    if(!window || !ioWinRgn)
        return paramErr;
    switch(inRegionCode)
    {
        case kWindowStructureRgn:
            ROMlib_window8_definition_regions(window, ioWinRgn, nullptr);
            return noErr;
        case kWindowContentRgn:
            ROMlib_window8_definition_regions(window, nullptr, ioWinRgn);
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
            /* the frame's parts, where Executor's definition has them */
            return ROMlib_window8_part_region(window, inRegionCode, ioWinRgn) ? noErr
                                                                            : errWindowRegionCodeInvalid;
    }
}

/* What CollapseWindow set (windMgr8.cpp) */
Boolean Executor::C_IsWindowCollapsed(WindowRef window)
{
    return window && ROMlib_window8_collapsed(window);
}

Boolean Executor::C_IsWindowCollapsable(WindowRef window)
{
    GUEST<UInt32> features = 0;
    return window && C_GetWindowFeatures(window, &features) == noErr && (features & kWindowCanCollapse);
}

/* A command key's glyph (Menus.h kMenu*Glyph): 8.x's MDEF draws it
   (menu/stdmdef.cpp) from the System's .Keyboard font. */
namespace
{
/* Per item (menu, item): the glyph and modifiers the 8.x calls set. */
struct ItemKeys
{
    SInt16 glyph = 0;
    UInt8 modifiers = 0;
    SInt16 hier_id = 0;      /* SetMenuItemHierarchicalID's; 0: none set */
    bool encoding_set = false;
    TextEncoding encoding = 0;
    SInt16 font_id = 0;      /* SetMenuItemFontID's; 0: the menu's font */
    UInt32 command_id = 0;   /* SetMenuItemCommandID's */
    UInt32 refcon = 0;       /* SetMenuItemRefCon's */
    UInt32 refcon2 = 0;      /* SetMenuItemRefCon2's */
};
std::map<std::pair<uint32_t, int>, ItemKeys> item_keys;
std::set<uint32_t> parent_choosable;

bool valid_item(MenuRef m, int item)
{
    return m && item >= 1 && item <= CountMItems(m);
}
}

int16_t Executor::ROMlib_menu_item_key_glyph(MenuHandle mh, int item)
{
    auto it = item_keys.find({ US_TO_SYN68K(mh), item });
    return it == item_keys.end() ? 0 : it->second.glyph;
}

OSErr Executor::C_SetMenuItemKeyGlyph(MenuRef inMenu, SInt16 inItem, SInt16 inGlyph)
{
    if(!valid_item(inMenu, inItem))
        return paramErr; /* 8.6 */
    item_keys[{ US_TO_SYN68K(inMenu), inItem }].glyph = inGlyph;
    return noErr;
}

OSErr Executor::C_GetMenuItemKeyGlyph(MenuRef inMenu, SInt16 inItem, GUEST<SInt16> *outGlyph)
{
    if(!valid_item(inMenu, inItem) || !outGlyph)
        return paramErr;
    auto it = item_keys.find({ US_TO_SYN68K(inMenu), inItem });
    *outGlyph = it == item_keys.end() ? 0 : it->second.glyph;
    return noErr;
}

OSErr Executor::C_SetMenuItemModifiers(MenuRef inMenu, SInt16 inItem, UInt8 inModifiers)
{
    if(!valid_item(inMenu, inItem))
        return paramErr;
    item_keys[{ US_TO_SYN68K(inMenu), inItem }].modifiers = inModifiers;
    return noErr;
}

OSErr Executor::C_GetMenuItemModifiers(MenuRef inMenu, SInt16 inItem, UInt8 *outModifiers)
{
    if(!valid_item(inMenu, inItem) || !outModifiers)
        return paramErr;
    auto it = item_keys.find({ US_TO_SYN68K(inMenu), inItem });
    *outModifiers = it == item_keys.end() ? 0 : it->second.modifiers;
    return noErr;
}

/* 8.6 keeps the ID apart from the item's command and mark; an item with
   none set answers its mark when its command is hMenuCmd. Executor's
   menus still open a submenu by command and mark only. */
OSErr Executor::C_SetMenuItemHierarchicalID(MenuRef inMenu, SInt16 inItem, SInt16 inHierID)
{
    if(!valid_item(inMenu, inItem))
        return paramErr;
    item_keys[{ US_TO_SYN68K(inMenu), inItem }].hier_id = inHierID;
    return noErr;
}

OSErr Executor::C_GetMenuItemHierarchicalID(MenuRef inMenu, SInt16 inItem, GUEST<SInt16> *outHierID)
{
    if(!valid_item(inMenu, inItem) || !outHierID)
        return paramErr;
    auto it = item_keys.find({ US_TO_SYN68K(inMenu), inItem });
    if(it != item_keys.end() && it->second.hier_id)
    {
        *outHierID = it->second.hier_id;
        return noErr;
    }
    GUEST<CharParameter> cmd, mark;
    GetItemCmd(inMenu, inItem, &cmd);
    GetItemMark(inMenu, inItem, &mark);
    *outHierID = cmd == 0x1B ? (SInt16)mark : 0; /* hMenuCmd */
    return noErr;
}

/* -2 (smCurrentScript) until set */
OSErr Executor::C_SetMenuItemTextEncoding(MenuRef inMenu, SInt16 inItem, TextEncoding inScriptID)
{
    if(!valid_item(inMenu, inItem))
        return paramErr;
    ItemKeys& k = item_keys[{ US_TO_SYN68K(inMenu), inItem }];
    k.encoding = inScriptID;
    k.encoding_set = true;
    return noErr;
}

OSErr Executor::C_GetMenuItemTextEncoding(MenuRef inMenu, SInt16 inItem, GUEST<TextEncoding> *outScriptID)
{
    if(!valid_item(inMenu, inItem) || !outScriptID)
        return paramErr;
    auto it = item_keys.find({ US_TO_SYN68K(inMenu), inItem });
    *outScriptID = it != item_keys.end() && it->second.encoding_set ? it->second.encoding
                                                                    : (TextEncoding)-2;
    return noErr;
}

/* MacPhoenix: an item's own font (8.6's Navigation Services: its
   Shortcuts menu); the MDEF draws the item's text in it. */
OSErr Executor::C_SetMenuItemFontID(MenuRef inMenu, SInt16 inItem, SInt16 inFontID)
{
    if(!valid_item(inMenu, inItem))
        return paramErr;
    item_keys[{ US_TO_SYN68K(inMenu), inItem }].font_id = inFontID;
    return noErr;
}

OSErr Executor::C_GetMenuItemFontID(MenuRef inMenu, SInt16 inItem, GUEST<SInt16> *outFontID)
{
    if(!valid_item(inMenu, inItem) || !outFontID)
        return paramErr;
    *outFontID = Executor::ROMlib_menu_item_font(inMenu, inItem);
    return noErr;
}

int16_t Executor::ROMlib_menu_item_font(MenuHandle mh, int item)
{
    auto it = item_keys.find({ US_TO_SYN68K(mh), item });
    return it == item_keys.end() ? 0 : it->second.font_id;
}

/* An item is enabled when the menu is and its own bit is (items past 31
   follow the menu's bit); item 0 is the menu itself. */
Boolean Executor::C_IsMenuItemEnabled(MenuRef menu, UInt16 item)
{
    if(!menu)
        return false;
    uint32_t flags = (*menu)->enableFlags;
    if(!(flags & 1))
        return false;
    return item == 0 || item > 31 || (flags & (1u << item));
}

/* MenusLib 8.5's EnableItem and DisableItem, the item a UInt16: items
   past 31 have no bit of their own (they follow the menu's). REALbasic's
   runtime calls them without checking its weak imports on 8.5 and later
   (GameBox Classic). */
void Executor::C_EnableMenuItem(MenuRef menu, UInt16 item)
{
    if(item < 32)
        C_EnableItem(menu, item);
}

void Executor::C_DisableMenuItem(MenuRef menu, UInt16 item)
{
    if(item < 32)
        C_DisableItem(menu, item);
}

/* AppearanceLib's per-item command ID: kept (REALbasic keeps its menu
   item objects there). */
OSErr Executor::C_SetMenuItemCommandID(MenuRef menu, SInt16 item, UInt32 commandID)
{
    if(!valid_item(menu, item))
        return paramErr;
    item_keys[{ US_TO_SYN68K(menu), item }].command_id = commandID;
    return noErr;
}

OSErr Executor::C_GetMenuItemCommandID(MenuRef menu, SInt16 item, GUEST<UInt32> *outCommandID)
{
    if(!valid_item(menu, item) || !outCommandID)
        return paramErr;
    auto it = item_keys.find({ US_TO_SYN68K(menu), item });
    *outCommandID = it == item_keys.end() ? 0 : it->second.command_id;
    return noErr;
}

/* AppearanceLib's two per-item reference constants, kept like the
   command ID (Illustrator 8.0 calls them unchecked, weak imports). */
OSErr Executor::C_SetMenuItemRefCon(MenuRef inMenu, SInt16 inItem, UInt32 inRefCon)
{
    if(!valid_item(inMenu, inItem))
        return paramErr;
    item_keys[{ US_TO_SYN68K(inMenu), inItem }].refcon = inRefCon;
    return noErr;
}

OSErr Executor::C_GetMenuItemRefCon(MenuRef inMenu, SInt16 inItem, GUEST<UInt32> *outRefCon)
{
    if(!valid_item(inMenu, inItem) || !outRefCon)
        return paramErr;
    auto it = item_keys.find({ US_TO_SYN68K(inMenu), inItem });
    *outRefCon = it == item_keys.end() ? 0 : it->second.refcon;
    return noErr;
}

OSErr Executor::C_SetMenuItemRefCon2(MenuRef inMenu, SInt16 inItem, UInt32 inRefCon2)
{
    if(!valid_item(inMenu, inItem))
        return paramErr;
    item_keys[{ US_TO_SYN68K(inMenu), inItem }].refcon2 = inRefCon2;
    return noErr;
}

OSErr Executor::C_GetMenuItemRefCon2(MenuRef inMenu, SInt16 inItem, GUEST<UInt32> *outRefCon2)
{
    if(!valid_item(inMenu, inItem) || !outRefCon2)
        return paramErr;
    auto it = item_keys.find({ US_TO_SYN68K(inMenu), inItem });
    *outRefCon2 = it == item_keys.end() ? 0 : it->second.refcon2;
    return noErr;
}

/* AppearanceLib's MenuEvent: MenuKey for a command-key event (MenuKey
   already matches the items' SetMenuItemModifiers); 0 for any other. */
UInt32 Executor::C_MenuEvent(const EventRecord *inEvent)
{
    if(!inEvent || (inEvent->what != keyDown && inEvent->what != autoKey)
       || !(inEvent->modifiers & cmdKey))
        return 0;
    return C_MenuKey(inEvent->message & charCodeMask);
}

/* MenusLib 8.5's HideMenuBar and ShowMenuBar: the menu bar's height goes
   to 0 and back, as programs that take the whole screen read it. Only
   that: the desktop region keeps the bar's strip, which isn't redrawn
   until something is drawn over it. */
static INTEGER hidden_mbar_height;

void Executor::C_HideMenuBar()
{
    if(LM(MBarHeight) == 0)
        return;
    hidden_mbar_height = LM(MBarHeight);
    LM(MBarHeight) = 0;
}

void Executor::C_ShowMenuBar()
{
    if(LM(MBarHeight) != 0 || hidden_mbar_height == 0)
        return;
    LM(MBarHeight) = hidden_mbar_height;
    hidden_mbar_height = 0;
    C_DrawMenuBar();
}

OSStatus Executor::C_SetSubmenuParentChoosable(MenuRef menu, Boolean inChoosable)
{
    if(!menu)
        return paramErr;
    if(inChoosable)
        parent_choosable.insert(US_TO_SYN68K(menu));
    else
        parent_choosable.erase(US_TO_SYN68K(menu));
    return noErr;
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

/* AppearanceLib's private font call the 8.6 Appearance control panel
   names its small system font popup with (its two arguments from the
   call sites: the script, -1, and the name). */
OSStatus Executor::C_GetSmallSystemFont(ScriptCode script, StringPtr outFontName)
{
    return C_GetThemeFont(kThemeSmallSystemFont, script, outFontName, nullptr, nullptr);
}

/* The same for the system font: AppearanceLib's private call the 8.6
   Appearance control panel names its large system font popup with (8.6:
   "Charcoal"). */
OSStatus Executor::C_GetSystemFont(ScriptCode script, StringPtr outFontName)
{
    return C_GetThemeFont(kThemeSystemFont, script, outFontName, nullptr, nullptr);
}

namespace
{
/* A theme's collection as 8.6 hands it to an IterateThemes procedure: the
   'scen' resource's items and, in tag order with them, 'thme' with the
   theme file's name (tests/guest/difftest desk). nil if it can't be
   read. */
Collection with_file_name(Collection flat, Handle scen, ConstStringPtr file)
{
    if(UnflattenCollectionFromHdl(flat, scen) != noErr)
        return nullptr;
    Collection c = NewCollection();
    if(!c)
        return nullptr;
    const OSType kFileTag = "thme"_4;
    bool added = false;
    auto add_name = [&]() {
        AddCollectionItem(c, kFileTag, 0, file[0] + 1, file);
        added = true;
    };
    for(LONGINT i = 1, n = CountCollectionItems(flat); i <= n; i++)
    {
        GUEST<CollectionTag> tag;
        GUEST<LONGINT> id, size, attributes;
        if(GetIndexedCollectionItemInfo(flat, i, &tag, &id, &size, &attributes) != noErr)
            continue;
        if(!added && (uint32_t)(OSType)tag >= kFileTag)
            add_name();
        if((OSType)tag == kFileTag)
            continue;
        GUEST<LONGINT> got = size;
        Ptr buf = NewPtr(std::max<LONGINT>(size, 1));
        if(!buf)
            continue;
        if(GetIndexedCollectionItem(flat, i, &got, buf) == noErr)
            AddCollectionItem(c, tag, id, got, buf);
        DisposePtr(buf);
    }
    if(!added)
        add_name();
    return c;
}
}

/* Each theme ('scen' resource, a flattened collection) of each file in
   the System Folder's Appearance:Theme Files, in resource order, until
   the procedure answers false; noErr without the folder. (8.6 on the G3:
   "Apple platinum" 2000 to 2012, tests/guest/difftest desk.) */
OSStatus Executor::C_IterateThemes(ThemeIteratorUPP inProc, void *inUserData)
{
    if(!inProc)
        return paramErr;
    GUEST<INTEGER> vref;
    GUEST<LONGINT> sysdir;
    if(FindFolder(kOnSystemDisk, kSystemFolderType, false, &vref, &sysdir) != noErr)
        return noErr;
    LONGINT dir = sysdir;
    StringPtr file = (StringPtr)NewPtr(sizeof(Str255));
    if(!file)
        return memFullErr;
    for(const char *sub : { "Appearance", "Theme Files" })
    {
        CInfoPBRec pb = {};
        file[0] = strlen(sub);
        memcpy(file + 1, sub, file[0]);
        pb.hFileInfo.ioNamePtr = file;
        pb.hFileInfo.ioVRefNum = vref;
        pb.hFileInfo.ioDirID = dir;
        if(PBGetCatInfo(&pb, false) != noErr || !(pb.hFileInfo.ioFlAttrib & 0x10 /* a folder */))
        {
            DisposePtr((Ptr)file);
            return noErr;
        }
        dir = pb.dirInfo.ioDrDirID;
    }
    INTEGER saved = CurResFile();
    bool go_on = true;
    for(int i = 1; go_on; i++)
    {
        CInfoPBRec pb = {};
        pb.hFileInfo.ioNamePtr = file;
        pb.hFileInfo.ioVRefNum = vref;
        pb.hFileInfo.ioDirID = dir;
        pb.hFileInfo.ioFDirIndex = i;
        if(PBGetCatInfo(&pb, false) != noErr)
            break;
        if(pb.hFileInfo.ioFlAttrib & 0x10 /* a folder */)
            continue;
        INTEGER ref = HOpenResFile(vref, dir, file, fsRdPerm);
        if(ref == -1)
            continue;
        UseResFile(ref);
        INTEGER n = Count1Resources("scen"_4);
        for(INTEGER r = 1; r <= n && go_on; r++)
        {
            UseResFile(ref);
            Handle h = Get1IndResource("scen"_4, r);
            if(!h)
                continue;
            GUEST<INTEGER> id;
            GUEST<ResType> type;
            Str255 rname;
            GetResInfo(h, &id, &type, rname);
            Collection flat = NewCollection();
            Collection c = flat ? with_file_name(flat, h, file) : nullptr;
            if(c)
                go_on = inProc(file, id, c, inUserData);
            if(c)
                DisposeCollection(c);
            if(flat)
                DisposeCollection(flat);
            ReleaseResource(h);
        }
        CloseResFile(ref);
    }
    UseResFile(saved);
    DisposePtr((Ptr)file);
    return noErr;
}

/* Executor plays no theme sounds. */
Boolean Executor::C_AreThemeSoundsEnabled()
{
    return false;
}

/* The current theme's settings (Appearance.h kTheme*Tag) into a
   collection, as far as Executor has them: name, highlight colour, the
   fonts, the desktop pattern, sounds and collapse off. */
OSStatus Executor::C_GetTheme(Collection ioCollection)
{
    if(!ioCollection)
        return paramErr;
    auto add = [&](OSType tag, const void *data, int32_t size) {
        return C_AddCollectionItem(ioCollection, tag, 0, size, (Ptr)data);
    };
    auto add_str = [&](OSType tag, const unsigned char *str) {
        return add(tag, str, str[0] + 1);
    };
    OSStatus err = add_str(kThemeNameTag, (const unsigned char *)"\016Mac OS Default");
    RGBColor hilite = LM(HiliteRGB);
    if(err == noErr)
        err = add(kThemeHighlightColorTag, &hilite, sizeof hilite);
    Boolean off = false;
    if(err == noErr)
        err = add(kThemeSoundsEnabledTag, &off, sizeof off);
    if(err == noErr)
        err = add(kThemeDblClickCollapseTag, &off, sizeof off);
    if(err == noErr)
        err = add_str(kThemeSystemFontTag, (const unsigned char *)theme_fonts[kThemeSystemFont].name);
    if(err == noErr)
        err = add_str(kThemeSmallSystemFontTag, (const unsigned char *)theme_fonts[kThemeSmallSystemFont].name);
    if(err == noErr)
        err = add_str(kThemeViewsFontTag, (const unsigned char *)theme_fonts[kThemeViewsFont].name);
    GUEST<SInt16> views_size = theme_fonts[kThemeViewsFont].size;
    if(err == noErr)
        err = add(kThemeViewsFontSizeTag, &views_size, sizeof views_size);
    if(err == noErr)
        if(Handle ppat = GetResource("ppat"_4, deskPatID))
        {
            LoadResource(ppat);
            HLockGuard guard(ppat);
            err = add(kThemeDesktopPatternTag, *ppat, GetHandleSize(ppat));
        }
    return err;
}

/* AppearanceLib's private GetScenario: the Appearance control panel
   passes one collection and reads theme items back ('patt', 'patn', ...):
   the current theme's, as GetTheme gives them. */
OSStatus Executor::C_GetScenario(Collection ioCollection)
{
    return C_GetTheme(ioCollection);
}

/* Theme files by name: Executor has none (the control panel then lists
   no variants). Each output is set, as the panel trusts them: with them
   left alone it took stale stack for a variant list and disposed of one
   of its own dialog's controls. */
OSStatus Executor::C_GetCurrentTheme(ConstStringPtr themeName, Ptr outThemeFile)
{
    (void)themeName, (void)outThemeFile;
    return fnfErr;
}

OSStatus Executor::C_GetThemeVariantList(ConstStringPtr themeName, GUEST<Handle> *outList)
{
    (void)themeName;
    if(outList)
        *outList = nullptr;
    return fnfErr;
}

OSStatus Executor::C_CountThemeVariants(Handle list, GUEST<SInt16> *outCount)
{
    if(outCount)
        *outCount = 0;
    return list ? noErr : paramErr;
}

OSStatus Executor::C_GetIndThemeVariant(Handle list, SInt16 index, StringPtr outName)
{
    (void)list, (void)index;
    if(outName)
        outName[0] = 0;
    return paramErr;
}

/* One long (tests/guest/difftest appr: 8.6 writes 4 bytes; the control
   panel reads the next long as well, its own). */
OSStatus Executor::C_GetThemeSoundMask(GUEST<UInt32> *outMask)
{
    if(outMask)
        *outMask = 0; /* no sounds */
    return noErr;
}

Boolean Executor::C_IsThemeCreditDialogAvailable()
{
    return false;
}

/* ── Menu items: Menus.h's 8.x calls ─────────────────────────────────── */

OSStatus Executor::C_AppendMenuItemText(MenuRef menu, ConstStringPtr inString)
{
    if(!menu || !inString)
        return paramErr;
    AppendMenu(menu, (StringPtr) "\001 ");
    SetMenuItemText(menu, CountMItems(menu), inString);
    return LM(MemErr);
}

OSStatus Executor::C_InsertMenuItemText(MenuRef menu, ConstStringPtr inString, INTEGER afterItem)
{
    if(!menu || !inString)
        return paramErr;
    INTEGER count = CountMItems(menu);
    if(afterItem < 0 || afterItem > count)
        afterItem = count;
    InsertMenuItem(menu, (StringPtr) "\001 ", afterItem);
    SetMenuItemText(menu, afterItem + 1, inString);
    return LM(MemErr);
}

/* MenusLib's menu bar clock: kept, not drawn. */
namespace
{
bool clock_enabled;
uint8_t clock_config[20];
}

Boolean Executor::C_IsMenuBarClockEnabled()
{
    return clock_enabled;
}

OSStatus Executor::C_EnableMenuBarClock()
{
    clock_enabled = true;
    return noErr;
}

OSStatus Executor::C_DisableMenuBarClock()
{
    clock_enabled = false;
    return noErr;
}

OSStatus Executor::C_RefreshMenuBarClock()
{
    return noErr;
}

/* {version word, 20 bytes}: version 0 is the only one. */
OSStatus Executor::C_GetMenuBarClockConfig(Ptr ioConfig)
{
    if(!ioConfig || *(GUEST<int16_t> *)ioConfig != 0)
        return paramErr;
    memcpy(ioConfig + 2, clock_config, sizeof clock_config);
    return noErr;
}

OSStatus Executor::C_SetMenuBarClockConfig(Ptr inConfig)
{
    if(!inConfig || *(GUEST<int16_t> *)inConfig != 0)
        return paramErr;
    memcpy(clock_config, inConfig + 2, sizeof clock_config);
    return noErr;
}

namespace
{
/* Item icons given by handle: kept here (the menu definition draws the
   'ICON'/'cicn' an item's icon number names, not these). */
std::map<std::pair<uint32_t, int>, std::pair<UInt8, uint32_t>> item_icons;
}

OSErr Executor::C_SetMenuItemIconHandle(MenuRef inMenu, SInt16 inItem, UInt8 inIconType,
                                        Handle inIconHandle)
{
    if(!inMenu || inItem < 1 || inItem > CountMItems(inMenu))
        return paramErr;
    auto key = std::make_pair(US_TO_SYN68K(inMenu), (int)inItem);
    if(inIconType == kMenuNoIcon || !inIconHandle)
        item_icons.erase(key);
    else
        item_icons[key] = { inIconType, US_TO_SYN68K(inIconHandle) };
    return noErr;
}

OSErr Executor::C_GetMenuItemIconHandle(MenuRef inMenu, SInt16 inItem, UInt8 *outIconType,
                                        GUEST<Handle> *outIconHandle)
{
    if(!inMenu || inItem < 1 || inItem > CountMItems(inMenu))
        return paramErr;
    auto it = item_icons.find(std::make_pair(US_TO_SYN68K(inMenu), (int)inItem));
    if(outIconType)
        *outIconType = it == item_icons.end() ? kMenuNoIcon : it->second.first;
    if(outIconHandle)
        *outIconHandle = it == item_icons.end() ? nullptr : (Handle)SYN68K_TO_US(it->second.second);
    return noErr;
}

/* ── The desktop: WindowsLib's private desktop calls (8.5) ───────────── */

/* The signatures from the 8.6 Appearance control panel's call sites: a
   copy the caller disposes (DisposePixPat), and the pattern's name
   (either may be nil). */
OSStatus Executor::C_GetDesktopPixPat(GUEST<PixPatHandle> *outPixPat, StringPtr outName)
{
    if(outName)
        outName[0] = 0;
    if(!outPixPat)
        return noErr;
    *outPixPat = nullptr;
    PixPatHandle desk = LM(DeskCPat);
    if(!desk)
        desk = GetPixPat(deskPatID);
    if(!desk)
        return resNotFound;
    PixPatHandle copy = NewPixPat();
    if(!copy)
        return memFullErr;
    CopyPixPat(desk, copy);
    *outPixPat = copy;
    return noErr;
}

/* The desktop pattern as a 'ppat' resource's data, a handle the caller
   owns. */
OSStatus Executor::C_GetDesktopPattern(GUEST<Handle> *outPattern, StringPtr outName)
{
    if(outName)
        outName[0] = 0;
    if(!outPattern)
        return noErr;
    *outPattern = nullptr;
    Handle ppat = GetResource("ppat"_4, deskPatID);
    if(!ppat)
        return resNotFound;
    LoadResource(ppat);
    GUEST<Handle> copy = ppat;
    OSErr err = HandToHand(&copy);
    if(err == noErr)
        *outPattern = copy;
    return err;
}

/* No desktop picture on any screen: the error the Appearance control
   panel takes for "none" (it then records 'nopf'). */
OSStatus Executor::C_GetDesktopPictureForDevice(GDHandle device, FSSpec *outFile, GUEST<UInt32> *outPlacement)
{
    (void)device, (void)outFile, (void)outPlacement;
    return errUnrecognizedWindowClass; /* -5605 */
}

/* A picture file read for the desktop (a file dropped on the control
   panel): Executor reads none. The second argument isn't known for sure,
   so it is left alone. */
OSStatus Executor::C_GetPictureFromFile(FSSpec *file, Ptr outPicture)
{
    (void)file, (void)outPicture;
    return fnfErr;
}

namespace
{
UInt32 double_click_action = 0;
}

/* What double-clicking a title bar does (1: collapse the window): kept for
   the Appearance control panel's Options tab; Executor's windows don't
   collapse on a double click either way. */
UInt32 Executor::C_GetWindowDoubleClickAction()
{
    return double_click_action;
}

OSStatus Executor::C_SetWindowDoubleClickAction(UInt32 action)
{
    double_click_action = action;
    return noErr;
}

/* ── Monitor desktop lists: WindowsLib's private desktop picture calls ─

   The 8.6 Appearance control panel's Desktop tab edits a copy of each
   screen's desktop settings (a "monitor desktop list"), draws its preview
   from it and makes it current when the user sets the desktop. The
   signatures come from the panel's call sites (tools/macdecode/pef.py
   --dis), the answers and the record's layout from the G3's 8.6
   (tests/guest/difftest desk): 372 bytes a record, one record a screen,
   numbered from 1; an index out of range is paramErr. Executor keeps the
   settings for the session and draws no picture on its desktop (the
   pattern only). */

namespace
{
struct MonitorDesktopRecord
{
    GUEST_STRUCT;
    GUEST<AliasHandle> alias;  /* the picture file, nil for none */
    GUEST<OSType> fileType;    /* its type */
    GUEST<Handle> picture;     /* the picture read (nil: none read) */
    GUEST<UInt32> alignment;   /* 1 to 4 */
    GUEST<UInt32> automatic;   /* alignment 5: "position automatically" */
    GUEST<UInt32> displayID;
    GUEST<GDHandle> device;
    GUEST<UInt32> unknown1c[2];
    GUEST<Rect> pictureFrame;
    GUEST<uint8_t> reserved[0x174 - 0x2c];
};
static_assert(sizeof(MonitorDesktopRecord) == 0x174);

struct MonitorDesktopList
{
    GUEST_STRUCT;
    GUEST<UInt32> count;
    MonitorDesktopRecord records[1];
};

enum
{
    kMonitorDesktopAutomatic = 5
};

/* The current settings, one record a screen (made from the screens on
   first use). */
std::vector<MonitorDesktopRecord> &current_desktops()
{
    static std::vector<MonitorDesktopRecord> current;
    if(current.empty())
    {
        for(GDHandle gd = GetDeviceList(); gd; gd = GetNextDevice(gd))
        {
            if(!TestDeviceAttribute(gd, screenDevice) || !TestDeviceAttribute(gd, screenActive))
                continue;
            MonitorDesktopRecord r;
            memset(&r, 0, sizeof r);
            GUEST<uint32_t> id;
            if(DMGetDisplayIDByGDevice(gd, &id, true) == noErr)
                r.displayID = id;
            r.device = gd;
            current.push_back(r);
        }
    }
    return current;
}

/* A copy of a record's file alias, owned by whoever holds the copy. */
void own_alias(MonitorDesktopRecord &r)
{
    if(!r.alias)
        return;
    GUEST<Handle> h = (Handle)r.alias;
    if(HandToHand(&h) == noErr)
        r.alias = (AliasHandle)(Handle)h;
    else
        r.alias = nullptr;
}

MonitorDesktopList *new_list(const std::vector<MonitorDesktopRecord> &recs)
{
    TheZoneGuard guard(LM(SysZone));
    auto *list = (MonitorDesktopList *)NewPtrClear(
        offsetof(MonitorDesktopList, records) + std::max<size_t>(recs.size(), 1) * sizeof(MonitorDesktopRecord));
    if(!list)
        return nullptr;
    list->count = recs.size();
    for(size_t i = 0; i < recs.size(); i++)
    {
        list->records[i] = recs[i];
        own_alias(list->records[i]);
    }
    return list;
}

MonitorDesktopRecord *record_of(Ptr list, UInt32 index)
{
    auto *l = (MonitorDesktopList *)list;
    if(!l || index < 1 || index > l->count)
        return nullptr;
    return &l->records[index - 1];
}

void clear_picture(MonitorDesktopRecord &r)
{
    if(r.alias)
        DisposeHandle((Handle)r.alias);
    if(r.picture)
        DisposeHandle(r.picture);
    r.alias = nullptr;
    r.picture = nullptr;
    r.fileType = 0;
    r.pictureFrame = Rect{};
}

void set_alignment(MonitorDesktopRecord &r, UInt32 alignment)
{
    r.automatic = alignment == kMonitorDesktopAutomatic;
    r.alignment = alignment == kMonitorDesktopAutomatic ? 0 : alignment;
}
}

OSStatus Executor::C_GetCurrentMonitorDesktopList(GUEST<Ptr> *outList)
{
    if(!outList)
        return paramErr;
    MonitorDesktopList *list = new_list(current_desktops());
    *outList = (Ptr)list;
    return list ? noErr : memFullErr;
}

/* A new list for the screens there are now (the caller disposes of the
   old one): a screen keeps the old list's picture and its alignment, the
   rest start over (on the G3 an alignment set without a picture is
   gone). */
OSStatus Executor::C_RefreshMonitorDesktopList(Ptr list, GUEST<Ptr> *outList)
{
    auto *old = (MonitorDesktopList *)list;
    if(!old || !outList)
        return paramErr;
    std::vector<MonitorDesktopRecord> recs = current_desktops();
    for(auto &r : recs)
    {
        r.alias = nullptr;
        r.picture = nullptr;
        r.fileType = 0;
        r.alignment = 0;
        r.automatic = 0;
        r.pictureFrame = Rect{};
        for(UInt32 i = 0; i < old->count; i++)
        {
            const MonitorDesktopRecord &o = old->records[i];
            if(o.displayID == r.displayID && o.alias)
            {
                r.alias = o.alias;
                r.fileType = o.fileType;
                r.alignment = o.alignment;
                r.automatic = o.automatic;
            }
        }
    }
    MonitorDesktopList *fresh = new_list(recs);
    *outList = (Ptr)fresh;
    return fresh ? noErr : memFullErr;
}

OSStatus Executor::C_DisposeMonitorDesktopList(Ptr list)
{
    auto *l = (MonitorDesktopList *)list;
    if(!l)
        return paramErr;
    for(UInt32 i = 0; i < l->count; i++)
        clear_picture(l->records[i]);
    DisposePtr(list);
    return noErr;
}

/* The list's settings become the screens' (the panel then draws the
   desktop). */
OSStatus Executor::C_SetCurrentMonitorDesktopList(Ptr list)
{
    auto *l = (MonitorDesktopList *)list;
    if(!l)
        return paramErr;
    auto &current = current_desktops();
    for(auto &r : current)
        clear_picture(r);
    current.clear();
    for(UInt32 i = 0; i < l->count; i++)
    {
        current.push_back(l->records[i]);
        current.back().picture = nullptr;
        own_alias(current.back());
    }
    return noErr;
}

OSStatus Executor::C_CountMonitorDesktopRecordsInList(Ptr list, GUEST<UInt32> *outCount)
{
    auto *l = (MonitorDesktopList *)list;
    if(!l || !outCount)
        return paramErr;
    *outCount = l->count;
    return noErr;
}

OSStatus Executor::C_GetMonitorDesktopRecordFromList(Ptr list, UInt32 index, Ptr outRecord)
{
    MonitorDesktopRecord *r = record_of(list, index);
    if(!r || !outRecord)
        return paramErr;
    memcpy(outRecord, r, sizeof *r);
    return noErr;
}

/* 1 to 4, or 5 (kMonitorDesktopAutomatic); 0 before any was set. */
OSStatus Executor::C_GetMonitorDesktopAlignment(Ptr list, UInt32 index, GUEST<UInt32> *outAlignment)
{
    MonitorDesktopRecord *r = record_of(list, index);
    if(!r || !outAlignment)
        return paramErr;
    *outAlignment = r->automatic ? (UInt32)kMonitorDesktopAutomatic : (UInt32)r->alignment;
    return noErr;
}

OSStatus Executor::C_SetMonitorDesktopAlignment(Ptr list, UInt32 index, UInt32 alignment)
{
    MonitorDesktopRecord *r = record_of(list, index);
    if(!r || alignment < 1 || alignment > kMonitorDesktopAutomatic)
        return paramErr;
    set_alignment(*r, alignment);
    return noErr;
}

OSStatus Executor::C_MonitorDesktopHasPicture(Ptr list, UInt32 index, Boolean *outHasPicture)
{
    MonitorDesktopRecord *r = record_of(list, index);
    if(!r || !outHasPicture)
        return paramErr;
    *outHasPicture = r->alias != nullptr;
    return noErr;
}

/* A picture file for the screen (nil: none) and its alignment, 1 to 5. */
OSStatus Executor::C_SetMonitorDesktopToFile(Ptr list, UInt32 index, FSSpec *file, UInt32 alignment)
{
    MonitorDesktopRecord *r = record_of(list, index);
    if(!r || alignment < 1 || alignment > kMonitorDesktopAutomatic)
        return paramErr;
    GUEST<AliasHandle> alias = nullptr;
    FInfo info;
    if(file)
    {
        OSErr err = FSpGetFInfo(file, &info);
        if(err == noErr)
            err = NewAliasMinimal(file, &alias);
        if(err != noErr)
            return err;
    }
    clear_picture(*r);
    r->alias = alias;
    r->fileType = file ? (OSType)info.fdType : 0;
    set_alignment(*r, alignment);
    return noErr;
}

/* The picture settings of one list's record into another's. */
OSStatus Executor::C_CopyMonitorDesktopFile(Ptr fromList, UInt32 fromIndex, Ptr toList, UInt32 toIndex)
{
    MonitorDesktopRecord *from = record_of(fromList, fromIndex);
    MonitorDesktopRecord *to = record_of(toList, toIndex);
    if(!from || !to)
        return paramErr;
    if(from == to)
        return noErr;
    MonitorDesktopRecord copy = *from;
    copy.picture = nullptr;
    own_alias(copy);
    clear_picture(*to);
    to->alias = copy.alias;
    to->fileType = copy.fileType;
    to->alignment = copy.alignment;
    to->automatic = copy.automatic;
    return noErr;
}

/* A picture takes time to draw (the panel draws a placeholder first). */
OSStatus Executor::C_DrawMonitorDesktopFromListWillTakeTime(Ptr list, UInt32 index, GrafPtr port,
                                                             const Rect *bounds, Boolean *outWillTakeTime)
{
    (void)port, (void)bounds;
    MonitorDesktopRecord *r = record_of(list, index);
    if(!r || !outWillTakeTime)
        return paramErr;
    *outWillTakeTime = r->alias != nullptr;
    return noErr;
}

/* A screen's desktop as the preview shows it: the region filled with the
   pattern given (nothing without one; as on the G3, no picture drawn
   here). */
OSStatus Executor::C_DrawMonitorDesktopFromList(Ptr list, UInt32 index, GrafPtr port, const Rect *bounds,
                                                RgnHandle region, PixPatHandle pattern, Point origin,
                                                Boolean flag)
{
    (void)bounds, (void)origin, (void)flag;
    if(!record_of(list, index))
        return paramErr;
    if(!port || !region || !pattern)
        return noErr;
    ThePortGuard guard(port);
    FillCRgn(region, pattern);
    return noErr;
}

/* Files the panel takes for a picture: Executor draws PICT files only. */
OSStatus Executor::C_FileContainsPossibleDesktopPicture(FSSpec *file, Boolean *outIsPicture)
{
    if(!file || !outIsPicture)
        return paramErr;
    FInfo info;
    OSErr err = FSpGetFInfo(file, &info);
    if(err != noErr)
        return err;
    *outIsPicture = info.fdType == "PICT"_4;
    return noErr;
}

/* The desktop pattern from a 'ppat' resource's data (the caller keeps
   its handle) and its name. */
OSStatus Executor::C_SetDesktopPattern(Handle ppat, ConstStringPtr name)
{
    (void)name;
    PixPatHandle pp;
    {
        TheZoneGuard guard(LM(SysZone));
        pp = ROMlib_pixpat_from_ppat(ppat);
    }
    if(!pp)
        return paramErr;
    PixPatHandle old = LM(DeskCPat);
    LM(DeskCPat) = pp;
    USE_DESKCPAT_VAR |= USE_DESKCPAT_BIT;
    if(PIXPAT_TYPE(pp) == pixpat_old_style_pattern)
        LM(DeskPattern) = PIXPAT_1DATA(pp);
    if(old && old != pp)
        DisposePixPat(old);
    return noErr;
}

/* A screen's picture file (nil: none) and placement: Executor draws no
   picture, so this only forgets one. */
OSStatus Executor::C_SetDesktopPictureForDevice(GDHandle device, FSSpec *file, UInt32 placement)
{
    (void)device, (void)placement;
    return file ? (OSStatus)paramErr : (OSStatus)noErr;
}

/* The desktop drawn again (after SetDesktopPattern). */
void Executor::C_DrawDesktop()
{
    PaintOne(nullptr, LM(GrayRgn));
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
    /* The background as it was, not the port's handle: the port disposes
       of its old pattern when it gets a new one, and a state restored
       after that drew with freed memory (the 8.6 control panels' titles
       over dots: tests/guest/difftest appr). A colour pattern ('ppat') as
       a copy the state owns until a restore hands it to the port. */
    GUEST<PixPatHandle> back_pixpat;
    Pattern back_pat; /* the old-style pattern otherwise */
    GUEST<INTEGER> font, size, mode;
    Style face;
};

PixPatHandle copy_pixpat(PixPatHandle src)
{
    PixPatHandle copy = NewPixPat();
    if(copy)
        CopyPixPat(src, copy);
    return copy;
}
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
    {
        PixPatHandle bk = CPORT_BK_PIXPAT(port);
        if(bk && PIXPAT_TYPE(bk) != pixpat_type_orig)
            s->back_pixpat = copy_pixpat(bk);
        else if(bk)
            s->back_pat = PIXPAT_1DATA(bk);
        else
            s->back_pat = qdGlobals().white;
    }
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
    if(CGrafPort_p(port) && s->back_pixpat)
    {
        /* the copy to the port: the state's own when it goes now */
        PixPatHandle bk = s->back_pixpat;
        if(inDisposeNow)
            s->back_pixpat = nullptr;
        else
            bk = copy_pixpat(bk);
        if(bk)
            BackPixPat(bk);
    }
    else
        BackPat(&s->back_pat);
    RGBBackColor(&s->back);
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
    auto *s = (SavedDrawingState *)inState;
    if(s->back_pixpat)
        DisposePixPat(s->back_pixpat);
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
