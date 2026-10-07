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
#include <menu/menu.h>

using namespace Executor;

#if !defined(No_STEF_zoommods)
/* WINDOW_ZOOMED returns true if w is currently in stdState (big) */

#define WINDOW_ZOOMED(w) (ROMlib_window_zoomed(w))

#endif

INTEGER Executor::C_FindWindow(Point p, GUEST<WindowPtr> *wpp)
{
    WindowPeek wp;
    LONGINT pointaslong, val;
    INTEGER retval;

    pointaslong = ((LONGINT)p.v << 16) | (unsigned short)p.h;

    *wpp = 0;
    if(MBDFCALL(mbHit, 0, pointaslong) != -1)
        return inMenuBar;
    for(wp = LM(WindowList); wp; wp = WINDOW_NEXT_WINDOW(wp))
    {
        if(WINDOW_VISIBLE(wp) && PtInRgn(p, WINDOW_STRUCT_REGION(wp)))
        {
            *wpp = (WindowPtr)wp;
            if(WINDOW_KIND(wp) < 0)
            {
                retval = inSysWindow;
                goto DONE;
            }
            val = WINDCALL((WindowPtr)wp, wHit, pointaslong);
            if(val == wNoHit)
                retval = LM(DeskHook) ? inSysWindow : inDesk;
            else
                retval = val + 2; /* datadesk showed us that this is how it's
				   done */
            goto DONE;
        }
    }
    retval = inDesk;
DONE:
    return retval;
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

void Executor::C_ZoomWindow(WindowPtr wp, INTEGER part,
                            Boolean front) /* IMIV-50 */
{
    RgnHandle behind;
#if !defined(No_STEF_zoommods)
    Boolean instdstate;
    Rect *u;
#if !defined(THEPORTNEEDNTBEWMGRPORT)
    GrafPtr gp;
#endif /* THEPORTNEEDNTBEWMGRPORT */

    instdstate = WINDOW_ZOOMED((WindowPeek)wp);

    if((part == inZoomIn && instdstate)
       || (part == inZoomOut && !instdstate))
#else /* No_STEF_zoommods */

    if((part == inZoomIn && WINDOW_SPARE_FLAG(wp) == inZoomOut)
       || (part == inZoomOut && (WINDOW_SPARE_FLAG(wp) == inZoomIn)))
#endif /* No_STEF_zoommods */
    {
#if !defined(No_STEF_zoommods)
        /* Save userState if not in stdstate */
        if(!instdstate)
        {
            u = &((WStateData *)*WINDOW_DATA(wp))->userState;
            u->top
                = PORT_RECT(wp).top - PORT_BOUNDS(wp).top;
            u->left
                = PORT_RECT(wp).left - PORT_BOUNDS(wp).left;
            u->bottom
                = PORT_RECT(wp).bottom - PORT_BOUNDS(wp).top;
            u->right
                = PORT_RECT(wp).right - PORT_BOUNDS(wp).left;
        }
#endif
        behind = NewRgn();
        CopyRgn(WINDOW_STRUCT_REGION(wp), behind);
        if(part == inZoomIn)
            PORT_RECT(wp) = (*(GUEST<WStateData *> *)WINDOW_DATA(wp))->userState;
        else
            PORT_RECT(wp) = (*(GUEST<WStateData *> *)WINDOW_DATA(wp))->stdState;
        OffsetRect(&PORT_BOUNDS(wp),
                   -PORT_RECT(wp).left - PORT_BOUNDS(wp).left,
                   -PORT_RECT(wp).top - PORT_BOUNDS(wp).top);

        OffsetRect(&PORT_RECT(wp),
                   -PORT_RECT(wp).left, -PORT_RECT(wp).top);
        WINDCALL(wp, wCalcRgns, 0);
        UnionRgn(behind, WINDOW_STRUCT_REGION(wp), behind);

        CalcVisBehind((WindowPeek)wp, behind);
        PaintBehind(WINDOW_NEXT_WINDOW(wp), behind);

#if !defined(THEPORTNEEDNTBEWMGRPORT)
        gp = qdGlobals().thePort;
        SetPort(wmgr_port);
#endif /* THEPORTNEEDNTBEWMGRPORT */
        SetClip(WINDOW_STRUCT_REGION(wp));
        ClipAbove((WindowPeek)wp);
        WINDCALL((WindowPtr)wp, wDraw, 0);
        EraseRgn(WINDOW_CONT_REGION(wp));
        CopyRgn(WINDOW_CONT_REGION(wp), WINDOW_UPDATE_REGION(wp));
#if !defined(THEPORTNEEDNTBEWMGRPORT)
        SetPort(gp);
#endif /* THEPORTNEEDNTBEWMGRPORT */

#if !defined(No_STEF_zoommods)
#else
        WINDOW_SPARE_FLAG(wp) = part;
#endif /* No_STEF_zoommods */
        DisposeRgn(behind);
        if(front)
            SelectWindow(wp);
    }
    ROMlib_rootless_update();
}
