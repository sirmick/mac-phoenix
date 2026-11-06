/* Copyright 1989 - 2000 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ListMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <ListMgr.h>
#include <MemoryMgr.h>
#include <ControlMgr.h>
#include <list/list.h>

using namespace Executor;

INTEGER Executor::C_LAddColumn(INTEGER count, INTEGER coln,
                               ListHandle list) /* IMIV-271 */
{
    Rect todraw;
    Point c;
    INTEGER ncols, nrows, noffsets, offset;
    Size nbefore, nafter;
    Ptr ip, op;
    int i;
    Point p;

    nrows = (*list)->dataBounds.bottom - (*list)->dataBounds.top;
    noffsets = count * nrows;
    if(coln > (*list)->dataBounds.right)
        coln = (*list)->dataBounds.right;

    if(coln < (*list)->dataBounds.left)
        coln = (*list)->dataBounds.left;

    (*list)->dataBounds.right = (*list)->dataBounds.right + count;

    if(noffsets)
    {

        SetHandleSize((Handle)list,
                      GetHandleSize((Handle)list) + noffsets * sizeof(INTEGER));
        ncols = (*list)->dataBounds.right - (*list)->dataBounds.left;
        nbefore = (coln - (*list)->dataBounds.left) * sizeof(INTEGER);
        nafter = (ncols - count) * sizeof(INTEGER) - nbefore;
        ip = (Ptr)((*list)->cellArray + nrows * (ncols - count) + 1);
        op = (Ptr)((*list)->cellArray + nrows * ncols + 1);
        op -= sizeof(INTEGER);
        ip -= sizeof(INTEGER);
        *(INTEGER *)op = *(INTEGER *)ip; /* sentinel */
        /* SPEEDUP:  merge the two BlockMoves ... (unroll begining and end) */
        while(--nrows >= 0)
        {
            ip -= nafter;
            op -= nafter;
            BlockMoveData(ip, op, nafter);
            offset = *(GUEST<INTEGER> *)op & 0x7FFF;
            for(i = 0; ++i <= count;)
            {
                op -= sizeof(INTEGER);
                *(GUEST<INTEGER> *)op = offset;
            }
            ip -= nbefore;
            op -= nbefore;
            BlockMoveData(ip, op, nbefore);
        }

        p.h = (*list)->cellSize.h;
        p.v = (*list)->cellSize.v;
        C_LCellSize(p, list); /* recalcs visible */

        if(((*list)->listFlags & lDrawingModeOff) == 0)
        {
            todraw = (*list)->dataBounds;
            todraw.left = coln;
            SectRect(&todraw, &(*list)->visible, &todraw);
            for(c.v = todraw.top; c.v < todraw.bottom; c.v++)
                for(c.h = todraw.left; c.h < todraw.right; c.h++)
                    C_LDraw(c, list);
        }
    }
    (*list)->maxIndex = ((*list)->dataBounds.right - (*list)->dataBounds.left) 
        * ((*list)->dataBounds.bottom - (*list)->dataBounds.top) * 2;

    return coln;
}

