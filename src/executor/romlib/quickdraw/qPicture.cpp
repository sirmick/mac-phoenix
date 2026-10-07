/* Copyright 1986, 1989, 1990, 1994, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <MemoryMgr.h>

#include <quickdraw/cquick.h>
#include <quickdraw/picture.h>
#include <mman/mman.h>

using namespace Executor;

PicHandle Executor::ROMlib_OpenPicture_helper(
    const Rect *pf, const OpenCPicParams *params)
{
    piccachehand pch;
    PicHandle ph;
    GUEST<INTEGER> *ip;
    GUEST<RgnHandle> temprh;

    HidePen();
    pch = (piccachehand)NewHandle(sizeof(piccache));
    PORT_PIC_SAVE(qdGlobals().thePort) = (Handle)pch;
    ph = (PicHandle)NewHandle((Size)INITIALPICSIZE);
    (*pch)->pichandle = ph;

    (*ph)->picSize = 10 + 16 * sizeof(INTEGER);
    (*ph)->picFrame = *pf;
    ip = &(*ph)->picSize + 5;
    ip[0] = OP_Version;
    ip[1] = 0x2FF; /* see the explanation in IM-V p. 93 */

    ip[2] = 0x0c00; /* see IM:Imaging with QuickDraw A-22 - A-24 */
    if(params)
    {
        ip[3] = params->version;
        ip[4] = params->reserved1;
        *(GUEST<uint32_t> *)&ip[5] = params->hRes;
        *(GUEST<uint32_t> *)&ip[7] = params->vRes;
        memcpy(&ip[9], &params->srcRect, sizeof params->srcRect);
        *(GUEST<uint32_t> *)&ip[13] = params->reserved2;
    }
    else
    {
        ip[3] = 0xffff;
        ip[4] = 0xffff;
        ip[5] = pf->left;
        ip[6] = 0x0000;
        ip[7] = pf->top;
        ip[8] = 0x0000;
        ip[9] = pf->right;
        ip[10] = 0x0000;
        ip[11] = pf->bottom;
        ip[12] = 0x0000;
        ip[13] = 0x0000;
        ip[14] = 0x0000;
    }

    ip[15] = 0x001e;

    (*pch)->picsize = INITIALPICSIZE;
    (*pch)->pichowfar = (LONGINT)(*ph)->picSize;
    temprh = NewRgn();
    (*pch)->picclip = temprh;
    /* -32766 below is an attempt to force a reload */
    SetRectRgn((*pch)->picclip, -32766, -32766, 32767, 32767);
    (*pch)->picbkpat = qdGlobals().white;
    (*pch)->picfont = 0;
    (*pch)->picface = 0;
    (*pch)->picfiller = 0;
    (*pch)->pictxmode = srcOr;
    (*pch)->pictxsize = 0;
    (*pch)->picspextra = 0;
    (*pch)->pictxnum.h = 1;
    (*pch)->pictxnum.v = 1;
    (*pch)->pictxden.h = 1;
    (*pch)->pictxden.v = 1;
    (*pch)->picdrawpnloc.h = 0;
    (*pch)->picdrawpnloc.v = 0;
    (*pch)->pictextpnloc.h = 0;
    (*pch)->pictextpnloc.v = 0;
    (*pch)->picpnsize.h = 1;
    (*pch)->picpnsize.v = 1;
    (*pch)->picpnmode = patCopy;
    (*pch)->picpnpat = qdGlobals().black;
    (*pch)->picfillpat = qdGlobals().black;
    (*pch)->piclastrect.top = 0;
    (*pch)->piclastrect.left = 0;
    (*pch)->piclastrect.bottom = 0;
    (*pch)->piclastrect.right = 0;
    (*pch)->picov.v = 0;
    (*pch)->picov.h = 0;
    (*pch)->picidunno = 0;
    (*pch)->picforeColor = blackColor;
    (*pch)->picbackColor = whiteColor;
    return (ph);
}

