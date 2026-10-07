/* Copyright 1986-1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ControlMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>

#include <WindowMgr.h>
#include <ControlMgr.h>

#include <ctl/ctl.h>

using namespace Executor;

void Executor::C_SetControlValue(ControlHandle c, INTEGER v) /* IMI-326 */
{
    CtlCallGuard guard(c);
    if(v < (*c)->contrlMin)
        (*c)->contrlValue = (*c)->contrlMin;
    else if(v > (*c)->contrlMax)
        (*c)->contrlValue = (*c)->contrlMax;
    else
        (*c)->contrlValue = v;
    CTLCALL(c, drawCntl, ALLINDICATORS);

    EM_D0 = 0;
}

INTEGER Executor::C_GetControlValue(ControlHandle c) /* IMI-326 */
{
    return (*c)->contrlValue;
}

void Executor::C_SetControlMinimum(ControlHandle c, INTEGER v) /* IMI-326 */
{
    CtlCallGuard guard(c);
    (*c)->contrlMin = v;
    if((*c)->contrlValue < v)
        (*c)->contrlValue = v;
    CTLCALL(c, drawCntl, ALLINDICATORS);
}

INTEGER Executor::C_GetControlMinimum(ControlHandle c) /* IMI-327 */
{
    return (*c)->contrlMin;
}

void Executor::C_SetControlMaximum(ControlHandle c, INTEGER v) /* IMI-327 */
{
    CtlCallGuard guard(c);
    /* #### TEST ON MAC MacBreadboard's behaviour suggests that
	   this code is needed. */
    if(v < (*c)->contrlMin)
        v = (*c)->contrlMin;

    (*c)->contrlMax = v;
    if((*c)->contrlValue > v)
        (*c)->contrlValue = v;
    CTLCALL(c, drawCntl, ALLINDICATORS);
}

INTEGER Executor::C_GetControlMaximum(ControlHandle c) /* IMI-327 */
{
    return (*c)->contrlMax;
}