INTEGER Executor::C_LAddRow(INTEGER count, INTEGER rown,
                            ListHandle list) /* IMIV-271 */
{
    Rect todraw;
    Cell c;
    INTEGER ncols, nrows, noffsets, offset;
    Size nbefore, nafter;
    GUEST<INTEGER> *ip, *op;
    Point p;

    ncols = (*list)->dataBounds.right - (*list)->dataBounds.left;
    noffsets = count * ncols;
    if(rown > (*list)->dataBounds.bottom)
        rown = (*list)->dataBounds.bottom;

    if(rown < (*list)->dataBounds.top)
        rown = (*list)->dataBounds.top;

    (*list)->dataBounds.bottom = (*list)->dataBounds.bottom + count;

    if(noffsets)
    {

        SetHandleSize((Handle)list,
                      GetHandleSize((Handle)list) + noffsets * sizeof(INTEGER));
        nrows = (*list)->dataBounds.bottom - (*list)->dataBounds.top;
        nbefore = (rown - (*list)->dataBounds.top);
        nafter = ((nrows - count) - nbefore) * ncols;
        ip = (*list)->cellArray + (nrows - count) * ncols + 1;
        op = (*list)->cellArray + nrows * ncols + 1;
        *--op = *--ip; /* sentinel */
        ip -= nafter;
        op -= nafter;
        offset = *ip & 0x7FFF;
        BlockMoveData((Ptr)ip, (Ptr)op, nafter * sizeof(INTEGER));
        /* move the after rows */
        while(--noffsets >= 0)
            *--op = offset;

        p.h = (*list)->cellSize.h;
        p.v = (*list)->cellSize.v;
        C_LCellSize(p, list); /* recalcs visible */

        if(((*list)->listFlags & lDrawingModeOff) == 0)
        {
            todraw = (*list)->dataBounds;
            todraw.top = rown;
            SectRect(&todraw, &(*list)->visible, &todraw);
            for(c.v = todraw.top; c.v < todraw.bottom; c.v++)
                for(c.h = todraw.left; c.h < todraw.right; c.h++)
                    C_LDraw(c, list);
        }
    }
    (*list)->maxIndex = ((*list)->dataBounds.right - (*list)->dataBounds.left) 
        * ((*list)->dataBounds.bottom - (*list)->dataBounds.top) * 2;
    return rown;
}

static void
compute_visible_rect(Rect *rp, ListHandle list, INTEGER top, INTEGER left,
                     INTEGER bottom, INTEGER right)
{
    INTEGER h, v;
    INTEGER new_top, new_left, new_bottom, new_right;

    h = (*list)->cellSize.h;
    v = (*list)->cellSize.v;

    new_top = (*list)->rView.top + (top - (*list)->visible.top) * v;
    new_left = (*list)->rView.left + (left - (*list)->visible.left) * h;
    new_bottom = new_top + (bottom - top) * v;
    new_right = new_left + (right - left) * h;

    rp->top = new_top;
    rp->left = new_left;
    rp->bottom = new_bottom;
    rp->right = new_right;

    SectRect(rp, &(*list)->rView, rp);
}