PicHandle Executor::C_OpenPicture(const Rect *pf)
{
    PicHandle retval;

    retval = ROMlib_OpenPicture_helper(pf, nullptr);
    return retval;
}

static void updateclip(void)
{
    piccachehand pch;
    SignedByte state;

    pch = (piccachehand)PORT_PIC_SAVE(qdGlobals().thePort);
    if(!EqualRgn(PORT_CLIP_REGION(qdGlobals().thePort), (*pch)->picclip))
    {
        CopyRgn(PORT_CLIP_REGION(qdGlobals().thePort), (*pch)->picclip);
        PICOP(OP_Clip);
        state = HGetState((Handle)PORT_CLIP_REGION(qdGlobals().thePort));
        HLock((Handle)PORT_CLIP_REGION(qdGlobals().thePort));
        PICWRITE(*PORT_CLIP_REGION(qdGlobals().thePort),
                 (*PORT_CLIP_REGION(qdGlobals().thePort))->rgnSize);
        HSetState((Handle)PORT_CLIP_REGION(qdGlobals().thePort), state);
    }
}

static Boolean EqualPat(Pattern pat1, Pattern pat2)
{
    LONGINT *lp1, *lp2;

    lp1 = (LONGINT *)pat1.pat;
    lp2 = (LONGINT *)pat2.pat;
    return lp1[0] == lp2[0] && lp1[1] == lp2[1];
}

static void updateapat(Pattern srcpat, Pattern dstpat, INTEGER opcode)
{
    if(!EqualPat(srcpat, dstpat))
    {
        dstpat = srcpat;
        PICOP(opcode);
        PICWRITE(&srcpat, sizeof(Pattern));
    }
}

static void updateaninteger(INTEGER src, GUEST<INTEGER> *dstp, INTEGER opcode)
{
    GUEST<INTEGER> gsrc = src;
    if(*dstp != gsrc)
    {
        *dstp = gsrc;
        PICOP(opcode);
        PICWRITE(&gsrc, sizeof(INTEGER));
    }
}

static void updatealongint(const GUEST<LONGINT> *srcp, GUEST<LONGINT> *dstp,
                           INTEGER opcode)
{
    if(*dstp != *srcp)
    {
        *dstp = *srcp;
        PICOP(opcode);
        PICWRITE(srcp, sizeof(LONGINT));
    }
}

static void updatebkpat(void)
{
    piccachehand pch;

    pch = (piccachehand)PORT_PIC_SAVE(qdGlobals().thePort);
    /* #warning Questionable code in updatebkpat */
    /* FIXME */
    if(CGrafPort_p(qdGlobals().thePort))
        updateapat(PIXPAT_1DATA(CPORT_BK_PIXPAT(qdGlobals().thePort)),
                   (*pch)->picbkpat, OP_BkPat);
    else
        updateapat(PORT_BK_PAT(qdGlobals().thePort), (*pch)->picbkpat, OP_BkPat);
}

static void updatepnpat(void)
{
    piccachehand pch;

    pch = (piccachehand)qdGlobals().thePort->picSave;
    /* #warning Questionable code in updatepnpat */
    /* FIXME */
    if(CGrafPort_p(qdGlobals().thePort))
        updateapat(PIXPAT_1DATA(CPORT_PEN_PIXPAT(qdGlobals().thePort)),
                   (*pch)->picpnpat, OP_PnPat);
    else
        updateapat(PORT_PEN_PAT(qdGlobals().thePort), (*pch)->picpnpat, OP_PnPat);
}

static void updatefillpat(void)
{
    piccachehand pch;

    pch = (piccachehand)qdGlobals().thePort->picSave;
    if(CGrafPort_p(qdGlobals().thePort))
        /* #warning Questionable code in updatefillpat */
        updateapat(PIXPAT_1DATA(CPORT_FILL_PIXPAT(qdGlobals().thePort)),
                   (*pch)->picfillpat, OP_FillPat);
    else
        updateapat(PORT_FILL_PAT(qdGlobals().thePort), (*pch)->picfillpat, OP_FillPat);
}

