/* Copyright 1986-1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ControlMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>

#include <algorithm>

#include <WindowMgr.h>
#include <ControlMgr.h>
#include <ControlMgr8.h>
#include <EventMgr.h>
#include <ToolboxEvent.h>
#include <OSEvent.h>
#include <ListMgr.h>

#include <ctl/ctl.h>
#include <wind/wind.h>
#include <rsys/stdfile.h>
#include <rsys/hook.h>
#include <osevent/osevent.h>
#include <base/functions.impl.h>

/*
 * Loser suggests that controls are tested in the opposite order that
 * they're on the linked list.  That would make sense, since it would
 * insure that when drawn under normal circumstances, the control that
 * is visible and clicked is the one that will be FindControl will
 * return.  Recursion is you friend.
 */

using namespace Executor;

INTEGER
find_control_helper(Point p, ControlHandle c,
                    GUEST<ControlHandle> *cp)
{
    INTEGER retval;
    ControlHandle next;

    if(!c)
        retval = 0;
    else
    {
        next = CTL_NEXT_CONTROL(c);
        if(!(retval = find_control_helper(p, next, cp)))
        {
            if(CTL_VIS(c) && CTL_HILITE(c) != 255)
            {
                retval = TestControl(c, p);
                if(retval)
                    *cp = c;
            }
        }
    }
    return retval;
}

INTEGER Executor::C_FindControl(Point p, WindowPtr w,
                                GUEST<ControlHandle> *cp) /* IMI-323 */
{
    INTEGER retval;

    /* MacPhoenix: in a window with a root control, the deepest one */
    ControlHandle hit;
    if(w && ROMlib_control8_find(w, p, &hit, &retval))
    {
        *cp = hit;
        return retval;
    }
    retval = w ? find_control_helper(p, WINDOW_CONTROL_LIST(w), cp) : 0;
    if(!retval)
        *cp = 0;
    return retval;
}



/* MacPhoenix: an indicator with live feedback (8.x: Appearance's live
   scroll bars) dragged: its region moved with the mouse (held to the
   thumbCntl limits, back where it started outside the slop), the
   definition setting the value from it (kControlMsgCalcValueFromPos), and
   the action procedure called with the control and the part all along, as
   for the arrows. 8.6's Finder scrolls its windows so, its procedure taking
   those two (G3); DragTheRgn would call it with none. */
static INTEGER track_live(ControlHandle c, Point p, INTEGER part, ControlActionUPP a)
{
    thumbstr thumb;
    thumb._tlimit.left = p.h;
    thumb._tlimit.top = p.v;
    CTLCALL(c, thumbCntl, ptr_to_longint(&thumb));
    Rect limit = thumb._tlimit, slop = thumb._tslop;
    INTEGER axis = thumb._taxis;
    RgnHandle start = NewRgn(), moved = NewRgn();
    CTLCALL(c, calcThumbRgn, ptr_to_longint(start));
    (*c)->contrlHilite = part;
    CTLCALL(c, drawCntl, part);
    Point last = p;
    while(StillDown())
    {
        GUEST<Point> gm;
        GetMouse(&gm);
        Point m = gm.get();
        if(!PtInRect(m, &slop))
            m = p;
        else
        {
            m.h = std::clamp<INTEGER>(m.h, limit.left, std::max<INTEGER>(limit.left, limit.right - 1));
            m.v = std::clamp<INTEGER>(m.v, limit.top, std::max<INTEGER>(limit.top, limit.bottom - 1));
        }
        if(axis == hAxisOnly)
            m.v = p.v;
        else if(axis == vAxisOnly)
            m.h = p.h;
        if(m.h != last.h || m.v != last.v)
        {
            CopyRgn(start, moved);
            OffsetRgn(moved, m.h - p.h, m.v - p.v);
            CTLCALL(c, kControlMsgCalcValueFromPos, ptr_to_longint(moved));
            last = m;
        }
        a(c, part);
    }
    DisposeRgn(moved);
    DisposeRgn(start);
    (*c)->contrlHilite = 0;
    CTLCALL(c, drawCntl, 0);
    return part;
}

