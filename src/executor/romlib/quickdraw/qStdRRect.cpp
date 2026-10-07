/* Copyright 1986, 1988, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <MemoryMgr.h>

#include <quickdraw/cquick.h>
#include <quickdraw/picture.h>

#include <quickdraw/region.h>

#include <algorithm>

using namespace Executor;


static RgnHandle roundRectRgn(const Rect& r, int16_t width, int16_t height)
{
    width = std::min<int16_t>(width, r.right - r.left);
    height = std::min<int16_t>(height, r.bottom - r.top);
    Rect ovalRect { r.top, r.left, r.top + height, r.left + width };
    RgnHandle rgn = ROMlib_circrgn(ovalRect);
    
    (*rgn)->rgnBBox = r;
    RgnVector vec(rgn);
    
    int16_t midX = r.left + width / 2;
    int16_t midY = r.top + height / 2;

    int16_t insertX = r.right - r.left - width;
    int16_t insertY = r.bottom - r.top - height;

    for(auto p = vec.begin(); p != vec.end() && *p != RGN_STOP; ++p)
    {
        if(*p >= midY)
            *p += insertY;

        for(++p; *p != RGN_STOP; ++p)
            if(*p >= midX)
                *p += insertX;
    }

    return rgn;
}

void Executor::C_StdRRect(GrafVerb verb, const Rect *r, INTEGER width, INTEGER height)
{
    PAUSEDECL;

    if(qdGlobals().thePort->picSave)
    {
        GUEST<Point> p = { height, width };
        ROMlib_drawingverbrectovalpicupdate(verb, r, &p);
        PICOP(OP_frameRRect + (int)verb);
        PICWRITE(r, sizeof(*r));
    }

    if(PORT_PEN_VIS(qdGlobals().thePort) < 0
       && (!PORT_REGION_SAVE(qdGlobals().thePort) || verb != frame))
        /*-->*/ return;

    PAUSERECORDING;
    if(width < 4 && height < 4)
        StdRect(verb, r);
    else
    {
        width = std::min<int16_t>(width, r->right - r->left);
        height = std::min<int16_t>(height, r->bottom - r->top);

        RgnHandle rh = roundRectRgn(*r, width, height);
        if(verb == frame)
        {
            if(RgnHandle rsave = (RgnHandle)PORT_REGION_SAVE(qdGlobals().thePort))
                XorRgn(rh, rsave, rsave);

            Rect inner = *r;
            Point penSize = PORT_PEN_SIZE(qdGlobals().thePort);
            InsetRect(&inner, penSize.h, penSize.v);
            RgnHandle innerRgn = roundRectRgn(inner, width - 2 * penSize.h, height - 2 * penSize.v);
            DiffRgn(rh, innerRgn, rh);
            DisposeRgn(innerRgn);
            StdRgn(paint, rh);
        }
        else
            StdRgn(verb, rh);
        DisposeRgn(rh);
    }
    RESUMERECORDING;
}
