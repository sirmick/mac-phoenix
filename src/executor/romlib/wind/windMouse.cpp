/* Copyright 1986, 1989, 1990, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in WindowMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <WindowMgr.h>
#include <EventMgr.h>
#include <OSEvent.h>
#include <MemoryMgr.h>
#include <MenuMgr.h>

#include <quickdraw/cquick.h>
#include <wind/wind.h>
#include <rsys/process.h>
#include <menu/menu.h>

using namespace Executor;

/* WINDOW_ZOOMED returns true if w is currently in stdState (big) */
#define WINDOW_ZOOMED(w) (ROMlib_window_zoomed(w))

INTEGER Executor::C_FindWindow(Point p, GUEST<WindowPtr> *wpp)
{
    WindowPeek wp;
    LONGINT pointaslong, val;
    INTEGER retval;

    pointaslong = ((LONGINT)p.v << 16) | (unsigned short)p.h;

    *wpp = 0;
    if(MBDFCALL(mbHit, 0, pointaslong) != -1)
        return inMenuBar;
    /* MacPhoenix: with the root layer current (Layer Manager), every
       layer's windows, front first. */
    if(ROMlib_root_layer_current())
        wp = ROMlib_layers_window_at(p);
    else if(ROMlib_layers_foreign_current())
        wp = ROMlib_layers_foreign_window_at(p);
    else
        for(wp = LM(WindowList); wp; wp = WINDOW_NEXT_WINDOW(wp))
            if(WINDOW_VISIBLE(wp) && PtInRgn(p, WINDOW_STRUCT_REGION(wp)))
                break;
    if(wp)
    {
        *wpp = (WindowPtr)wp;
        if(WINDOW_KIND(wp) < 0)
            return inSysWindow;
        val = WINDCALL((WindowPtr)wp, wHit, pointaslong);
        if(val == wNoHit)
            retval = LM(DeskHook) ? inSysWindow : inDesk;
        else
            retval = val + 2; /* datadesk showed us that this is how it's
                                 done */
        return retval;
    }
    return inDesk;
}

static Boolean xTrackBox(WindowPtr wp, Point pt, INTEGER part) /* IMIV-50 */
{
    Boolean inpart = true, inp;
    EventRecord ev;

    ThePortGuard guard(wmgr_port);

    SetClip(LM(GrayRgn));

    WINDCALL(wp, wDraw, part);
    while(!GetOSEvent(mUpMask, &ev))
    {
        Point evwhere = ev.where.get();
        CALLDRAGHOOK();
        if(pt.h != evwhere.h || pt.v != evwhere.v)
        {
            pt.h = evwhere.h;
            pt.v = evwhere.v;
            inp = (WINDCALL(wp, wHit, ((LONGINT)pt.v << 16) | (unsigned short)pt.h) == part);
            if(inpart != inp)
            {
                WINDCALL(wp, wDraw, part);
                inpart = inp;
            }
        }
    }

    return inpart;
}

Boolean Executor::C_TrackBox(WindowPtr wp, Point pt, INTEGER part) /* IMIV-50 */
{
    if(part)
        part -= 2;
    return xTrackBox(wp, pt, part);
}

Boolean Executor::C_TrackGoAway(WindowPtr w, Point p)
{
    return xTrackBox(w, p, wInGoAway);
}

/* MacPhoenix: as a 7.6.1 Quadra's (ROM $18F36), which zooms whatever
   state the window looks to be in: the port moves so that portRect's
   top-left lands on the state rectangle's (userState for inZoomIn, else
   stdState), and portRect's bottom-right follows from its size; portRect's
   top-left stays as it is. Color Picker 2.0's More Choices sets userState
   to the dialog widened leftwards and portRect.left to -103 (where its
   list goes), then zooms in; the window grows to the left and doesn't
   move. Executor's checked the state first (none here: the dialog was in
   neither) and reset portRect to (0,0). */
void Executor::C_ZoomWindow(WindowPtr wp, INTEGER part,
                            Boolean front) /* IMIV-50 */
{
    if(!wp || !WINDOW_DATA(wp))
        return;
    WStateData *ws = *(GUEST<WStateData *> *)WINDOW_DATA(wp);

    /* Apple's WDEF records userState at wCalcRgns unless the window is in
       its standard state; Executor's records it here. */
    if(part == inZoomOut && !WINDOW_ZOOMED((WindowPeek)wp))
    {
        ws->userState = PORT_RECT(wp);
        OffsetRect(&ws->userState, -PORT_BOUNDS(wp).left,
                   -PORT_BOUNDS(wp).top);
    }
    Rect state = part == inZoomIn ? ws->userState : ws->stdState;

    RgnHandle behind = NewRgn();
    CopyRgn(WINDOW_STRUCT_REGION(wp), behind);

    INTEGER dh = state.left - (PORT_RECT(wp).left - PORT_BOUNDS(wp).left);
    INTEGER dv = state.top - (PORT_RECT(wp).top - PORT_BOUNDS(wp).top);
    OffsetRect(&PORT_BOUNDS(wp), -dh, -dv);
    PORT_RECT(wp).right = PORT_RECT(wp).left + RECT_WIDTH(&state);
    PORT_RECT(wp).bottom = PORT_RECT(wp).top + RECT_HEIGHT(&state);

    WINDCALL(wp, wCalcRgns, 0);
    UnionRgn(behind, WINDOW_STRUCT_REGION(wp), behind);

    CalcVisBehind((WindowPeek)wp, behind);
    PaintBehind(WINDOW_NEXT_WINDOW(wp), behind);

    {
        ThePortGuard guard(wmgr_port);
        SetClip(WINDOW_STRUCT_REGION(wp));
        ClipAbove((WindowPeek)wp);
        WINDCALL((WindowPtr)wp, wDraw, 0);
        EraseRgn(WINDOW_CONT_REGION(wp));
        CopyRgn(WINDOW_CONT_REGION(wp), WINDOW_UPDATE_REGION(wp));
    }

    DisposeRgn(behind);
    if(front)
        SelectWindow(wp);
    ROMlib_rootless_update();
}
