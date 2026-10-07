/* Copyright 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <SysErr.h>
#include <WindowMgr.h>
#include <MenuMgr.h>
#include <rsys/redrawscreen.h>
#include <quickdraw/cquick.h>

using namespace Executor;

void Executor::redraw_screen(void)
{
    TheGDeviceGuard guard(LM(MainDevice));

    if(LM(WWExist) == EXIST_YES)
    {
        WindowPeek frontp;
        frontp = (WindowPeek)FrontWindow();
        if(frontp)
        {
            RgnHandle screen_rgn;
            Rect b;

            b = PIXMAP_BOUNDS(GD_PMAP(LM(MainDevice)));
            screen_rgn = NewRgn();
            RectRgn(screen_rgn, &b);
            PaintBehind(frontp, screen_rgn);
            DisposeRgn(screen_rgn);
        }
    }

    DrawMenuBar();
}
