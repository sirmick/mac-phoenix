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

using namespace Executor;

void Executor::C_StdRect(GrafVerb v, const Rect *rp)
{
    RgnHandle rh, rh2;
    PAUSEDECL;
    Rect patcheduprect;

#define MOREINSANECOMPATIBILITY
#if defined(MOREINSANECOMPATIBILITY)
    if(v == frame && PORT_REGION_SAVE(qdGlobals().thePort))
    {
        if(rp->left > rp->right)
        {
            patcheduprect = *rp;
            patcheduprect.left = rp->right;
            patcheduprect.right = rp->left;
            if(rp->top > rp->bottom)
            {
                patcheduprect.top = rp->bottom;
                patcheduprect.bottom = rp->top;
            }
            rp = &patcheduprect;
        }
        else if(rp->top > rp->bottom)
        {
            patcheduprect = *rp;
            patcheduprect.top = rp->bottom;
            patcheduprect.bottom = rp->top;
            rp = &patcheduprect;
        }
        if(rp == &patcheduprect)
        {
            rh = NewRgn();
            RectRgn(rh, rp);
            XorRgn(rh,
                   (RgnHandle)PORT_REGION_SAVE(qdGlobals().thePort),
                   (RgnHandle)PORT_REGION_SAVE(qdGlobals().thePort));
            DisposeRgn(rh);
            /*-->*/ return;
        }
    }
#endif /* MOREINSANECOMPATIBILITY */

    if(EmptyRect(rp))
        /*-->*/ return;

    if(qdGlobals().thePort->picSave)
    {
        ROMlib_drawingverbrectpicupdate(v, rp);
        PICOP(OP_frameRect + (int)v);
        PICWRITE(rp, sizeof(*rp));
    }

    PAUSERECORDING;
    rh = NewRgn();
    RectRgn(rh, rp);
    switch(v)
    {
        case frame:
            if(PORT_REGION_SAVE(qdGlobals().thePort))
                XorRgn(rh,
                       (RgnHandle)PORT_REGION_SAVE(qdGlobals().thePort),
                       (RgnHandle)PORT_REGION_SAVE(qdGlobals().thePort));
            if(PORT_PEN_VIS(qdGlobals().thePort) >= 0)
            {
                rh2 = NewRgn();
                RectRgn(rh2, rp);
                InsetRgn(rh2,
                         PORT_PEN_SIZE(qdGlobals().thePort).h,
                         PORT_PEN_SIZE(qdGlobals().thePort).v);
                XorRgn(rh, rh2, rh);
                StdRgn(paint, rh);
                DisposeRgn(rh2);
            }
            break;
        case paint:
        case erase:
        case invert:
        case fill:
            StdRgn(v, rh);
    }
    DisposeRgn(rh);
    RESUMERECORDING;
}
