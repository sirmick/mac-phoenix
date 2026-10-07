/* Copyright 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <ListMgr.h>
#include <MemoryMgr.h>
#include <list/list.h>

using namespace Executor;

static void draw(Boolean sel, Rect *rect, INTEGER doff, INTEGER dl,
                 ListHandle list)
{
    GrafPtr savePort;

    savePort = qdGlobals().thePort;
    SetPort((*list)->port);
    EraseRect(rect);
    MoveTo(rect->left + (*list)->indent.h, rect->top + (*list)->indent.v);
    HLock((Handle)(*list)->cells);
    DrawText((Ptr)*(*list)->cells + doff, 0, dl);
    HUnlock((Handle)(*list)->cells);
    if(sel)
        InvertRect(rect);
    SetPort(savePort);
}

void Executor::C_ldef0(INTEGER msg, Boolean sel, Rect *rect, Cell cell,
                       INTEGER doff, INTEGER dl, ListHandle list) /* IMIV-276 */
{
    GrafPtr savePort;
    FontInfo fi;

    switch(msg)
    {
        case lInitMsg:
            savePort = qdGlobals().thePort;
            SetPort((*list)->port);
            GetFontInfo(&fi);
            (*list)->indent.h = 5;
            (*list)->indent.v = fi.ascent;
            SetPort(savePort);
            break;
        case lDrawMsg:
            draw(sel, rect, doff, dl, list);
            break;
        case lHiliteMsg:
            savePort = qdGlobals().thePort;
            SetPort((*list)->port);
            InvertRect(rect);
            SetPort(savePort);
            break;
        case lCloseMsg: /* nothing special to do */
            break;
        default: /* weirdness */
            break;
    }
}
