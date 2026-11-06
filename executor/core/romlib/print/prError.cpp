/* Copyright 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in PrintMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <PrintMgr.h>

using namespace Executor;

INTEGER Executor::C_PrError()
{
    return LM(PrintErr);
}

void Executor::C_PrSetError(INTEGER iErr)
{
    LM(PrintErr) = iErr;
}
