/* Copyright 1986, 1989, 1990, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in WindowMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <WindowMgr.h>
#include <EventMgr.h>
#include <OSEvent.h>
#include <ToolboxUtil.h>
#include <MemoryMgr.h>

#include <quickdraw/cquick.h>
#include <wind/wind.h>

#include <algorithm>

using namespace Executor;

/*
 * Note, the code below probably be rewritten to use XorRgn as much
 *	 as possible and probably have only one CalcVisBehind
 */

void Executor::C_MoveWindow(WindowPtr wp, INTEGER h, INTEGER v, Boolean front)
{
    GrafPtr gp;
    RgnHandle movepart, updatepart, behindpart;
    Rect r;
    WindowPeek w;

    w = (WindowPeek)wp;
    gp = qdGlobals().thePort;
    if(WINDOW_VISIBLE(w))
    {
        SetPort(wmgr_port);
        ClipRect(&GD_BOUNDS(LM(TheGDevice)));
        ClipAbove(w);
        movepart = NewRgn();
        updatepart = NewRgn();
        behindpart = NewRgn();
        SectRgn(PORT_CLIP_REGION(wmgr_port),
                WINDOW_STRUCT_REGION(w), movepart);

#if 1
        /* 
	 * CopyBits does unaligned 32-bit reads from the source, which can
	 * cause it to read beyond the framebuffer in certain circumstances.
	 * This is a cheesy way to prevent that from happening here.  A
	 * better fix would be either in CopyBits or to force an extra page
	 * after the framebuffer.
	 */
        {
            Rect tmpr;
            RgnHandle last_three_pixels;

            tmpr = GD_BOUNDS(LM(TheGDevice));
            tmpr.top = tmpr.bottom - 1;
            tmpr.left = tmpr.right - 3;
            last_three_pixels = NewRgn();
            RectRgn(last_three_pixels, &tmpr);
            DiffRgn(movepart, last_three_pixels, movepart);
            DisposeRgn(last_three_pixels);
        }
#endif

        CopyRgn(movepart, behindpart);
        r = (*WINDOW_STRUCT_REGION(w))->rgnBBox;
    }
#if !defined(LETGCCWAIL)
    else
    {
        movepart = 0;
        updatepart = 0;
        behindpart = 0;
    }
#endif

#if 1
    /*
 * NOTE: the use of portRect below was introduced by Bill, without comment
 *       either here or in the rcslog.  But taking it out made the MSW5.1
 *	 Picture editting window come up in the wrong place.
 *	 (That could be due to other inconsistencies though, like the
 */
    h += PORT_BOUNDS(w).left - PORT_RECT(w).left;
    v += PORT_BOUNDS(w).top - PORT_RECT(w).top;
#else
    h += PORT_BOUNDS(w).left;
    v += PORT_BOUNDS(w).top;
#endif
    if(WINDOW_VISIBLE(w))
    {
        WRAPPER_PIXMAP_FOR_COPY(wrapper);

        OffsetRect(&r, h, v);
        OffsetRgn(movepart, h, v);
        SectRgn(movepart, PORT_CLIP_REGION(wmgr_port), movepart);
        ClipRect(&GD_BOUNDS(LM(TheGDevice)));

        WRAPPER_SET_PIXMAP(wrapper, GD_PMAP(LM(TheGDevice)));

#define NEW_CLIP_HACK
#if defined(NEW_CLIP_HACK)
        /* 
	 * This hack appears to be necessary because clipping via the
	 * clip-region isn't enough to prevent us from reading bits that
	 * are outside the framebuffer.  If there is unmapped memory on
	 * either side of the framebuffer we can eat flaming death for
	 * just looking at it.  This appears to happen under NT4.0.
	 */
        {
            Rect srcr, dstr;

            SectRect(&(*WINDOW_STRUCT_REGION(w))->rgnBBox,
                     &GD_BOUNDS(LM(TheGDevice)), &srcr);

            dstr = GD_BOUNDS(LM(TheGDevice));
            OffsetRect(&dstr, h, v);
            SectRect(&dstr, &r, &dstr);
            CopyBits(wrapper, wrapper, &srcr, &dstr, srcCopy, movepart);
        }
#else
        CopyBits(wrapper, wrapper,
                 &(*WINDOW_STRUCT_REGION(w))->rgnBBox, &r,
                 srcCopy, movepart);
#endif
    }
    OffsetRgn(WINDOW_STRUCT_REGION(w), h, v);
    OffsetRgn(WINDOW_CONT_REGION(w), h, v);
    OffsetRgn(WINDOW_UPDATE_REGION(w), h, v);
    OffsetRect(&PORT_BOUNDS(w), -h, -v);
    if(WINDOW_VISIBLE(w))
    {
        ClipRect(&GD_BOUNDS(LM(TheGDevice)));
        ClipAbove(w);
        DiffRgn(WINDOW_STRUCT_REGION(w), movepart, updatepart);
        SectRgn(PORT_CLIP_REGION(wmgr_port), updatepart, updatepart);
        DiffRgn(behindpart, movepart, behindpart);
        DiffRgn(behindpart, updatepart, behindpart);
        PaintOne(w, updatepart);
        PaintBehind(WINDOW_NEXT_WINDOW(w), behindpart);
        CalcVisBehind(w, updatepart);
        CalcVisBehind(WINDOW_NEXT_WINDOW(w), behindpart);
        CalcVisBehind(WINDOW_NEXT_WINDOW(w), movepart);

        DisposeRgn(movepart);
        DisposeRgn(updatepart);
        DisposeRgn(behindpart);
    }
    if(front)
        SelectWindow((WindowPtr)w);
    SetPort(gp);

    if(WINDOW_VISIBLE(w))
    {
        ROMlib_rootless_update();
    }
}

