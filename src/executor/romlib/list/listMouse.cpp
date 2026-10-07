/* Copyright 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ListMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <EventMgr.h>
#include <ToolboxEvent.h>
#include <ToolboxUtil.h>
#include <OSEvent.h>
#include <ControlMgr.h>
#include <ListMgr.h>

#include <quickdraw/cquick.h>
#include <list/list.h>
#include <rsys/hook.h>
#include <base/functions.impl.h>
#include <base/traps.impl.h>

using namespace Executor;


static void findcell(GUEST<Cell> *, ListHandle);
static void setselectnilflag(Boolean setit, Cell cell, ListHandle list,
                             Boolean hiliteempty);
static void scrollbyvalues(ListHandle);
static void rect2value(Rect *in, Rect *butnotin, INTEGER value,
                       ListHandle list, Boolean hiliteempty);
static void rectvalue(Rect *rp, INTEGER value, ListHandle list,
                      Boolean hiliteempty);

static void findcell(GUEST<Cell> *cp, ListHandle list)
{
    cp->h = (cp->h - (*list)->rView.left) / (*list)->cellSize.h + (*list)->visible.left;
    cp->v = (cp->v - (*list)->rView.top) / (*list)->cellSize.v + (*list)->visible.top;

    if(cp->h >= (*list)->visible.right)
        cp->h = 32767;
    if(cp->v >= (*list)->visible.bottom)
        cp->v = 32767;
}

static void setselectnilflag(Boolean setit, Cell cell, ListHandle list,
                             Boolean hiliteempty)
{
    GrafPtr saveport;
    GUEST<RgnHandle> saveclip;
    Rect r;
    GUEST<INTEGER> *ip;
    INTEGER off0wbit, off0, off1;
    LISTDECL();

    if((ip = ROMlib_getoffp(cell, list)))
    {
        off0wbit = *ip;
        if(setit)
            *ip = off0wbit | 0x8000;
        else
            *ip = off0wbit & 0x7FFF;
        if(PtInRect(cell, &(*list)->visible) && (!(off0wbit & 0x8000) ^ !setit))
        {
            off0 = off0wbit & 0x7FFF;
            off1 = ip[1] & 0x7FFF;
            if(hiliteempty || off0 != off1)
            {

                C_LRect(&r, cell, list);

                saveport = qdGlobals().thePort;
                SetPort((*list)->port);
                saveclip = PORT_CLIP_REGION(qdGlobals().thePort);
                PORT_CLIP_REGION(qdGlobals().thePort) = NewRgn();
                ClipRect(&r);

                LISTBEGIN(list);
/* #define TEMPORARY_HACK_DO_NOT_CHECK_IN */
#if !defined(TEMPORARY_HACK_DO_NOT_CHECK_IN)
                LISTCALL(lHiliteMsg, setit, &r, cell, off0, off1 - off0, list);
#endif
                LISTEND(list);

                DisposeRgn(PORT_CLIP_REGION(qdGlobals().thePort));
                PORT_CLIP_REGION(qdGlobals().thePort) = saveclip;
                SetPort(saveport);
            }
        }
    }
}

static void rectvalue(Rect *rp, INTEGER value, ListHandle list,
                      Boolean hiliteempty)
{
    GUEST<INTEGER> *ip, *ep;
    GUEST<INTEGER> *sp;
    Cell c;
    LISTDECL();

    LISTBEGIN(list);
    for(c.v = rp->top; c.v < rp->bottom; c.v++)
    {
        c.h = rp->left;
        if((sp = ip = ROMlib_getoffp(c, list)))
        {
            for(ep = ip + (rp->right - rp->left); ip != ep; ip++)
                if(!(*ip & 0x8000) ^ !value)
                {
                    c.h = rp->left + (ip - sp);
                    setselectnilflag(value, c, list, hiliteempty);
                }
        }
    }
    LISTEND(list);
}

static void rect2value(Rect *in, Rect *butnotin, INTEGER value,
                       ListHandle list, Boolean hiliteempty)
{
    GUEST<INTEGER> *ip;
    Cell c;

    for(c.v = in->top; c.v < in->bottom; c.v++)
        for(c.h = in->left; c.h < in->right; c.h++)
            if(!PtInRect(c, butnotin) && (ip = ROMlib_getoffp(c, list)))
                if(!(*ip & 0x8000) ^ !value)
                    setselectnilflag(value, c, list, hiliteempty);
}

static void scrollbyvalues(ListHandle list)
{
    INTEGER h, v;
    ControlHandle ch;
    Point p;

    h = (ch = (*list)->hScroll) ? GetControlValue(ch) : toHost((*list)->visible.left);
    v = (ch = (*list)->vScroll) ? GetControlValue(ch) : toHost((*list)->visible.top);
    C_LScroll(h - (*list)->visible.left, v - (*list)->visible.top, list);
    (*list)->visible.left = h;
    (*list)->visible.top = v;
    p.h = (*list)->cellSize.h;
    p.v = (*list)->cellSize.v;
    C_LCellSize(p, list);
}

