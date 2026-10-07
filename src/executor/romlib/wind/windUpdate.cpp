/* Copyright 1986, 1989, 1990, 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in WindowMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <WindowMgr.h>

#include <quickdraw/cquick.h>
#include <wind/wind.h>

using namespace Executor;

void Executor::C_InvalRect(const Rect *r)
{
    if(qdGlobals().thePort)
    {
        RgnHandle rh;
        Rect r_copy;

        r_copy = *r; /* Just in case NewRgn moves memory */
        rh = NewRgn();
        RectRgn(rh, &r_copy);
        OffsetRgn(rh, -PORT_BOUNDS(qdGlobals().thePort).left,
                  -PORT_BOUNDS(qdGlobals().thePort).top);
        UnionRgn(rh, WINDOW_UPDATE_REGION(qdGlobals().thePort),
                 WINDOW_UPDATE_REGION(qdGlobals().thePort));
        DisposeRgn(rh);
    }
}

void Executor::C_InvalRgn(RgnHandle r)
{
    GrafPtr current_port;
    RgnHandle update_rgn;
    int top, left;

    current_port = qdGlobals().thePort;
    top = PORT_BOUNDS(current_port).top;
    left = PORT_BOUNDS(current_port).left;

    OffsetRgn(r, -left, -top);

    update_rgn = WINDOW_UPDATE_REGION(current_port);
    UnionRgn(r, update_rgn, update_rgn);

    OffsetRgn(r, left, top);
}

void Executor::C_ValidRect(const Rect *r)
{
    RgnHandle rh;

    rh = NewRgn();
    RectRgn(rh, r);
    OffsetRgn(rh, -PORT_BOUNDS(qdGlobals().thePort).left,
              -PORT_BOUNDS(qdGlobals().thePort).top);
    DiffRgn(WINDOW_UPDATE_REGION(qdGlobals().thePort), rh,
            WINDOW_UPDATE_REGION(qdGlobals().thePort));
    DisposeRgn(rh);
}

void Executor::C_ValidRgn(RgnHandle r)
{
    OffsetRgn(r, -PORT_BOUNDS(qdGlobals().thePort).left,
              -PORT_BOUNDS(qdGlobals().thePort).top);
    DiffRgn(WINDOW_UPDATE_REGION(qdGlobals().thePort), r,
            WINDOW_UPDATE_REGION(qdGlobals().thePort));
    OffsetRgn(r, PORT_BOUNDS(qdGlobals().thePort).left,
              PORT_BOUNDS(qdGlobals().thePort).top);
}

int Executor::ROMlib_emptyvis = 0;

void Executor::C_BeginUpdate(WindowPtr w)
{
    /* #warning Should LM(SaveVisRgn) ever become 0? */
    if(!LM(SaveVisRgn))
        LM(SaveVisRgn) = (RgnHandle)NewHandle(0);

    CopyRgn(PORT_VIS_REGION(w), LM(SaveVisRgn));

    if(EmptyRgn(WINDOW_UPDATE_REGION(w)))
        ROMlib_emptyvis = 1;
#if 0
  else /* our ROMlib_emptyvis hack below doesn't work if there are some
	  routines that write to the screen and ignore it */
#endif
    {
        CopyRgn(WINDOW_UPDATE_REGION(w), PORT_VIS_REGION(w));
        OffsetRgn(PORT_VIS_REGION(w),
                  PORT_BOUNDS(w).left,
                  PORT_BOUNDS(w).top);
        SectRgn(PORT_VIS_REGION(w), LM(SaveVisRgn), PORT_VIS_REGION(w));
        SetEmptyRgn(WINDOW_UPDATE_REGION(w));
        ROMlib_emptyvis = EmptyRgn(PORT_VIS_REGION(w));
    }
}

void Executor::C_EndUpdate(WindowPtr w)
{
    CopyRgn(LM(SaveVisRgn), PORT_VIS_REGION(w));
    CopyRgn(WINDOW_CONT_REGION(w), LM(SaveVisRgn));
    OffsetRgn(LM(SaveVisRgn),
              PORT_BOUNDS(w).left,
              PORT_BOUNDS(w).top);
    ROMlib_emptyvis = 0;
}