void Executor::C_LDelColumn(INTEGER count, INTEGER coln,
                            ListHandle list) /* IMIV-271 */
{
    Rect todraw;
    Cell c;
    INTEGER ncols, nrows, noffsets, delta;
    Size nbefore, nafter, ntomove;
    GUEST<uint16_t> *ip, *op; /* unsigned INTEGER */
    Ptr dataip, dataop;
    INTEGER off1, off2, off3, off4, off5;
    int i;
    ControlHandle control;
    Point p;

    if(!list || coln >= (*list)->dataBounds.right)
        /*-->*/ return; /* invalid */

    if(count == 0 || (coln == (*list)->dataBounds.left && count >= (*list)->dataBounds.right - (*list)->dataBounds.left))
    {
        SetHandleSize((Handle)(*list)->cells, (Size)0);
        SetHandleSize((Handle)list, sizeof(ListRec));
        (*list)->cellArray[0] = 0;
        (*list)->dataBounds.right = (*list)->dataBounds.left;
        (*list)->visible.left = (*list)->dataBounds.left;
        p.h = (*list)->cellSize.h;
        p.v = (*list)->cellSize.v;
        C_LCellSize(p, list); /* recalcs visible */
        if(((*list)->listFlags & lDrawingModeOff) == 0)
            EraseRect(&(*list)->rView);
        if((control = (*list)->hScroll))
            SetControlMaximum(control, (*control)->contrlMin);

        (*list)->maxIndex = ((*list)->dataBounds.right - (*list)->dataBounds.left) 
            * ((*list)->dataBounds.bottom - (*list)->dataBounds.top) * 2;

        /*-->*/ return; /* quick delete of everything */
    }

    if(coln + count > (*list)->dataBounds.right)
        count = (*list)->dataBounds.right - coln;

    nrows = (*list)->dataBounds.bottom - (*list)->dataBounds.top;
    noffsets = count * nrows;
    if(coln > (*list)->dataBounds.right)
        coln = (*list)->dataBounds.right;

    (*list)->dataBounds.right = (*list)->dataBounds.right - count;

    if(noffsets)
    {
        INTEGER visible_right, bounds_right;

        ncols = (*list)->dataBounds.right - (*list)->dataBounds.left;
        nbefore = (coln - (*list)->dataBounds.left);
        nafter = ncols - nbefore;
        ip = op = (GUEST<uint16_t> *)(*list)->cellArray;
        dataip = dataop = (Ptr)*(*list)->cells;
        delta = 0;
        /* SPEEDUP:  partial loop unrolling ... combine things and don't
		     bother adding delta when we know that it's zero */
        while(--nrows >= 0)
        {
            off1 = *ip & 0x7FFF;
            for(i = nbefore; --i >= 0;) /* copy before-offsets */
                *op++ = *ip++ - delta;

            off2 = *ip & 0x7FFF;
            ntomove = off2 - off1;
            BlockMoveData(dataip, dataop, ntomove); /* copy before-data */
            dataip += ntomove;
            dataop += ntomove;

            ip += count; /* skip count offsets */

            off3 = *ip & 0x7FFF;
            ntomove = off3 - off2;
            dataip += ntomove; /* skip appropriate data */
            delta += ntomove; /* note this */

            off4 = *ip & 0x7FFF;
            for(i = nafter; --i >= 0;) /* copy after-offsets */
                *op++ = *ip++ - delta;

            off5 = *ip & 0x7FFF;
            ntomove = off5 - off4;
            BlockMoveData(dataip, dataop, ntomove); /* copy before-data */
            dataip += ntomove;
            dataop += ntomove;
        }
        *op++ = *ip++ - delta; /* sentinel */
        SetHandleSize((Handle)list,
                      GetHandleSize((Handle)list) - noffsets * sizeof(INTEGER));
        SetHandleSize((Handle)(*list)->cells,
                      GetHandleSize((Handle)(*list)->cells) - delta);

        p.h = (*list)->cellSize.h;
        p.v = (*list)->cellSize.v;

        /* save visible_right and bounds_right now, because LCellSize
	   will adjust them and we won't be able to figure out if we
	   needed to force a scroll */

        visible_right = (*list)->visible.right;
        bounds_right = (*list)->dataBounds.right;
        C_LCellSize(p, list); /* recalcs visible */
        if((control = (*list)->hScroll))
        {
            INTEGER visible_left;

            visible_left = (*list)->visible.left;

            /* Determine whether or not we need to scroll up one location
	       (because we were maximally scrolled down and we deleted
	       something that was visible) */

            if(visible_left > 0 && visible_right > bounds_right)
            {
                --visible_left;
                (*list)->visible.left = visible_left;
                coln = visible_left;
            }
        }

        if(((*list)->listFlags & lDrawingModeOff) == 0)
        {
            Rect eraser;

            compute_visible_rect(&eraser, list, (*list)->visible.top,
                                 (*list)->visible.right,
                                 (*list)->visible.bottom,
                                 visible_right);
            EraseRect(&eraser);

            todraw = (*list)->dataBounds;
            todraw.left = coln;
            SectRect(&todraw, &(*list)->visible, &todraw);
            for(c.v = todraw.top; c.v < todraw.bottom; c.v++)
                for(c.h = todraw.left; c.h < todraw.right; c.h++)
                    C_LDraw(c, list);
        }
    }

    (*list)->maxIndex = ((*list)->dataBounds.right - (*list)->dataBounds.left) 
        * ((*list)->dataBounds.bottom - (*list)->dataBounds.top) * 2;
}

