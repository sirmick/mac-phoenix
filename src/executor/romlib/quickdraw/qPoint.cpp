/* Copyright 1986, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <quickdraw/cquick.h>

using namespace Executor;

void Executor::C_AddPt(Point src, GUEST<Point> *dst)
{
    dst->h = dst->h + (src.h);
    dst->v = dst->v + (src.v);
}

void Executor::C_SubPt(Point src, GUEST<Point> *dst)
{
    dst->h = dst->h - (src.h);
    dst->v = dst->v - (src.v);
}

void Executor::C_SetPt(GUEST<Point> *pt, INTEGER h, INTEGER v)
{
    pt->h = h;
    pt->v = v;
}

Boolean Executor::C_EqualPt(Point p1, Point p2)
{
    return (p1.h == p2.h && p1.v == p2.v);
}

void Executor::C_LocalToGlobal(GUEST<Point> *pt)
{
    if(qdGlobals().thePort)
    {
        pt->h = pt->h - (PORT_BOUNDS(qdGlobals().thePort).left);
        pt->v = pt->v - (PORT_BOUNDS(qdGlobals().thePort).top);
    }
}

void Executor::C_GlobalToLocal(GUEST<Point> *pt)
{
    if(qdGlobals().thePort)
    {
        pt->h = pt->h + (PORT_BOUNDS(qdGlobals().thePort).left);
        pt->v = pt->v + (PORT_BOUNDS(qdGlobals().thePort).top);
    }
}