static void C_ROMlib_mytrack(ControlHandle ch, INTEGER part)
{
    INTEGER quant, page;
    ListPtr lp;

    lp = *guest_cast<ListHandle>((*ch)->contrlRfCon);

    page = ch == lp->hScroll ? lp->visible.right - lp->visible.left - 1
                                 : lp->visible.bottom - lp->visible.top - 1;

    switch(part)
    {
        case inUpButton:
            quant = -1;
            break;
        case inDownButton:
            quant = 1;
            break;
        case inPageUp:
            quant = -page;
            break;
        case inPageDown:
            quant = page;
            break;
        default:
            gui_assert(0);
            quant = 0;
            break;
    }
    SetControlValue(ch, GetControlValue(ch) + quant);
    scrollbyvalues(guest_cast<ListHandle>((*ch)->contrlRfCon));
}
PASCAL_FUNCTION_PTR(ROMlib_mytrack);

static inline Boolean CALLCLICK(ListClickLoopUPP fp)
{
    Boolean retval;

    ROMlib_hook(list_clicknumber);
    retval = fp();
    return retval;
}


Boolean Executor::C_LClick(Point pt, INTEGER mods,
                           ListHandle list) /* IMIV-273 */
{
    ControlHandle ch, scrollh, scrollv;
    struct
    {
        short top, left, bottom, right;
    } r;
    GUEST<Rect> rswapped;
    Boolean doubleclick, ctlchanged;
    Boolean hiliteempty, onlyone, userects, disjoint, extend;
    Boolean initial;
    enum
    {
        Off,
        On,
        UseSense
    } cellvalue; /* order is important here */
    Byte flags;
    EventRecord evt;
    Rect anchor, oldselrect{}, newselrect, newcellr, pinrect;
    Cell c, oldcellunswapped, newcellunswapped;
    GUEST<Cell> newcell, oldcell{}, cswapped;
    LONGINT l;
    INTEGER dh, dv;
    Point p;

    doubleclick = false;
    if(PtInRect(pt, &(*list)->rView))
    {

        flags = (*list)->selFlags;
        newcell.h = pt.h;
        newcell.v = pt.v;
        findcell(&newcell, list);
        if(newcell.h == (*list)->lastClick.h && newcell.v == (*list)->lastClick.v && TickCount() < (*list)->clikTime + LM(DoubleTime))
            doubleclick = true;
        (*list)->lastClick = newcell;
        hiliteempty = !(flags & lNoNilHilite);
        if(((mods & shiftKey) || (flags & lExtendDrag)) && !(flags & lOnlyOne))
        {
            onlyone = false;
            disjoint = !(flags & lNoDisjoint);
            userects = !(flags & lNoRect);
            cellvalue = (flags & lUseSense) ? UseSense : On;
            extend = (flags & lUseSense) ? false : !(flags & lNoExtend);
        }
        else if((mods & cmdKey) && !(flags & lOnlyOne))
        {
            onlyone = false;
            disjoint = !(flags & lNoDisjoint);
            userects = false;
            cellvalue = UseSense;
            extend = false;
        }
        else
        {
            onlyone = true;
            disjoint = false;
            userects = false;
            cellvalue = On;
            extend = false;
        }
        initial = C_LGetSelect(false, &newcell, list);
        if(cellvalue == UseSense)
            cellvalue = initial ? Off : On;
        if(!disjoint && !initial)
            rectvalue(&(*list)->dataBounds, Off, list, hiliteempty);

        if(userects)
        {
            anchor.top = anchor.bottom = 0;
            if(extend)
            {
                rswapped = (*list)->dataBounds;
                r.top = rswapped.top;
                r.left = rswapped.left;
                r.bottom = rswapped.bottom;
                r.right = rswapped.right;
                for(c.h = r.left; c.h < r.right; c.h++)
                    for(c.v = r.top; c.v < r.bottom; c.v++)
                    {
                        cswapped.h = c.h;
                        cswapped.v = c.v;
                        if(C_LGetSelect(false, &cswapped, list))
                            goto out1;
                    }
            out1:
                c.h = cswapped.h;
                c.v = cswapped.v;
                if(c.h != r.right)
                {
                    anchor.left = c.h;

                    for(c.h = r.right - 1; c.h >= r.left; c.h--)
                        for(c.v = r.top; c.v < r.bottom; c.v++)
                        {
                            cswapped.h = c.h;
                            cswapped.v = c.v;
                            if(C_LGetSelect(false, &cswapped, list))
                                goto out2;
                        }
                out2:
                    c.h = cswapped.h;
                    c.v = cswapped.v;
                    anchor.right = c.h + 1;

                    cswapped.h = r.left;
                    cswapped.v = r.top;
                    C_LGetSelect(true, &cswapped, list);
                    anchor.top = cswapped.v;

                    for(c.v = r.bottom - 1; c.v >= r.top; c.v--)
                        for(c.h = r.left; c.h < r.right; c.h++)
                        {
                            cswapped.h = c.h;
                            cswapped.v = c.v;
                            if(C_LGetSelect(false, &cswapped, list))
                                goto out3;
                        }
                out3:
                    anchor.bottom = cswapped.v + 1;
                }
            }
            if(anchor.top == anchor.bottom)
            {
                anchor.top = newcell.v;
                anchor.left = newcell.h;
                anchor.bottom = anchor.top + 1;
                anchor.right = anchor.left + 1;
            }
            c.h = anchor.left;
            c.v = anchor.top;
            C_LRect(&rswapped, c, list);
            if(pt.h < rswapped.right && pt.v < rswapped.bottom)
            {
                anchor.top = anchor.bottom - 1;
                anchor.left = anchor.right - 1;
            }
            else
            {
                anchor.bottom = anchor.top + 1;
                anchor.right = anchor.left + 1;
            }
            oldselrect = (flags & lUseSense) ? anchor : (*list)->dataBounds;
        }

        (*list)->clikTime = TickCount();
        (*list)->clikLoc.h = pt.h;
        (*list)->clikLoc.v = pt.v;
        oldcell.h = 32767;

        evt.where.h = pt.h;
        evt.where.v = pt.v;
        pinrect = (*list)->rView;
        pinrect.left = pinrect.left - 1;
        pinrect.bottom = pinrect.bottom - 1;
        do
        {
            (*list)->mouseLoc = evt.where;
            if((*list)->lClikLoop)
                if(CALLCLICK((*list)->lClikLoop))
                    /*-->*/ break;
            p.h = evt.where.h;
            p.v = evt.where.v;
            if(!PtInRect(p, &(*list)->rView))
            {
                ctlchanged = false;
                scrollh = (*list)->hScroll;
                scrollv = (*list)->vScroll;
                dh = 0;
                dv = 0;
                if(evt.where.h < (*list)->rView.left)
                {
                    if(scrollh)
                    {
                        SetControlValue(scrollh, GetControlValue(scrollh) - 1);
                        ctlchanged = true;
                    }
                    else
                        dh = -1;
                }
                else if(evt.where.h > (*list)->rView.right)
                {
                    if(scrollh)
                    {
                        SetControlValue(scrollh, GetControlValue(scrollh) + 1);
                        ctlchanged = true;
                    }
                    else
                        dh = 1;
                }
                if(evt.where.v < (*list)->rView.top)
                {
                    if(scrollv)
                    {
                        SetControlValue(scrollv, GetControlValue(scrollv) - 1);
                        ctlchanged = true;
                    }
                    else
                        dv = -1;
                }
                else if(evt.where.v > (*list)->rView.bottom)
                {
                    if(scrollv)
                    {
                        SetControlValue(scrollv, GetControlValue(scrollv) + 1);
                        ctlchanged = true;
                    }
                    else
                        dv = 1;
                }
                if(ctlchanged)
                    scrollbyvalues(list);
                else
                    C_LScroll(dh, dv, list);
            }
            p.h = evt.where.h;
            p.v = evt.where.v;
            l = PinRect(&pinrect, p);
            newcell.h = LoWord(l);
            newcell.v = HiWord(l);
            findcell(&newcell, list);
            if(userects)
            {
                newcellr.top = newcell.v;
                newcellr.left = newcell.h;
                newcellr.bottom = newcellr.top + 1;
                newcellr.right = newcellr.left + 1;
                UnionRect(&anchor, &newcellr, &newselrect);
                rect2value(&oldselrect, &newselrect, !cellvalue, list,
                           hiliteempty);
                rectvalue(&newselrect, cellvalue, list, hiliteempty);
                oldselrect = newselrect;
            }
            else
            {
                if(newcell.h != oldcell.h || newcell.v != oldcell.v)
                {
                    if(onlyone && oldcell.h != 32767)
                    {
                        oldcellunswapped.h = oldcell.h;
                        oldcellunswapped.v = oldcell.v;
                        setselectnilflag(false, oldcellunswapped, list,
                                         hiliteempty);
                    }
                    newcellunswapped.h = newcell.h;
                    newcellunswapped.v = newcell.v;
                    setselectnilflag(cellvalue, newcellunswapped, list,
                                     hiliteempty);
                    oldcell = newcell;
                }
            }
        } while(!OSEventAvail(mUpMask, &evt) && (GlobalToLocal(&evt.where), true));
    }
    else if(((ch = (*list)->hScroll) && PtInRect(pt, &(*ch)->contrlRect)) || ((ch = (*list)->vScroll) && PtInRect(pt, &(*ch)->contrlRect)))
    {
        if(TestControl(ch, pt) == inThumb)
        {
            TrackControl(ch, pt, nullptr);
            scrollbyvalues(list);
        }
        else
            TrackControl(ch, pt, &ROMlib_mytrack);
    }
    return doubleclick;
    return 0;
}

LONGINT Executor::C_LLastClick(ListHandle list) /* IMIV-273 */
{
    return ((LONGINT)(*list)->lastClick.v << 16) | (unsigned short)(*list)->lastClick.h;
}

void Executor::C_LSetSelect(Boolean setit, Cell cell,
                            ListHandle list) /* IMIV-273 */
{
    setselectnilflag(setit, cell, list, true);
}