static void updatefont(void)
{
    piccachehand pch;

    pch = (piccachehand)PORT_PIC_SAVE(qdGlobals().thePort);
    updateaninteger(PORT_TX_FONT(qdGlobals().thePort), &(*pch)->picfont, OP_TxFont);
}

static void updatetxmode(void)
{
    piccachehand pch;

    pch = (piccachehand)PORT_PIC_SAVE(qdGlobals().thePort);
    updateaninteger(PORT_TX_MODE(qdGlobals().thePort), &(*pch)->pictxmode, OP_TxMode);
}

static void updatetxsize(void)
{
    piccachehand pch;

    pch = (piccachehand)PORT_PIC_SAVE(qdGlobals().thePort);
    updateaninteger(PORT_TX_SIZE(qdGlobals().thePort), &(*pch)->pictxsize, OP_TxSize);
}

static void updatepnmode(void)
{
    piccachehand pch;

    pch = (piccachehand)PORT_PIC_SAVE(qdGlobals().thePort);
    updateaninteger(PORT_PEN_MODE(qdGlobals().thePort), &(*pch)->picpnmode, OP_PnMode);
}

static void updateforeColor(void)
{
    piccachehand pch;

    pch = (piccachehand)PORT_PIC_SAVE(qdGlobals().thePort);
    if(CGrafPort_p(qdGlobals().thePort))
    {
        /* ### this isn't quite correct since we only want to write the
	 current color if it has changed.  that requires we record the
	 current color, but the current `piccache' doesn't contain
	 that info */
        PICOP(OP_RGBFgCol);
        PICWRITE(&CPORT_RGB_FG_COLOR(qdGlobals().thePort),
                 sizeof CPORT_RGB_FG_COLOR(qdGlobals().thePort));
    }
    else
        updatealongint(&PORT_FG_COLOR(qdGlobals().thePort), &(*pch)->picforeColor,
                       OP_FgColor);
}

static void updatebackColor(void)
{
    piccachehand pch;

    pch = (piccachehand)PORT_PIC_SAVE(qdGlobals().thePort);
    if(CGrafPort_p(qdGlobals().thePort))
    {
        /* ### see comment in `updateforeColor ()' */
        PICOP(OP_RGBBkCol);
        PICWRITE(&CPORT_RGB_BK_COLOR(qdGlobals().thePort),
                 sizeof CPORT_RGB_BK_COLOR(qdGlobals().thePort));
    }
    else
        updatealongint(&PORT_BK_COLOR(qdGlobals().thePort),
                       &(*pch)->picbackColor,
                       OP_BkColor);
}

static void updatespextra(void)
{
    piccachehand pch;

    pch = (piccachehand)PORT_PIC_SAVE(qdGlobals().thePort);
    updatealongint(&PORT_SP_EXTRA(qdGlobals().thePort), &(*pch)->picspextra, OP_SpExtra);
}

static void updatetxnumtxden(Point num, Point den)
{
    piccachehand pch;

    pch = (piccachehand)PORT_PIC_SAVE(qdGlobals().thePort);
    if((*pch)->pictxnum.h != num.h || (*pch)->pictxnum.v != num.v || (*pch)->pictxden.v != den.v || (*pch)->pictxden.h != den.h)
    {
        (*pch)->pictxnum.h = num.h;
        (*pch)->pictxnum.v = num.v;
        (*pch)->pictxden.h = den.h;
        (*pch)->pictxden.v = den.v;
        PICOP(OP_TxRatio);
        GUEST<Point> tmpP;
        tmpP.set(num);
        PICWRITE(&tmpP, sizeof(tmpP));
        tmpP.set(den);
        PICWRITE(&tmpP, sizeof(tmpP));
    }
}

