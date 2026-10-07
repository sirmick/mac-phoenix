/* Copyright 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ControlMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>

#include <ControlMgr.h>

#include <ctl/ctl.h>
#include <wind/wind.h>

using namespace Executor;

void Executor::C_Draw1Control(ControlHandle c) /* IMIV-53 */
{
    if(CTL_VIS(c))
    {
        CtlCallGuard guard(c);

        CTLCALL(c, drawCntl, 0);
    }
}

void Executor::C_UpdateControls(WindowPtr wp, RgnHandle rh) /* IMIV-53 */
{
    ControlHandle c;

    if(!ROMlib_emptyvis)
    {
        for(c = WINDOW_CONTROL_LIST(wp); c; c = CTL_NEXT_CONTROL(c))
            if(RectInRgn(&CTL_RECT(c), rh))
                Draw1Control(c);
    }
}