INTEGER Executor::C_TrackControl(ControlHandle c, Point p,
                                 ControlActionUPP a) /* IMI-323 */
{
    INTEGER partstart, inpart;
    EventRecord ev;
    thumbstr thumb;
    RgnHandle rh;
    LONGINT l;
    Point whereunswapped;

    int retval;

    /* MacPhoenix: an 8.x control definition that tracks the mouse itself */
    if(ROMlib_control8_handles_tracking(c))
        return ROMlib_control8_track(c, p, a, 0);

    CtlCallGuard guard(c);
    partstart = inpart = TestControl(c, p);

    /* A point outside the control is no part: the Quadra's TrackControl
       answers 0 for one (upstream asked GetMouse again, for Super Dice It
       1.1's global coordinates). */
    if(!partstart)
        return 0;

    if(CTL_ACTION_AS_LONG(c) == -1L)
    {
        /* if we don't draw before/after, then jim's demo cdefs
	      don't update pop up menu bars */
        CTLCALL(c, drawCntl, 0);
        /* this is not how IMI says to do it, but it makes
	      Microsoft Word work. */
        CTLCALL(c, autoTrack, inpart);
        CTLCALL(c, drawCntl, 0);

        /* NOTE 1: force a return of inpart */
        partstart = inpart;
        goto done;
    }

    if(a == (ControlActionUPP)-1)
        a = CTL_ACTION(c);

    /* #if 0 reading the above code suggests that
   it's impossible to get here */
    if(0 && a == (ControlActionUPP)-1)
    {
        /* totally custom */
        while(!GetOSEvent(mUpMask, &ev))
        {
            GlobalToLocal(&ev.where);
            whereunswapped.h = ev.where.h;
            whereunswapped.v = ev.where.v;
            inpart = TestControl(c, whereunswapped);
            CTLCALL(c, autoTrack, inpart);
        }
        CTLCALL(c, posCntl, ((((int32_t)whereunswapped.v - p.v) << 16)
                             | (uint16_t)(whereunswapped.h - p.h)));
        inpart = TestControl(c, whereunswapped);
    }
    else
        /* #endif */
        if(partstart > 128 && a && ROMlib_control8_live_feedback(c))
        inpart = track_live(c, p, partstart, a);
    else if(partstart > 128)
    {
        /* indicator */
        /* The code used to & the result of CTLCALL with 0xf000
	      for no apparent reason.  Taking it out fixed a bug in
	      Quicken. */
        if(!CTLCALL(c, dragCntl, partstart))
        {
            thumb._tlimit.left = p.h;
            thumb._tlimit.top = p.v;
            CTLCALL(c, thumbCntl, ptr_to_longint(&thumb));
            rh = NewRgn();

            CTLCALL(c, calcThumbRgn, ptr_to_longint(rh));

            LM(DragPattern) = qdGlobals().ltGray;
            l = DragTheRgn(rh, p, &thumb._tlimit, &thumb._tslop,
                           thumb._taxis, (ProcPtr)a);
            if((uint32_t)l != 0x80008000)
            {
                CTLCALL(c, posCntl, l);
                inpart = partstart;
            }
            DisposeRgn(rh);
        }
    }
    else
    {
        /* not an indicator */
        (*c)->contrlHilite = partstart;
        CTLCALL(c, drawCntl, partstart);
        /* MacPhoenix: a part held repeats as 8.6's does on the G3: its
           action at once, again after the keyboard's delay until repeat
           (KeyThresh, 24 ticks), then every 6 ticks; a click up to about
           0.4 s is one step (each pass of this loop called it: a short
           click on a Finder window's page part scrolled it to the end).
           The scroll wheel's clicks (osevent/wheel.cpp) count passes. */
        uint32_t next_action = 0;
        bool acted = false;
        auto action_due = [&]() {
            uint32_t now = TickCount();
            if(ROMlib_wheel_active() || !acted)
                next_action = now + std::max<int>(LM(KeyThresh), 1);
            else if((int32_t)(now - next_action) >= 0)
                next_action = now + 6;
            else
                return false;
            acted = true;
            return true;
        };
        /* the action callback can remove mouse up events which is why the
	      following line is not a GetOSEvent call. */
        while(!OSEventAvail(mUpMask, &ev) && StillDown())
        {
            GlobalToLocal(&ev.where);
            whereunswapped.h = ev.where.h;
            whereunswapped.v = ev.where.v;
            inpart = TestControl(c, whereunswapped);
            if(inpart && inpart != partstart)
                inpart = 0;
            if(inpart != (*c)->contrlHilite)
            {
                (*c)->contrlHilite = inpart;
                CTLCALL(c, drawCntl, partstart);
            }
            if(a && inpart && action_due())
                a(c, inpart);
        }
        GetOSEvent(mUpMask, &ev);
        GlobalToLocal(&ev.where);
        whereunswapped.h = ev.where.h;
        whereunswapped.v = ev.where.v;
        if((*c)->contrlHilite)
        {
            (*c)->contrlHilite = 0;
            CTLCALL(c, drawCntl, partstart);
        }
        inpart = TestControl(c, whereunswapped);
    }
done:;

    retval = (partstart == inpart ? inpart : 0); /* DON'T CHANGE THIS
						  line w/o looking to Note 1
						  above */
    return retval;
}

INTEGER Executor::C_TestControl(ControlHandle c, Point p) /* IMI-325 */
{
    int16_t retval;

    CtlCallGuard guard(c);
    if((*c)->contrlVis && (*c)->contrlHilite != 255)
        retval = CTLCALL(c, testCntl, ((LONGINT)p.v << 16) | (unsigned short)p.h);
    else
        retval = 0;
    return retval;
}
