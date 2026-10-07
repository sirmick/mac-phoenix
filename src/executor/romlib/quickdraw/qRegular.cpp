/* Copyright 1986, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <DialogMgr.h>
#include <quickdraw/quick.h>
#include <quickdraw/cquick.h>
#include <ctl/ctl.h>
#include <wind/wind.h>

using namespace Executor;

void Executor::C_FrameRect(const Rect *r)
{
    CALLRECT(frame, r);
}

void Executor::C_PaintRect(const Rect *r)
{
    CALLRECT(paint, r);
}

void Executor::C_EraseRect(const Rect *r)
{
    CALLRECT(erase, r);
}

void Executor::C_InvertRect(const Rect *r)
{
    CALLRECT(invert, r);
}

void Executor::C_FillRect(const Rect *r, const Pattern *pat)
{
    if(!EmptyRgn(PORT_VIS_REGION(qdGlobals().thePort)))
    {
        ROMlib_fill_pat(*pat);
        CALLRECT(fill, r);
    }
}

void Executor::C_FrameOval(const Rect *r)
{
    CALLOVAL(frame, r);
}

void Executor::C_PaintOval(const Rect *r)
{
    CALLOVAL(paint, r);
}

void Executor::C_EraseOval(const Rect *r)
{
    CALLOVAL(erase, r);
}

void Executor::C_InvertOval(const Rect *r)
{
    CALLOVAL(invert, r);
}

void Executor::C_FillOval(const Rect *r, const Pattern *pat)
{
    ROMlib_fill_pat(*pat);
    CALLOVAL(fill, r);
}

void Executor::C_FrameRoundRect(const Rect *r, INTEGER ow, INTEGER oh)
{
    CALLRRECT(frame, r, ow, oh);
}

void Executor::C_PaintRoundRect(const Rect *r, INTEGER ow, INTEGER oh)
{
    CALLRRECT(paint, r, ow, oh);
}

void Executor::C_EraseRoundRect(const Rect *r, INTEGER ow, INTEGER oh)
{
    CALLRRECT(erase, r, ow, oh);
}

void Executor::C_InvertRoundRect(const Rect *r, INTEGER ow, INTEGER oh)
{
    CALLRRECT(invert, r, ow, oh);
}

void Executor::C_FillRoundRect(const Rect *r, INTEGER ow, INTEGER oh, const Pattern *pat)
{
    ROMlib_fill_pat(*pat);
    CALLRRECT(fill, r, ow, oh);
}

void Executor::C_FrameArc(const Rect *r, INTEGER start, INTEGER angle)
{
    CALLARC(frame, r, start, angle);
}

void Executor::C_PaintArc(const Rect *r, INTEGER start, INTEGER angle)
{
    CALLARC(paint, r, start, angle);
}

void Executor::C_EraseArc(const Rect *r, INTEGER start, INTEGER angle)
{
    CALLARC(erase, r, start, angle);
}

void Executor::C_InvertArc(const Rect *r, INTEGER start, INTEGER angle)
{
    CALLARC(invert, r, start, angle);
}

void Executor::C_FillArc(const Rect *r, INTEGER start, INTEGER angle, const Pattern *pat)
{
    ROMlib_fill_pat(*pat);
    CALLARC(fill, r, start, angle);
}

void Executor::C_FrameRgn(RgnHandle rh)
{
    CALLRGN(frame, rh);
}

void Executor::C_PaintRgn(RgnHandle rh)
{
    CALLRGN(paint, rh);
}

void Executor::C_EraseRgn(RgnHandle rh)
{
    CALLRGN(erase, rh);
}

void Executor::C_InvertRgn(RgnHandle rh)
{
    CALLRGN(invert, rh);
}

void Executor::C_FillRgn(RgnHandle rh, const Pattern *pat)
{
    ROMlib_fill_pat(*pat);
    CALLRGN(fill, rh);
}

void Executor::C_FramePoly(PolyHandle poly)
{
    CALLPOLY(frame, poly);
}

void Executor::C_PaintPoly(PolyHandle poly)
{
    CALLPOLY(paint, poly);
}

void Executor::C_ErasePoly(PolyHandle poly)
{
    CALLPOLY(erase, poly);
}

void Executor::C_InvertPoly(PolyHandle poly)
{
    CALLPOLY(invert, poly);
}

void Executor::C_FillPoly(PolyHandle poly, const Pattern *pat)
{
    ROMlib_fill_pat(*pat);
    CALLPOLY(fill, poly);
}
