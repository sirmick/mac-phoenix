/* Copyright 1986, 1988, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ControlMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>

#include <WindowMgr.h>
#include <ControlMgr.h>
#include <ToolboxUtil.h>

#include <ctl/ctl.h>

using namespace Executor;

void Executor::C_MoveControl(ControlHandle c, INTEGER h, INTEGER v) /* IMI-325 */
{
    if((*c)->contrlVis)
    {
        HideControl(c);
        (*c)->contrlRect.right = (*c)->contrlRect.right
                                      + h - (*c)->contrlRect.left;
        (*c)->contrlRect.bottom = (*c)->contrlRect.bottom
                                       + v - (*c)->contrlRect.top;
        (*c)->contrlRect.left = h;
        (*c)->contrlRect.top = v;
        ShowControl(c);
    }
    else
    {
        (*c)->contrlRect.right = (*c)->contrlRect.right + h - (*c)->contrlRect.left;
        (*c)->contrlRect.bottom = (*c)->contrlRect.bottom + v - (*c)->contrlRect.top;
        (*c)->contrlRect.left = h;
        (*c)->contrlRect.top = v;
    }
}

void Executor::C_DragControl(ControlHandle c, Point p, const Rect *limit, const Rect *slop,
                             INTEGER axis) /* IMI-325 */
{
    RgnHandle rh;
    LONGINT l;

    CtlCallGuard guard(c);

    if(!(CTLCALL(c, dragCntl, 0) & 0xf000))
    {
        rh = NewRgn();
        CTLCALL(c, calcCntlRgn, ptr_to_longint(rh));
        l = DragGrayRgn(rh, p, limit, slop, axis, (ProcPtr)0);
        if((uint32_t)l != 0x80008000)
            MoveControl(c, (*c)->contrlRect.left + LoWord(l),
                        (*c)->contrlRect.top + HiWord(l));

        DisposeRgn(rh);
    }
}

void Executor::C_SizeControl(ControlHandle c, INTEGER width,
                             INTEGER height) /* IMI-326 */
{
    if((*c)->contrlVis)
    {
        HideControl(c);
        (*c)->contrlRect.right = (*c)->contrlRect.left + width;
        (*c)->contrlRect.bottom = (*c)->contrlRect.top + height;
        ShowControl(c);
    }
    else
    {
        (*c)->contrlRect.right = (*c)->contrlRect.left + width;
        (*c)->contrlRect.bottom = (*c)->contrlRect.top + height;
    }
}
