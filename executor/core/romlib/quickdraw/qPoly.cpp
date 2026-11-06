/* Copyright 1986, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

/* HLock checked by ctm on Mon May 13 17:55:01 MDT 1991 */

#include <base/common.h>
#include <QuickDraw.h>
#include <MemoryMgr.h>

#include <quickdraw/quick.h>
#include <quickdraw/cquick.h>

using namespace Executor;

PolyHandle Executor::C_OpenPoly()
{
    PolyHandle ph;

    ph = (PolyHandle)NewHandle((Size)SMALLPOLY);
    (*ph)->polySize = SMALLPOLY;
    PORT_POLY_SAVE(qdGlobals().thePort) = (Handle)ph;
    HidePen();
    return (ph);
}

void Executor::C_ClosePoly()
{
    INTEGER top = 32767, left = 32767, bottom = -32767, right = -32767;
    GUEST<INTEGER> *ip, *ep;
    INTEGER i;
    PolyHandle ph;

    ph = (PolyHandle)PORT_POLY_SAVE(qdGlobals().thePort);
    for(ip = (GUEST<INTEGER> *)((char *)*ph + SMALLPOLY),
    ep = (GUEST<INTEGER> *)((char *)*ph + (*ph)->polySize);
        ip != ep;)
    {
        if((i = *ip) <= top)
            top = i;
        ++ip;
        if(i >= bottom)
            bottom = i;
        if((i = *ip) <= left)
            left = i;
        ++ip;
        if(i >= right)
            right = i;
    }
    (*ph)->polyBBox.top = top;
    (*ph)->polyBBox.left = left;
    (*ph)->polyBBox.bottom = bottom;
    (*ph)->polyBBox.right = right;
    PORT_POLY_SAVE(qdGlobals().thePort) = nullptr;
    ShowPen();
}

void Executor::C_KillPoly(PolyHandle poly)
{
    DisposeHandle((Handle)poly);
}

void Executor::C_OffsetPoly(PolyHandle poly, INTEGER dh,
                            INTEGER dv) /* Note: IM I-191 is wrong */
{
    GUEST<Point> *pp, *ep;

    if(dh || dv)
    {
        (*poly)->polyBBox.top = (*poly)->polyBBox.top + dv;
        (*poly)->polyBBox.bottom = (*poly)->polyBBox.bottom + dv;
        (*poly)->polyBBox.left = (*poly)->polyBBox.left + dh;
        (*poly)->polyBBox.right = (*poly)->polyBBox.right + dh;
        pp = (*poly)->polyPoints;
        ep = (GUEST<Point> *)(((char *)*poly) + (*poly)->polySize);
        while(pp != ep)
        {
            pp->h = pp->h + (dh);
            pp->v = pp->v + (dv);
            pp++;
        }
    }
}
