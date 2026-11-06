/* Copyright 1986, 1988, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

/* HLock checked by ctm on Mon May 13 17:57:59 MDT 1991 */

#include <base/common.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <MemoryMgr.h>

#include <quickdraw/cquick.h>
#include <quickdraw/picture.h>

using namespace Executor;

static void polyrgn(PolyHandle ph, RgnHandle rh)
{
    GUEST<Point> *pp, *ep;
    Point firstp;
    GUEST<INTEGER> tmpvis;
    INTEGER state;

    state = HGetState((Handle)ph);
    HLock((Handle)ph);
    pp = (*ph)->polyPoints;
    ep = (GUEST<Point> *)((char *)*ph + (*ph)->polySize);
    firstp.h = (*ph)->polyPoints[0].h;
    firstp.v = (*ph)->polyPoints[0].v;
    if(ep[-1].h == firstp.h && ep[-1].v == firstp.v)
        ep--;

    tmpvis = PORT_PEN_VIS(qdGlobals().thePort);
    PORT_PEN_VIS(qdGlobals().thePort) = 0;
    OpenRgn();
    MoveTo(pp->h, pp->v);
    pp++;
    while(pp != ep)
    {
        LineTo(pp->h, pp->v);
        pp++;
    }
    LineTo(firstp.h, firstp.v);
    CloseRgn(rh);
    PORT_PEN_VIS(qdGlobals().thePort) = tmpvis;
    HSetState((Handle)ph, state);
}

void Executor::C_StdPoly(GrafVerb verb, PolyHandle ph)
{
    RgnHandle rh;
    Point p;
    GUEST<Point> *pp, *ep;
    Point firstp;
    INTEGER state;
    PAUSEDECL;

    if(!ph || !*ph || (*ph)->polySize == 10 || EmptyRect(&(*ph)->polyBBox))
        /*-->*/ return;

    state = HGetState((Handle)ph);
    HLock((Handle)ph);
    if(qdGlobals().thePort->picSave)
    {
        ROMlib_drawingverbpicupdate(verb);
        PICOP(OP_framePoly + (int)verb);
        PICWRITE(*ph, (*ph)->polySize);
    }

    if(PORT_PEN_VIS(qdGlobals().thePort) < 0 && !PORT_REGION_SAVE(qdGlobals().thePort)
       && verb != frame)
    {
        HSetState((Handle)ph, state);
        /*-->*/ return;
    }

    PAUSERECORDING;
    switch(verb)
    {
        case frame:
            /* we used to unconditionally close the polygon here, but
	   testing on the mac shows that is incorrect */

            pp = (*ph)->polyPoints;
            ep = (GUEST<Point> *)((char *)*ph + (*ph)->polySize);
            firstp.h = pp[0].h;
            firstp.v = pp[0].v;
            MoveTo(firstp.h, firstp.v);
            for(++pp; pp != ep; pp++)
            {
                p.h = pp[0].h;
                p.v = pp[0].v;
                StdLine(p);
                PORT_PEN_LOC(qdGlobals().thePort) = pp[0];
            }

            break;
        case paint:
        case erase:
        case invert:
        case fill:
            rh = NewRgn();
            polyrgn(ph, rh);
            StdRgn(verb, rh);
            DisposeRgn(rh);
    }
    RESUMERECORDING;
    HSetState((Handle)ph, state);
}
