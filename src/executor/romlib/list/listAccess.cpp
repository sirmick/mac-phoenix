/* Copyright 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ListMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <ListMgr.h>
#include <MemoryMgr.h>
#include <IntlUtil.h>
#include <ControlMgr.h>
#include <list/list.h>
#include <rsys/hook.h>
#include <base/functions.impl.h>
#include <algorithm>

using namespace Executor;

void Executor::C_LGetCellDataLocation(GUEST<INTEGER> *offsetp, GUEST<INTEGER> *lenp,
                       Cell cell, ListHandle list) /* IMIV-274 */
{
    GUEST<INTEGER> *ip;

    if((ip = ROMlib_getoffp(cell, list)))
    {
        *offsetp = *ip++ & 0x7FFF;
        *lenp = (*ip & 0x7FFF) - *offsetp;
    }
    else
        *offsetp = *lenp = -1;
}

Boolean Executor::C_LNextCell(Boolean hnext, Boolean vnext, GUEST<Cell> *cellp,
                              ListHandle list) /* IMIV-274 */
{
    Boolean retval;
    Point scratch;
    INTEGER right, bottom;

    scratch.v = cellp->v;
    scratch.h = cellp->h;
    right = (*list)->dataBounds.right;
    bottom = (*list)->dataBounds.bottom;
    if(hnext)
    {
        if(++scratch.h >= right)
            if(vnext && ++scratch.v < bottom)
            {
                scratch.h = 0;
                retval = true;
            }
            else
                retval = false;
        else
            retval = true;
    }
    else
    {
        if(vnext && ++scratch.v < bottom)
            retval = true;
        else
            retval = false;
    }
    if(retval)
    {
        cellp->v = scratch.v;
        cellp->h = scratch.h;
    }
    return retval;
}

void Executor::C_LRect(Rect *cellrect, Cell cell, ListHandle list) /* IMIV-274 */
{
    Point csize;
    INTEGER temp;

    if(PtInRect(cell, &(*list)->visible))
    {
        csize.h = (*list)->cellSize.h;
        csize.v = (*list)->cellSize.v;
        *cellrect = (*list)->rView;
        cellrect->top = cellrect->top + ((cell.v - (*list)->visible.top) * csize.v);
        cellrect->left = cellrect->left + ((cell.h - (*list)->visible.left) * csize.h);
        if((temp = cellrect->top + csize.v) < cellrect->bottom)
            cellrect->bottom = temp;
        if((temp = cellrect->left + csize.h) < cellrect->right)
            cellrect->right = temp;
    }
    else
    {
        cellrect->top = cellrect->left = cellrect->bottom = cellrect->right = 0;
    }
}

using cmpf = UPP<INTEGER(Ptr p1, Ptr p2, INTEGER len1, INTEGER len2)>;


#define CALLCMP(a1, a2, a3, a4, fp) \
    ROMlib_CALLCMP(a1, a2, a3, a4, (cmpf)fp)

namespace Executor
{
static inline INTEGER ROMlib_CALLCMP(Ptr, Ptr, INTEGER, INTEGER, cmpf);
}

static inline INTEGER Executor::ROMlib_CALLCMP(Ptr p1, Ptr p2, INTEGER l1, INTEGER l2, cmpf fp)
{
    INTEGER retval;

    if(fp == &IUMagString)
        retval = C_IUMagString(p1, p2, l1, l2);
    else
    {
        ROMlib_hook(list_cmpnumber);
        retval = fp(p1, p2, l1, l2);
    }
    return retval;
}


