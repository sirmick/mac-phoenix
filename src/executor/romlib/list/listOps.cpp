/* Copyright 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ListMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <ListMgr.h>
#include <MemoryMgr.h>
#include <ControlMgr.h>
#include <list/list.h>
#include <algorithm>

using namespace Executor;

GUEST<INTEGER> *Executor::ROMlib_getoffp(Cell cell, /* INTERNAL */
                                                ListHandle list)
{
    Rect *rp;
    INTEGER ncols;
    GUEST<INTEGER> *retval;

    if(list && PtInRect(cell, rp = &(*list)->dataBounds))
    {
        ncols = rp->right - rp->left;
        retval = (*list)->cellArray + ncols * (cell.v - rp->top) + (cell.h - rp->left);
    }
    else
        retval = 0;
    return retval;
}

typedef enum { Add,
               Rep } AddOrRep;

static void cellhelper(AddOrRep addorrep, Ptr dp, INTEGER dl, Cell cell,
                       ListHandle list)
{
    GUEST<INTEGER> *ip, *ep;
    INTEGER off0, off1, off2, delta, len;
    Ptr sp;
    Cell temp;
    LONGINT ip_offset, ep_offset;

    if((dl > 0 || (addorrep == Rep && dl == 0)) && (ip = ROMlib_getoffp(cell, list)))
    {
        temp.h = (*list)->dataBounds.right - 1;
        temp.v = (*list)->dataBounds.bottom - 1;
        ep = ROMlib_getoffp(temp, list) + 1;
        ip_offset = (char *)ip - (char *)*list;
        ep_offset = (char *)ep - (char *)*list;
        off0 = ip[0] & 0x7FFF;
        off1 = ip[1] & 0x7FFF;
        off2 = ep[0] & 0x7FFF;
        len = off1 - off0;

        /*
 * TODO:  Do this with Munger
 */

        if(addorrep == Add)
            delta = dl;
        else
            delta = dl - len;

        if(delta > 0)
        {
            OSErr err;
            Size current_size, new_size;

            current_size = GetHandleSize((Handle)(*list)->cells);
            new_size = current_size + delta;
            SetHandleSize((Handle)(*list)->cells, new_size);
            err = MemError();
            if(err != noErr)
            {
                warning_unexpected("err = %d, delta = %d", err, delta);
                return;
            }
        }

        sp = (Ptr)*(*list)->cells + off1;
        BlockMoveData(sp, sp + delta, (Size)off2 - off1);

        if(delta < 0)
        {
            Size current_size, new_size;
            OSErr err;

            current_size = GetHandleSize((Handle)(*list)->cells);
            new_size = current_size + delta;
            SetHandleSize((Handle)(*list)->cells, new_size);
            err = MemError();
            if(err != noErr)
                warning_unexpected("err = %d, delta = %d", err, delta);
        }

        BlockMoveData(dp, (Ptr)*(*list)->cells + off0 + (addorrep == Add ? len : 0), (Size)dl);

        ip = (GUEST<INTEGER> *)((char *)*list + ip_offset);
        ep = (GUEST<INTEGER> *)((char *)*list + ep_offset);

        if(delta)
        {
            while(++ip <= ep)
                *ip = *ip + (delta);
        }
        if(((*list)->listFlags & lDrawingModeOff) == 0)
            C_LDraw(cell, list);
    }
}

void Executor::C_LAddToCell(Ptr dp, INTEGER dl, Cell cell,
                            ListHandle list) /* IMIV-272 */
{
    cellhelper(Add, dp, dl, cell, list);
}

void Executor::C_LClrCell(Cell cell, ListHandle list) /* IMIV-272 */
{
    cellhelper(Rep, (Ptr)0, 0, cell, list);
}