static void updatepnsize(void)
{
    piccachehand pch;

    pch = (piccachehand)PORT_PIC_SAVE(qdGlobals().thePort);
    updatealongint((GUEST<LONGINT> *)&qdGlobals().thePort->pnSize,
                   (GUEST<LONGINT> *)&(*pch)->picpnsize, OP_PnSize);
}

static void updatetxface(void)
{
    piccachehand pch;
    Style f;

    pch = (piccachehand)PORT_PIC_SAVE(qdGlobals().thePort);
    if((*pch)->picface != PORT_TX_FACE(qdGlobals().thePort))
    {
        (*pch)->picface = PORT_TX_FACE(qdGlobals().thePort);
        f = PORT_TX_FACE(qdGlobals().thePort);
        PICOP(OP_TxFace);
        PICWRITE(&f, sizeof(f));
        if(sizeof(f) & 1)
            PICWRITE("", 1); /* even things up */
    }
}

static void updateoval(GUEST<Point> *ovp)
{
    piccachehand pch;

    pch = (piccachehand)PORT_PIC_SAVE(qdGlobals().thePort);
    if((*pch)->picov.h != ovp->h || (*pch)->picov.v != ovp->v)
    {
        (*pch)->picov = *ovp;
        PICOP(OP_OvSize);
        PICWRITE(ovp, sizeof(*ovp));
    }
}

void Executor::ROMlib_textpicupdate(Point num, Point den)
{
    updateclip();
    updatefont();
    updatetxface();
    updatetxmode();
    updatetxsize();
    updatespextra();
    updatetxnumtxden(num, den);
    updateforeColor();
    updatebackColor();
}

void Executor::ROMlib_drawingpicupdate(void)
{
    updateclip();
    updatepnsize();
    updatepnmode();
    updatepnpat();
    updateforeColor();
    updatebackColor();
}

void Executor::ROMlib_drawingverbpicupdate(GrafVerb v)
{
    ROMlib_drawingpicupdate();
    switch(v)
    {
        case erase:
            updatebkpat();
            break;
        case fill:
            updatefillpat();
            break;
        case invert:
            if(!(LM(HiliteMode) & 0x80))
            {
                PICOP(OP_HiliteColor);
                PICWRITE(&LM(HiliteRGB), sizeof LM(HiliteRGB));
                PICOP(OP_HiliteMode);
            }
            break;
        default:
            break;
    }
}

void Executor::ROMlib_drawingverbrectpicupdate(GrafVerb v, const Rect *rp)
{
    piccachehand pch;

    ROMlib_drawingverbpicupdate(v);
    pch = (piccachehand)PORT_PIC_SAVE(qdGlobals().thePort);
    (*pch)->piclastrect = *rp; /* currently unused */
}

void Executor::ROMlib_drawingverbrectovalpicupdate(
    GrafVerb v, const Rect *rp, GUEST<Point> *ovp)
{
    ROMlib_drawingverbrectpicupdate(v, rp);
    updateoval(ovp);
}

void Executor::C_ClosePicture()
{
    piccachehand pch;

    if((pch = (piccachehand)PORT_PIC_SAVE(qdGlobals().thePort)))
    {
        PicHandle ph;

        PICOP(OP_EndPic);
        ph = (*pch)->pichandle;
        SetHandleSize((Handle)ph, (*pch)->pichowfar);
        DisposeRgn((*pch)->picclip);
        DisposeHandle((Handle)pch);
        PORT_PIC_SAVE(qdGlobals().thePort) = nullptr;
        ShowPen();
    }
}

void Executor::C_PicComment(INTEGER kind, INTEGER size, Handle hand)
{
    CALLCOMMENT(kind, size, hand);
}

/* look in qPicStuff.c for DrawPicture */

void Executor::C_KillPicture(PicHandle pic)
{
    /*
 * It's not clear what the Mac does in the case below.  We really should
 * test exactly how the Mac deals with DisposeHandle being called on resource
 * handles.
 */
    SignedByte state;

    state = HGetState((Handle)pic);
    if(!(state & RSRCBIT))
        DisposeHandle((Handle)pic);
}