void Executor::C_LDelRow(INTEGER count, INTEGER rown,
                         ListHandle list) /* IMIV-272 */
{
    Rect todraw;
    Cell c;
    INTEGER ncols, nrows, noffsets;
    Size nbefore, nafter;
    GUEST<uint16_t> *ip, *op;
    ControlHandle control;
    INTEGER off1, off2, off3;
    Size delta, ntomove;
    Point p;

    if(!list || rown >= (*list)->dataBounds.bottom)
        /*-->*/ return; /* invalid */

    if(count == 0 || (rown == (*list)->dataBounds.top && count >= (*list)->dataBounds.bottom - (*list)->dataBounds.top))
    {
        SetHandleSize((Handle)(*list)->cells, (Size)0);
        SetHandleSize((Handle)list, sizeof(ListRec));
        (*list)->cellArray[0] = 0;
        (*list)->dataBounds.bottom = (*list)->dataBounds.top;
        (*list)->visible.top = (*list)->dataBounds.top;
        p.h = (*list)->cellSize.h;
        p.v = (*list)->cellSize.v;
        C_LCellSize(p, list); /* recalcs visible */
        if(((*list)->listFlags & lDrawingModeOff) == 0)
            EraseRect(&(*list)->rView);
        if((control = (*list)->vScroll))
            SetControlMaximum(control, (*control)->contrlMin);

        (*list)->maxIndex = ((*list)->dataBounds.right - (*list)->dataBounds.left) 
            * ((*list)->dataBounds.bottom - (*list)->dataBounds.top) * 2;

        /*-->*/ return; /* quick delete of all */
    }

    if(rown + count > (*list)->dataBounds.bottom)
        count = (*list)->dataBounds.bottom - rown;

    ncols = (*list)->dataBounds.right - (*list)->dataBounds.left;
    noffsets = count * ncols;

    (*list)->dataBounds.bottom = (*list)->dataBounds.bottom - count;

    if(noffsets)
    {
        INTEGER visible_bottom, bounds_bottom;

        nrows = (*list)->dataBounds.bottom - (*list)->dataBounds.top;
        nbefore = (rown - (*list)->dataBounds.top) * ncols;
        nafter = nrows * ncols - nbefore;
        ip = op = (GUEST<uint16_t> *)(*list)->cellArray + nbefore;
        ip += noffsets;
        off1 = *op & 0x7FFF;
        off2 = *ip & 0x7FFF;
        delta = off2 - off1;

        while(--nafter >= 0)
            *op++ = *ip++ - delta;
        off3 = *ip & 0x7FFF;
        *op = *ip - delta; /* sentinel */

        ntomove = off3 - off2;
        BlockMoveData((Ptr)*(*list)->cells + off2,
                      (Ptr)*(*list)->cells + off1, ntomove);

        SetHandleSize((Handle)list,
                      GetHandleSize((Handle)list) - noffsets * sizeof(INTEGER));
        SetHandleSize((Handle)(*list)->cells,
                      GetHandleSize((Handle)(*list)->cells) - delta);

        p.h = (*list)->cellSize.h;
        p.v = (*list)->cellSize.v;

        /* save visible_bottom and bounds_bottom now, because LCellSize
	   will adjust them and we won't be able to figure out if we
	   needed to force a scroll */

        visible_bottom = (*list)->visible.bottom;
        bounds_bottom = (*list)->dataBounds.bottom;
        C_LCellSize(p, list); /* recalcs visible */
        if((control = (*list)->vScroll))
        {
            INTEGER visible_top;

            visible_top = (*list)->visible.top;

            /* Determine whether or not we need to scroll up one location
	       (because we were maximally scrolled down and we deleted
	       something that was visible) */

            if(visible_top > 0 && visible_bottom > bounds_bottom)
            {
                --visible_top;
                (*list)->visible.top = visible_top;
                rown = visible_top;
            }
        }

        if(((*list)->listFlags & lDrawingModeOff) == 0)
        {
            Rect eraser;

            compute_visible_rect(&eraser, list, (*list)->visible.bottom,
                                 (*list)->visible.left, visible_bottom,
                                 (*list)->visible.right);
            EraseRect(&eraser);

            todraw = (*list)->dataBounds;
            todraw.top = rown;
            SectRect(&todraw, &(*list)->visible, &todraw);

            for(c.v = todraw.top; c.v < todraw.bottom; c.v++)
                for(c.h = todraw.left; c.h < todraw.right; c.h++)
                    C_LDraw(c, list);
        }
    }
    (*list)->maxIndex = ((*list)->dataBounds.right - (*list)->dataBounds.left) 
        * ((*list)->dataBounds.bottom - (*list)->dataBounds.top) * 2;
}