Boolean Executor::C_LSearch(Ptr dp, INTEGER dl, Ptr proc, GUEST<Cell> *cellp,
                            ListHandle list) /* IMIV-274 */
{
    GUEST<INTEGER> offS, lenS;
    INTEGER off, len;

    Cell cell;
    GUEST<Cell> swappedcell;
    cmpf fp;

    HLock((Handle)list);
    HLock((Handle)(*list)->cells);

    fp = proc ? (cmpf)proc : &IUMagString;
    cell.h = cellp->h;
    cell.v = cellp->v;
    swappedcell = *cellp;
    /* TODO: SPEEDUP:  the following is a stupid way to do the loop, instead ip
		 and ep should be used! */
    while((C_LGetCellDataLocation(&offS, &lenS, cell, list), len = lenS, off = offS,
           len != -1)
          && CALLCMP(dp, (Ptr)*(*list)->cells + off, dl, len, fp) != 0)
        if(!C_LNextCell(true, true, &swappedcell, list))
        {
            cell.h = (*list)->dataBounds.right;
            cell.v = (*list)->dataBounds.bottom;
        }
        else
        {
            cell.h = swappedcell.h;
            cell.v = swappedcell.v;
        }

    HUnlock((Handle)(*list)->cells);
    HUnlock((Handle)list);
    if(len != -1)
    {
        cellp->h = cell.h;
        cellp->v = cell.v;
        /*-->*/ return true;
    }
    else
        return false;
}

void Executor::C_LSize(INTEGER width, INTEGER height,
                       ListHandle list) /* IMIV-274 */
{
    INTEGER oldright, oldbottom, newright, newbottom;
    ControlHandle cv, ch;
    RgnHandle rectrgn, updatergn;
    Rect r;
    Point p;

    oldright = (*list)->rView.right;
    oldbottom = (*list)->rView.bottom;
    newright = (*list)->rView.left + width;
    (*list)->rView.right = newright;
    newbottom = (*list)->rView.top + height;
    (*list)->rView.bottom = newbottom;
    ch = (*list)->hScroll;
    cv = (*list)->vScroll;

    p.h = (*list)->cellSize.h;
    p.v = (*list)->cellSize.v;
    C_LCellSize(p, list); /* sets visible */

    updatergn = NewRgn();
    rectrgn = NewRgn();
    if(newright != oldright)
    {
        if(newbottom != oldbottom)
        { /* both are different */
            if(ch)
            {
                MoveControl(ch, (*list)->rView.left - 1, newbottom);
                SizeControl(ch, newright - (*list)->rView.left + 2, 16);
            }
            if(cv)
            {
                MoveControl(cv, newright, (*list)->rView.top - 1);
                SizeControl(cv, 16, newbottom - (*list)->rView.top + 2);
            }
            r.top = std::min(oldbottom, newbottom);
            r.bottom = std::max(oldbottom, newbottom);
            r.left = (*list)->rView.left - 1;
            r.right = std::max(oldright, newright);
            if(ch)
                r.bottom = r.bottom + (16);
            RectRgn(rectrgn, &r);
            UnionRgn(rectrgn, updatergn, updatergn);
        }
        else
        { /* just right different */
            if(ch)
            {
                SizeControl(ch, newright - (*list)->rView.left + 2, 16);
            }
            if(cv)
                MoveControl(cv, newright, (*list)->rView.top - 1);
        }
        r.left = std::min(oldright, newright);
        r.right = std::max(oldright, newright);
        r.top = (*list)->rView.top - 1;
        r.bottom = std::max(oldbottom, newbottom);
        if(cv)
            r.right = r.right + (16);
        RectRgn(rectrgn, &r);
        UnionRgn(rectrgn, updatergn, updatergn);
    }
    else if(newbottom != oldbottom)
    { /* just bottom different */
        if(ch)
            MoveControl(ch, (*list)->rView.left - 1, newbottom);
        if(cv)
        {
            SizeControl(cv, 16, newbottom - (*list)->rView.top + 2);
        }
        r.top = std::min(oldbottom, newbottom);
        r.bottom = std::max(oldbottom, newbottom);
        r.left = (*list)->rView.left - 1;
        r.right = std::max(oldright, newright);
        if(ch)
            r.bottom = r.bottom + (16);
        RectRgn(rectrgn, &r);
        UnionRgn(rectrgn, updatergn, updatergn);
    }
    C_LUpdate(updatergn, list);
    DisposeRgn(updatergn);
    DisposeRgn(rectrgn);
}
