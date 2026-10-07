/* Copyright 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ListMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <MemoryMgr.h>
#include <ResourceMgr.h>
#include <ListMgr.h>
#include <ControlMgr.h>
#include <list/list.h>

using namespace Executor;

#define STEF_lActivefix

void Executor::ROMlib_vminmax(INTEGER *minp, INTEGER *maxp,
                              ListPtr lp) /* INTERNAL */
{
    *minp = lp->dataBounds.top;
    *maxp = *minp + lp->dataBounds.bottom - lp->visible.bottom + lp->visible.top;
    if(lp->cellSize.v * (lp->visible.bottom - lp->visible.top) > lp->rView.bottom - lp->rView.top)
        ++*maxp;
}

void Executor::ROMlib_hminmax(INTEGER *minp, INTEGER *maxp,
                              ListPtr lp) /* INTERNAL */
{
    *minp = lp->dataBounds.left;
    *maxp = *minp + lp->dataBounds.right - lp->visible.right + lp->visible.left;
    if(lp->cellSize.h * (lp->visible.right - lp->visible.left) > lp->rView.right - lp->rView.left)
        ++*maxp;
}

/* #define LIST_DEBUG */
#if defined(LIST_DEBUG)
typedef struct ll_elem_str
{
    ListHandle list;
    GrafPtr orig_port;
    void (*lastTextProc)(INTEGER bc, Ptr p, Point num, Point den);
    struct ll_elem_str *next;
} ll_elem;

static ll_elem *ll_head;

static void
add_list(ListHandle list)
{
    ll_elem *new_elemp;

    new_elemp = malloc(sizeof *new_elemp);
    new_elemp->list = list;
    new_elemp->orig_port = (*list)->port;
    new_elemp->next = ll_head;
    ll_head = new_elemp;
}

static void
delete_list(ListHandle list)
{
    ll_elem **pp;

    for(pp = &ll_head; *pp && (*pp)->list != list; pp = &(*pp)->next)
        ;
    if(*pp)
    {
        ll_elem *to_freep;

        to_freep = *pp;
        *pp = (*pp)->next;
        free(to_freep);
    }
}

void
check_lists(void)
{
    ll_elem **pp;

    for(pp = &ll_head; *pp; pp = &(*pp)->next)
    {
        GrafPtr gp;

        gp = (*(*pp)->list)->port;
        if(gp != (*pp)->orig_port)
            (*pp)->lastTextProc = gp->grafProcs->textProc;
    }
}
#endif

ListHandle Executor::C_LNew(const Rect *rview, const Rect *bounds, Point csize,
                            INTEGER proc, WindowPtr wind, Boolean draw,
                            Boolean grow, Boolean scrollh,
                            Boolean scrollv) /* IMIV-270 */
{
    ListHandle retval;
    ListPtr lp;
    INTEGER noffs, min, max;
    INTEGER *ip;
    Rect r;
    int i;
    GUEST<DataHandle> tempdatah;
    GUEST<Handle> temph;
    LISTDECL();

    noffs = (bounds->right - bounds->left) * (bounds->bottom - bounds->top) + 1;
    retval = (ListHandle)NewHandle(sizeof(ListRec) - sizeof((*retval)->cellArray) + noffs * sizeof(INTEGER));
    if(!retval)
        /*-->*/ return 0; /* couldn't allocate memory */

    temph = GetResource("LDEF"_4, proc);
    if(!((*retval)->listDefProc = temph))
    {
        DisposeHandle((Handle)retval);
        /*-->*/ return 0; /* spooey list definition proc */
    }

    tempdatah = (DataHandle)NewHandle(0);
    (*retval)->cells = tempdatah;
    HLock((Handle)retval);
    lp = *retval;

    lp->dataBounds = *bounds;
    lp->rView = *rview;
    lp->port = wind;
    lp->indent.h = 0;
    lp->indent.v = 0;
    lp->selFlags = 0;
#if defined(STEF_lActivefix)
    lp->lActive = true;
#else
    lp->lActive = FrontWindow() == wind;
#endif
    lp->lReserved = 0;
    lp->clikTime = 0;
    lp->clikLoc.h = -32768;
    lp->clikLoc.v = -32768;
    lp->mouseLoc.h = -1;
    lp->mouseLoc.v = -1;
    lp->lClikLoop = 0;
    lp->lastClick.h = -1;
    lp->lastClick.v = -1;
    lp->refCon = 0;
    lp->userHandle = nullptr;
    lp->maxIndex = (noffs-1) * 2;
    ip = (INTEGER *)lp->cellArray;
    for(i = 0; i < noffs; i++)
        *ip++ = 0;

    lp->visible.top = bounds->top;
    lp->visible.left = bounds->left;
    lp->vScroll = 0;
    lp->hScroll = 0;
    C_LCellSize(csize, retval); /* sets cellSize and visible */

    lp->listFlags = lMysteryFlags;
    
    if(!draw)
        lp->listFlags |= lDrawingModeOff;
    
    if(grow)
        lp->listFlags |= lGrowBox;

    if(scrollv)
    {
        r = lp->rView;
        r.top = r.top - 1;
        r.left = r.right;
        r.right = r.right + (16);
        r.bottom = r.bottom + 1;
        ROMlib_vminmax(&min, &max, lp);
        lp->vScroll = NewControl((WindowPtr)wind, &r, (StringPtr) "",
                                    draw && lp->lActive, min, min, max, scrollBarProc, (LONGINT)0);
        (*lp->vScroll)->contrlRfCon = guest_cast<LONGINT>(retval);
        lp->listFlags |= lDoVAutoscroll;
    }

    if(scrollh)
    {
        r = lp->rView;
        r.left = r.left - 1;
        r.top = r.bottom;
        r.bottom = r.bottom + (16);
        r.right = r.right + 1;
        ROMlib_hminmax(&min, &max, lp);
        lp->hScroll = NewControl((WindowPtr)wind, &r, (StringPtr) "",
                                    draw && lp->lActive, min, min, max, scrollBarProc, (LONGINT)0);
        (*lp->hScroll)->contrlRfCon = guest_cast<LONGINT>(retval);
        lp->listFlags |= lDoHAutoscroll;
    }

    HUnlock((Handle)retval);
    LISTBEGIN(retval);
    LISTCALL(lInitMsg, false, (Rect *)0, *(Cell *)&lp->clikLoc, 0, 0, retval);
    LISTEND(retval);

#if defined(LIST_DEBUG)
    add_list(retval);
#endif
    return retval;
}

void Executor::C_LDispose(ListHandle list) /* IMIV-271 */
{
    if(list)
    {
        LISTDECL();
        LISTBEGIN(list);
        LISTCALL(lCloseMsg, false, (Rect *)0, *(Cell *)&(*list)->clikLoc, 0,
                 0, list);
        LISTEND(list);

        DisposeHandle((Handle)(*list)->cells);
        if((*list)->hScroll)
            DisposeControl((*list)->hScroll);
        if((*list)->vScroll)
            DisposeControl((*list)->vScroll);
    }
#if defined(LIST_DEBUG)
    delete_list(list);
#endif
}
