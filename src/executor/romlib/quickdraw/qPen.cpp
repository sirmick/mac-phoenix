/* Copyright 1986, 1989, 1990 by Abacus Research and
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

void Executor::C_HidePen()
{
    if(qdGlobals().thePort)
        PORT_PEN_VIS(qdGlobals().thePort) = PORT_PEN_VIS(qdGlobals().thePort) - 1;
}

void Executor::C_ShowPen()
{
    if(qdGlobals().thePort)
        PORT_PEN_VIS(qdGlobals().thePort) = PORT_PEN_VIS(qdGlobals().thePort) + 1;
}

void Executor::C_GetPen(GUEST<Point> *ptp)
{
    if(qdGlobals().thePort)
        *ptp = PORT_PEN_LOC(qdGlobals().thePort);
}

void Executor::C_GetPenState(PenState *ps)
{
    if(!qdGlobals().thePort)
        return;

    if(CGrafPort_p(qdGlobals().thePort))
    {
        PixPatHandle pen_pixpat;

        ps->pnLoc = PORT_PEN_LOC(qdGlobals().thePort);
        ps->pnSize = PORT_PEN_SIZE(qdGlobals().thePort);
        ps->pnMode = PORT_PEN_MODE(qdGlobals().thePort);

        pen_pixpat = CPORT_PEN_PIXPAT(theCPort);
        /*
 * NOTE: it's not clear what the Mac does here.  Cotton has been
 *	 wrong about this stuff before.
 */
        if(PIXPAT_TYPE(pen_pixpat) == pixpat_type_orig)
            /* #warning GetPenState not necessarily implemented correctly... */
            ps->pnPat = PIXPAT_1DATA(pen_pixpat);
        else
        {
            /* high bit indicates there is a pixpat (not a pattern)
	     stored in the pnPat field */
            ps->pnMode |= 0x8000;
            *(PixPatHandle *)&ps->pnPat.pat[0] = pen_pixpat;
        }
    }
    else
        *ps = *(PenState *)&PORT_PEN_LOC(qdGlobals().thePort);
}

void Executor::C_SetPenState(PenState *ps)
{
    if(!qdGlobals().thePort)
        return;

    PORT_PEN_LOC(qdGlobals().thePort) = ps->pnLoc;
    PORT_PEN_SIZE(qdGlobals().thePort) = ps->pnSize;

    if(ps->pnMode & 0x8000)
    {
        PORT_PEN_MODE(qdGlobals().thePort) = ps->pnMode & ~0x8000;
        PenPixPat(*(PixPatHandle *)&ps->pnPat.pat[0]);
    }
    else
    {
        PORT_PEN_MODE(qdGlobals().thePort) = ps->pnMode;
        PenPat(&ps->pnPat);
    }
}

void Executor::draw_state_save(draw_state_t *draw_state)
{
    GrafPtr current_port;

    current_port = qdGlobals().thePort;

    GetPenState(&draw_state->pen_state);
    if(CGrafPort_p(current_port))
    {
        draw_state->fg_color = CPORT_RGB_FG_COLOR(current_port);
        draw_state->bk_color = CPORT_RGB_BK_COLOR(current_port);
    }
    draw_state->fg = PORT_FG_COLOR(current_port);
    draw_state->bk = PORT_BK_COLOR(current_port);

    draw_state->tx_font = PORT_TX_FONT(current_port);
    draw_state->tx_face = PORT_TX_FACE(current_port);
    draw_state->tx_size = PORT_TX_SIZE(current_port);
    draw_state->tx_mode = PORT_TX_MODE(current_port);
}

void Executor::draw_state_restore(draw_state_t *draw_state)
{
    GrafPtr current_port;

    current_port = qdGlobals().thePort;

    SetPenState(&draw_state->pen_state);
    if(CGrafPort_p(current_port))
    {
        CPORT_RGB_FG_COLOR(current_port) = draw_state->fg_color;
        CPORT_RGB_BK_COLOR(current_port) = draw_state->bk_color;
    }
    PORT_FG_COLOR(current_port) = draw_state->fg;
    PORT_BK_COLOR(current_port) = draw_state->bk;

    PORT_TX_FONT(current_port) = draw_state->tx_font;
    PORT_TX_FACE(current_port) = draw_state->tx_face;
    PORT_TX_SIZE(current_port) = draw_state->tx_size;
    PORT_TX_MODE(current_port) = draw_state->tx_mode;
}

void Executor::C_PenSize(INTEGER w, INTEGER h)
{
    if(qdGlobals().thePort)
    {
        PORT_PEN_SIZE(qdGlobals().thePort).h = w;
        PORT_PEN_SIZE(qdGlobals().thePort).v = h;
    }
}

void Executor::C_PenMode(INTEGER m)
{
    if(qdGlobals().thePort)
        PORT_PEN_MODE(qdGlobals().thePort) = m;
}

void Executor::C_PenPat(const Pattern *pp)
{
    if(qdGlobals().thePort)
    {
        if(CGrafPort_p(qdGlobals().thePort))
        {
            PixPatHandle old_pen;

            old_pen = CPORT_PEN_PIXPAT(theCPort);
            if(PIXPAT_TYPE(old_pen) == pixpat_type_orig)
                PIXPAT_1DATA(old_pen) = *pp;
            else
            {
                PixPatHandle new_pen = NewPixPat();

                PIXPAT_TYPE(new_pen) = 0;
                PIXPAT_1DATA(new_pen) = *pp;

                PenPixPat(new_pen);
            }
            /* #warning PenPat not currently implemented correctly... */
        }
        else
            PORT_PEN_PAT(qdGlobals().thePort) = *pp;
    }
}

void Executor::C_PenNormal()
{
    if(qdGlobals().thePort)
    {
        PenSize(1, 1);
        PenMode(patCopy);
        PenPat(&qdGlobals().black);
    }
}

void Executor::C_MoveTo(INTEGER h, INTEGER v)
{
    if(qdGlobals().thePort)
    {
        PORT_PEN_LOC(qdGlobals().thePort).h = h;
        PORT_PEN_LOC(qdGlobals().thePort).v = v;
    }
}

void Executor::C_Move(INTEGER dh, INTEGER dv)
{
    if(qdGlobals().thePort)
    {
        PORT_PEN_LOC(qdGlobals().thePort).h = PORT_PEN_LOC(qdGlobals().thePort).h + (dh);
        qdGlobals().thePort->pnLoc.v = PORT_PEN_LOC(qdGlobals().thePort).v + (dv);
    }
}

void Executor::C_LineTo(INTEGER h, INTEGER v)
{
    Point p;

    if(qdGlobals().thePort)
    {
        p.h = h;
        p.v = v;
        CALLLINE(p);
        PORT_PEN_LOC(qdGlobals().thePort).h = p.h;
        PORT_PEN_LOC(qdGlobals().thePort).v = p.v;
    }
}

void Executor::C_Line(INTEGER dh, INTEGER dv)
{
    Point p;

    if(qdGlobals().thePort)
    {
        p.h = PORT_PEN_LOC(qdGlobals().thePort).h + dh;
        p.v = PORT_PEN_LOC(qdGlobals().thePort).v + dv;
        CALLLINE(p);
        PORT_PEN_LOC(qdGlobals().thePort).h = p.h;
        PORT_PEN_LOC(qdGlobals().thePort).v = p.v;
    }
}