void Executor::C_DragWindow(WindowPtr wp, Point p, const Rect *rp)
{
    RgnHandle rh;
    LONGINT l;
    EventRecord ev;
    bool cmddown;
    Rect r;

    ThePortGuard guard(wmgr_port);
    GetOSEvent(0, &ev);
    SetClip(LM(GrayRgn));
    cmddown = (bool)(ev.modifiers & cmdKey);
    if(cmddown)
        ClipAbove((WindowPeek)wp);
    rh = NewRgn();
    CopyRgn(WINDOW_STRUCT_REGION(wp), rh);
    r = *rp;
    if(r.top < 24)
        r.top = 24;
    l = DragGrayRgn(rh, p, &r, &r, noConstraint, (ProcPtr)0);
    if((uint32_t)l != 0x80008000)
        MoveWindow(wp,
                   (-PORT_BOUNDS(wp).left
                    + LoWord(l) + PORT_RECT(wp).left),
                   (-PORT_BOUNDS(wp).top
                    + HiWord(l) + PORT_RECT(wp).top),
                   !cmddown);

    DisposeRgn(rh);
}

#define SETUP_PORT(p)       \
    do                      \
    {                       \
        SetPort(p);         \
        PenPat(&qdGlobals().gray);       \
        PenMode(notPatXor); \
    } while(false)

#define RESTORE_PORT(p)   \
    do                    \
    {                     \
        PenPat(&qdGlobals().black);    \
        PenMode(patCopy); \
        SetPort(p);       \
    } while(false)

LONGINT Executor::C_GrowWindow(WindowPtr w, Point startp, const Rect *rp)
{
    EventRecord ev;
    GrafPtr gp;
    Point p;
    Rect r;
    Rect pinr;
    LONGINT l;

    p.h = startp.h;
    p.v = startp.v;
#if 0
    r.left   = - PORT_BOUNDS (w).left;
    r.top    = - PORT_BOUNDS (w).top;
    r.right  = r.left + RECT_WIDTH (&PORT_RECT (w));
    r.bottom = r.top  + RECT_HEIGHT (&PORT_RECT (w));
#else
    r.left = PORT_RECT(w).left - PORT_BOUNDS(w).left;
    r.top = PORT_RECT(w).top - PORT_BOUNDS(w).top;
    r.right = PORT_RECT(w).right - PORT_BOUNDS(w).left;
    r.bottom = PORT_RECT(w).bottom - PORT_BOUNDS(w).top;
#endif

    pinr.left = startp.h - (r.right - r.left);
    if(rp->left > 0)
        pinr.left += rp->left - 1;
    pinr.top    = startp.v - (r.bottom - r.top);
    if(rp->top > 0)
        pinr.top += rp->top - 1;

    pinr.right  = std::min(32767, startp.h - (r.right - r.left) + (int)rp->right);
    pinr.bottom = std::min(32767, startp.v - (r.bottom - r.top) + (int)rp->bottom);

    gp = qdGlobals().thePort;
    SETUP_PORT((GrafPtr)LM(WMgrPort));
    SETUP_PORT(wmgr_port);
    ClipRect(&GD_BOUNDS(LM(TheGDevice)));
    ClipAbove((WindowPeek)w);
    WINDCALL((WindowPtr)w, wGrow, ptr_to_longint(&r));
    while(!GetOSEvent(mUpMask, &ev))
    {
        Point ep = ev.where.get();
        l = PinRect(&pinr, ep);
        ep.v = HiWord(l);
        ep.h = LoWord(l);
        if(p.h != ep.h || p.v != ep.v)
        {
            WINDCALL((WindowPtr)w, wGrow, ptr_to_longint(&r));
            r.right = r.right + (ep.h - p.h);
            r.bottom = r.bottom + (ep.v - p.v);
            WINDCALL((WindowPtr)w, wGrow, ptr_to_longint(&r));
            p = ep;
        }
        CALLDRAGHOOK();
    }
    WINDCALL((WindowPtr)w, wGrow, ptr_to_longint(&r));
    RESTORE_PORT((GrafPtr)LM(WMgrPort));
    RESTORE_PORT(gp);
    if(p.h != startp.h || p.v != startp.v)
        /*-->*/ return (((LONGINT)(r.bottom - r.top) << 16) | (unsigned short)(r.right - r.left));
    else
        return (0L);
}

/* #### speedup? bag saveold, drawnew */

void Executor::C_SizeWindow(WindowPtr w, INTEGER width, INTEGER height,
                            Boolean flag)
{
    if(width || height)
    {
        if(WINDOW_VISIBLE(w))
            SaveOld((WindowPeek)w);

        PORT_RECT(w).right = PORT_RECT(w).left + width;
        PORT_RECT(w).bottom = PORT_RECT(w).top + height;

        ThePortGuard guard(wmgr_port);
        WINDCALL(w, wCalcRgns, 0);
        if(WINDOW_VISIBLE(w))
        {
            DrawNew((WindowPeek)w, flag);

            ROMlib_rootless_update();
        }
    }
}