void Executor::C_LGetCell(Ptr dp, GUEST<INTEGER> *dlp, Cell cell,
                          ListHandle list) /* IMIV-272 */
{
    GUEST<INTEGER> *ip;
    INTEGER off1, off2;
    INTEGER ntomove;

    if((ip = ROMlib_getoffp(cell, list)))
    {
        off1 = *ip++ & 0x7fff;
        off2 = *ip & 0x7fff;
        ntomove = off2 - off1;
        if(ntomove > *dlp)
            ntomove = *dlp;
        BlockMoveData((Ptr)*(*list)->cells + off1, dp, (Size)ntomove);
        *dlp = ntomove;
    }
}

void Executor::C_LSetCell(Ptr dp, INTEGER dl, Cell cell,
                          ListHandle list) /* IMIV-272 */
{
    cellhelper(Rep, dp, dl, cell, list);
}

void Executor::C_LCellSize(Point csize, ListHandle list) /* IMIV-273 */
{
    ListPtr lp;
    GrafPtr gp;
    FontInfo fi;
    INTEGER nh, nv;

    lp = *list;
    if(!(lp->cellSize.h = csize.h))
        lp->cellSize.h = (lp->rView.right - lp->rView.left) / std::max(1, (lp->dataBounds.right
                                                                                 - lp->dataBounds.left));
    if(!(lp->cellSize.v = csize.v))
    {
        gp = qdGlobals().thePort;
        SetPort(lp->port);
        GetFontInfo(&fi);
        lp = *list; /* could have moved */
        lp->cellSize.v = fi.ascent + fi.descent + fi.leading;
        SetPort(gp);
    }

    nh = (lp->rView.right - lp->rView.left + lp->cellSize.h - 1) / lp->cellSize.h;
    nv = (lp->rView.bottom - lp->rView.top + lp->cellSize.v - 1) / lp->cellSize.v;
    
    lp->visible.right = lp->visible.left + nh;
    lp->visible.bottom = lp->visible.top + nv;

    {
        ControlHandle control;

        if((control = (*list)->hScroll))
        {
            INTEGER min, max;

            ROMlib_hminmax(&min, &max, *list);
            SetControlMaximum(control, max);
        }
    }
    {
        ControlHandle control;

        if((control = (*list)->vScroll))
        {
            INTEGER min, max;

            ROMlib_vminmax(&min, &max, *list);
            SetControlMaximum(control, max);
        }
    }
}

Boolean Executor::C_LGetSelect(Boolean next, GUEST<Cell> *cellp,
                               ListHandle list) /* IMIV-273 */
{
    GUEST<INTEGER> *ip, *ep;
    INTEGER nint, ncols, rown, coln;
    Boolean retval;
    Cell temp, c;
    Point p;

    if(!list || !cellp)
        retval = false;
    else if(next)
    {
        c.h = cellp->h;
        c.v = cellp->v;
        if(!(ip = ROMlib_getoffp(c, list)))
        {
            temp.h = 0;
            temp.h = cellp->v + 1;
            ip = ROMlib_getoffp(temp, list);
        }
        if(!ip)
            retval = false;
        else
        {
            temp.h = (*list)->dataBounds.right - 1;
            temp.v = (*list)->dataBounds.bottom - 1;
            ep = ROMlib_getoffp(temp, list) + 1;
            while(ip != ep && !(*ip & 0x8000))
                ip++;
            if(ip == ep)
                retval = false;
            else
            {
                nint = ip - (*list)->cellArray;
                ncols = (*list)->dataBounds.right - (*list)->dataBounds.left;
                rown = nint / ncols;
                coln = nint % ncols;
                cellp->v = (*list)->dataBounds.top + rown;
                cellp->h = (*list)->dataBounds.left + coln;
                retval = true;
            }
        }
    }
    else
    {
        p.h = cellp->h;
        p.v = cellp->v;
        if(!(ip = ROMlib_getoffp(p, list)))
            retval = false;
        else
            retval = (*ip & 0x8000) ? true : false;
    }
    return retval;
}

/* LSetSelect in listMouse.c */
