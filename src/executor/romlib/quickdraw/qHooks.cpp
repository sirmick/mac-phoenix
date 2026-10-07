/* Copyright 1994 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <QuickDraw.h>
#include <quickdraw/quick.h>
#include <rsys/hook.h>
#include <print/print.h>
#include <prefs/options.h>
#include <base/functions.impl.h>

using namespace Executor;


const bool ROMlib_text_disable_hack = false;
static bool text_is_enabled_p = true;

void
Executor::disable_stdtext(void)
{
    if(ROMlib_text_disable_hack)
        text_is_enabled_p = false;
}

void
Executor::enable_stdtext(void)
{
    text_is_enabled_p = true;
}

void Executor::ROMlib_CALLTEXT(INTEGER bc, Ptr bufp, Point num, Point den)
{
    QDProcsPtr gp;
    QDTextUPP pp;

    if(text_is_enabled_p)
    {
        if((gp = qdGlobals().thePort->grafProcs)
           && (pp = gp->textProc) != &StdText)
        {
            ROMlib_hook(q_textprocnumber);
            pp(bc, bufp, num, den);
        }
        else
            C_StdText(bc, bufp, num, den);
    }
}

void Executor::ROMlib_CALLLINE(Point p)
{
    QDProcsPtr gp;
    QDLineUPP pp;

    if((gp = qdGlobals().thePort->grafProcs)
       && (pp = gp->lineProc) != &StdLine)
    {
        ROMlib_hook(q_lineprocnumber);
        pp(p);
    }
    else
        C_StdLine(p);
}

void Executor::ROMlib_CALLRECT(GrafVerb v, const Rect *rp)
{
    QDProcsPtr gp;
    QDRectUPP pp;

    if((gp = qdGlobals().thePort->grafProcs)
       && (pp = gp->rectProc) != &StdRect)
    {
        ROMlib_hook(q_rectprocnumber);
        pp(v, rp);
    }
    else
        C_StdRect(v, rp);
}

void Executor::ROMlib_CALLOVAL(GrafVerb v, const Rect *rp)
{
    QDProcsPtr gp;
    QDOvalUPP pp;

    if((gp = qdGlobals().thePort->grafProcs)
       && (pp = gp->ovalProc) != &StdOval)
    {
        ROMlib_hook(q_ovalprocnumber);
        pp(v, rp);
    }
    else
        C_StdOval(v, rp);
}

void Executor::ROMlib_CALLRRECT(GrafVerb v, const Rect *rp, INTEGER ow, INTEGER oh)
{
    QDProcsPtr gp;
    QDRRectUPP pp;

    if((gp = qdGlobals().thePort->grafProcs)
       && (pp = gp->rRectProc) != &StdRRect)
    {
        ROMlib_hook(q_rrectprocnumber);
        pp(v, rp, ow, oh);
    }
    else
        C_StdRRect(v, rp, ow, oh);
}

void Executor::ROMlib_CALLARC(GrafVerb v, const Rect *rp, INTEGER starta, INTEGER arca)
{
    QDProcsPtr gp;
    QDArcUPP pp;

    if((gp = qdGlobals().thePort->grafProcs)
       && (pp = gp->arcProc) != &StdArc)
    {
        ROMlib_hook(q_arcprocnumber);
        pp(v, rp, starta, arca);
    }
    else
        C_StdArc(v, rp, starta, arca);
}

void Executor::ROMlib_CALLRGN(GrafVerb v, RgnHandle rh)
{
    QDProcsPtr gp;
    QDRgnUPP pp;

    if((gp = qdGlobals().thePort->grafProcs)
       && (pp = gp->rgnProc) != &StdRgn)
    {
        ROMlib_hook(q_rgnprocnumber);
        pp(v, rh);
    }
    else
        C_StdRgn(v, rh);
}

void Executor::ROMlib_CALLPOLY(GrafVerb v, PolyHandle rh)
{
    QDProcsPtr gp;
    QDPolyUPP pp;

    if((gp = qdGlobals().thePort->grafProcs)
       && (pp = gp->polyProc) != &StdPoly)
    {
        ROMlib_hook(q_polyprocnumber);
        pp(v, rh);
    }
    else
        C_StdPoly(v, rh);
}

void Executor::ROMlib_CALLBITS(BitMap *bmp, const Rect *srcrp, const Rect *dstrp,
                               INTEGER mode, RgnHandle maskrh)
{
    QDProcsPtr gp;
    QDBitsUPP pp;

    if((gp = qdGlobals().thePort->grafProcs)
       && (pp = gp->bitsProc) != &StdBits)
    {
        ROMlib_hook(q_bitsprocnumber);
        pp(bmp, srcrp, dstrp, mode, maskrh);
    }
    else
        C_StdBits(bmp, srcrp, dstrp, mode, maskrh);
}

void Executor::ROMlib_CALLCOMMENT(INTEGER kind, INTEGER size, Handle datah)
{
    QDProcsPtr gp;
    QDCommentUPP pp;

    if((gp = qdGlobals().thePort->grafProcs)
       && (pp = gp->commentProc) != &StdComment)
    {
        ROMlib_hook(q_commentprocnumber);
        pp(kind, size, datah);
    }
    else
        C_StdComment(kind, size, datah);
}

INTEGER
Executor::ROMlib_CALLTXMEAS(INTEGER bc, Ptr bufp, GUEST<Point> *nump, GUEST<Point> *denp,
                            FontInfo *fip)
{
    QDProcsPtr gp;
    QDTexMeasUPP pp;
    INTEGER retval;

    if((gp = qdGlobals().thePort->grafProcs)
       && (pp = gp->txMeasProc) != &StdTxMeas)
    {
        ROMlib_hook(q_txmeasprocnumber);
        retval = pp(bc, bufp, nump, denp, fip);
    }
    else
        retval = C_StdTxMeas(bc, bufp, nump, denp, fip);
    return retval;
}

void Executor::ROMlib_PICWRITE(Ptr addr, INTEGER count)
{
    QDProcsPtr gp;
    QDPutPicUPP pp;

    if((gp = qdGlobals().thePort->grafProcs)
       && (pp = gp->putPicProc) != &StdPutPic)
    {
        ROMlib_hook(q_putpicprocnumber);
        pp(addr, count);
    }
    else
        C_StdPutPic(addr, count);
}

