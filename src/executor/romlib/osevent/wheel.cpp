/* MacPhoenix: the host's scroll wheel (the tab's wheel and trackpad) on
   System 7 and 8, which have no wheel: the front window's scroll bar
   under the pointer is clicked, its arrow (a line at a time) or for a
   big turn its page part, as the application would see a real click,
   without the visible cursor moving.

   A click is a mouseDown posted at the part when the application next
   asks for events, then the mouse held there for as many lines as the
   wheel turned: Button (StillDown, WaitMouseUp) answers down that many
   times and GetMouse the part's point, so TrackControl's loop (or the
   application's own) calls its action that often; then a mouseUp there.
   The cursor is the host's: only what the Event Manager reports moves. */

#include <base/common.h>
#include <ControlMgr.h>
#include <EventMgr.h>
#include <OSEvent.h>
#include <QuickDraw.h>
#include <ToolboxEvent.h>
#include <WindowMgr.h>

#include <ctl/ctl.h>
#include <osevent/osevent.h>
#include <wind/wind.h>

#include <algorithm>
#include <cstdlib>

using namespace Executor;

namespace
{
int pendingH, pendingV;      /* lines: right and down positive */

struct Press
{
    bool on;
    Point where;             /* global */
    int polls;               /* Button calls still answered down */
    uint32_t deadline;       /* ticks: an application that never tracks */
} press;

/* a turn of this many lines or more clicks the page part (lines/page) */
constexpr int pageLines = 15;
/* lines a click: the rest waits for the next */
constexpr int maxRepeats = 10;

/* A mouse event at AT (global), the button down or up */
void post(INTEGER what, Point at, bool down)
{
    GUEST<EvQElPtr> qel;
    if(PPostEvent(what, 0, &qel) != noErr)
        return;
    EvQElPtr q = qel;
    q->evtQWhere = at;
    q->evtQModifiers = down ? ROMlib_GetModifiers() & ~btnState : ROMlib_GetModifiers() | btnState;
}

void finish()
{
    press.on = false;
    post(mouseUp, press.where, false);
}

/* The point of BAR (local) along its axis at A, across its middle */
Point along(ControlHandle bar, bool vertical, int a)
{
    Rect r = CTL_RECT(bar);
    Point p;
    p.h = vertical ? (r.left + r.right) / 2 : a;
    p.v = vertical ? a : (r.top + r.bottom) / 2;
    return p;
}

/* The point of BAR where TestControl answers PART, searched from the end
   PART is nearest (up/left for the up arrow and page) inwards. */
bool find_part(ControlHandle bar, bool vertical, int part, Point *out)
{
    Rect r = CTL_RECT(bar);
    int lo = vertical ? r.top : r.left, hi = vertical ? r.bottom : r.right;
    bool fromLo = part == inUpButton || part == inPageUp;
    for(int i = 1; i < hi - lo - 1; i++)
    {
        Point p = along(bar, vertical, fromLo ? lo + i : hi - 1 - i);
        if(TestControl(bar, p) == part)
        {
            *out = p;
            return true;
        }
    }
    return false;
}

/* A scroll bar of W along the axis asked for: active, something to
   scroll, an arrow at each end. The nearest to the pointer (local). */
ControlHandle scroll_bar(WindowPtr w, bool vertical, Point at)
{
    ControlHandle best = nullptr;
    long bestDist = 0;
    for(ControlHandle c = WINDOW_CONTROL_LIST(w); c; c = CTL_NEXT_CONTROL(c))
    {
        Rect r = CTL_RECT(c);
        int height = r.bottom - r.top, width = r.right - r.left;
        int across = vertical ? width : height, length = vertical ? height : width;
        if((height > width) != vertical || across < 10 || across > 24 || length < 3 * across)
            continue;
        if(!CTL_VIS(c) || CTL_HILITE(c) == 255 || CTL_MAX(c) <= CTL_MIN(c))
            continue;
        int lo = vertical ? r.top : r.left, hi = vertical ? r.bottom : r.right;
        if(TestControl(c, along(c, vertical, lo + across / 2)) != inUpButton
           || TestControl(c, along(c, vertical, hi - 1 - across / 2)) != inDownButton)
            continue;
        /* outside the bar's span across the pointer's axis counts more */
        int a = vertical ? at.v : at.h, b = vertical ? at.h : at.v;
        int alo = vertical ? r.top : r.left, ahi = vertical ? r.bottom : r.right;
        int blo = vertical ? r.left : r.top, bhi = vertical ? r.right : r.bottom;
        long dist = (a < alo ? alo - a : a >= ahi ? a - ahi + 1 : 0) * 4L
                    + (b < blo ? blo - b : b >= bhi ? b - bhi + 1 : 0);
        if(!best || dist < bestDist)
            best = c, bestDist = dist;
    }
    return best;
}
}

void Executor::ROMlib_wheel_add(int dh, int dv)
{
    pendingH = std::clamp(pendingH + dh, -60, 60);
    pendingV = std::clamp(pendingV + dv, -60, 60);
}

void Executor::ROMlib_wheel_start()
{
    if(press.on)
    {
        if((int32_t)(TickCount() - press.deadline) > 0)
            finish();
        return;
    }
    if(!pendingH && !pendingV)
        return;
    EventRecord ev;
    if(!LM(MBState) || OSEventAvail(mDownMask | mUpMask, &ev))
        return;                         /* the button's in use: later */

    bool vertical = pendingV != 0;
    int lines = vertical ? pendingV : pendingH;
    Point mouse = LM(MouseLocation2);
    WindowPtr w = FrontWindow();
    if(!w || !PtInRgn(mouse, WINDOW_STRUCT_REGION(w)))
    {
        pendingH = pendingV = 0;        /* not over the front window: dropped */
        return;
    }

    GUEST<GrafPtr> saved;
    GetPort(&saved);
    SetPort(w);
    GUEST<Point> local = mouse;
    GlobalToLocal(&local);
    ControlHandle bar = scroll_bar(w, vertical, local.get());
    int n = std::abs(lines), part = lines > 0 ? inDownButton : inUpButton, repeats;
    Point at;
    bool found = false;
    if(bar && n >= pageLines
       && find_part(bar, vertical, lines > 0 ? inPageDown : inPageUp, &at))
    {
        part = lines > 0 ? inPageDown : inPageUp;
        repeats = n / pageLines;
        found = true;
        lines -= (lines > 0 ? 1 : -1) * repeats * pageLines;
    }
    else if(bar && find_part(bar, vertical, part, &at))
    {
        repeats = std::min(n, maxRepeats);
        found = true;
        lines -= (lines > 0 ? 1 : -1) * repeats;
    }
    else
        lines = 0;                      /* no bar there: dropped */
    GUEST<Point> where = at;
    if(found)
        LocalToGlobal(&where);
    SetPort(saved);
    (vertical ? pendingV : pendingH) = lines;
    if(!found)
        return;

    press = { true, where.get(), repeats, (uint32_t)TickCount() + 30 };
    post(mouseDown, press.where, true);
}

void Executor::ROMlib_wheel_mouse(EventRecord *evt)
{
    if(press.on)
    {
        evt->where = press.where;
        evt->modifiers = evt->modifiers & ~btnState;
    }
}

bool Executor::ROMlib_wheel_active()
{
    return press.on;
}

bool Executor::ROMlib_wheel_button(bool *down)
{
    if(!press.on)
        return false;
    if(--press.polls >= 0)
        *down = true;
    else
    {
        finish();
        *down = false;
    }
    return true;
}
