/* Copyright 1986, 1988, 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>

using namespace Executor;

void Executor::C_SetStdProcs(QDProcs *procs)
{
    procs->textProc = &StdText;
    procs->lineProc = &StdLine;
    procs->rectProc = &StdRect;
    procs->rRectProc = &StdRRect;
    procs->ovalProc = &StdOval;
    procs->arcProc = &StdArc;
    procs->polyProc = &StdPoly;
    procs->rgnProc = &StdRgn;
    procs->bitsProc = &StdBits;
    procs->commentProc = &StdComment;
    procs->txMeasProc = &StdTxMeas;
    procs->getPicProc = &StdGetPic;
    procs->putPicProc = &StdPutPic;
}
