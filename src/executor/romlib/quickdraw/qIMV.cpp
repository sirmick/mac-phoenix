/* Copyright 1989, 1990, 1994, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <MemoryMgr.h>

#include <quickdraw/cquick.h>
#include <mman/tempalloc.h>

using namespace Executor;

/* these stubs are here to make my Pic V2 code work */

void Executor::C_CharExtra(Fixed Extra) /* IMV-77 */
{
    /* TODO */
    /* #warning CharExtra not implemented */
    warning_unimplemented("");
}

void Executor::C_SetStdCProcs(CQDProcs *cProcs) /* IMV-77 */
{
    cProcs->textProc = &StdText;
    cProcs->lineProc = &StdLine;
    cProcs->rectProc = &StdRect;
    cProcs->rRectProc = &StdRRect;
    cProcs->ovalProc = &StdOval;
    cProcs->arcProc = &StdArc;
    cProcs->polyProc = &StdPoly;
    cProcs->rgnProc = &StdRgn;
    cProcs->bitsProc = &StdBits;
    cProcs->commentProc = &StdComment;
    cProcs->txMeasProc = &StdTxMeas;
    cProcs->getPicProc = &StdGetPic;
    cProcs->putPicProc = &StdPutPic;
    cProcs->opcodeProc = nullptr /* ??? */;
    cProcs->newProc1Proc = nullptr /* ??? */;
    cProcs->newProc2Proc = nullptr /* ??? */;
    cProcs->newProc3Proc = nullptr /* ??? */;
    cProcs->newProc4Proc = nullptr /* ??? */;
    cProcs->newProc5Proc = nullptr /* ??? */;
    cProcs->newProc6Proc = nullptr /* ??? */;
}

void Executor::C_GetCPixel(INTEGER h, INTEGER v, RGBColor *pixelp)
{
    PixMap temp_pm;
    uint8_t temp_fbuf[4];
    Rect src_rect, dst_rect;
    GrafPtr port = qdGlobals().thePort;
    CTabHandle ctab;
    ColorSpec *cspec;
    int pixval;
    int bpp;

    temp_pm.baseAddr = (Ptr)temp_fbuf;
    temp_pm.bounds.top = 0;
    temp_pm.bounds.bottom = 1;
    temp_pm.bounds.left = 0;
    temp_pm.bounds.right = 1;
    temp_pm.rowBytes = static_cast<int16_t>(4 | PIXMAP_DEFAULT_ROW_BYTES);

    if(CGrafPort_p(port))
    {
        PixMapHandle port_pm = CPORT_PIXMAP(port);

        bpp = PIXMAP_PIXEL_SIZE(port_pm);
        ctab = PIXMAP_TABLE(port_pm);
    }
    else
    {
        bpp = 1;
        ctab = validate_relative_bw_ctab();
    }
    temp_pm.pmTable = ctab;
    pixmap_set_pixel_fields(&temp_pm, bpp);

    src_rect.top = v;
    src_rect.bottom = v + 1;
    src_rect.left = h;
    src_rect.right = h + 1;

    dst_rect = temp_pm.bounds;

    CopyBits(PORT_BITS_FOR_COPY(qdGlobals().thePort), (BitMap *)&temp_pm,
             &src_rect, &dst_rect, srcCopy, nullptr);

    if(bpp > 8)
    {
        gui_fatal("> 8bpp unimplemented");
    }
    else
    {
        /* extract the pixel */
        pixval = (*temp_fbuf >> (8 - bpp));

        /* Convert that pixel to an RGB value. */
        cspec = CTAB_TABLE(ctab);
        if(CTAB_FLAGS(ctab) & CTAB_GDEVICE_BIT)
            *pixelp = cspec[pixval].rgb;
        else
        {
            GUEST<int16_t> swapped_pixval;
            int i;

            /* non-device color tables aren't guaranteed to be sorted, so we
	     need to hunt for an entry with the specified value */
            swapped_pixval = pixval;
            for(i = CTAB_SIZE(ctab); i >= 0; i--)
                if(cspec[i].value == swapped_pixval)
                    break;
            if(i >= 0)
                *pixelp = cspec[i].rgb;
            else
            {
                warning_unexpected("Failed to find match in GetCPixel.");
                pixelp->red = pixelp->green = pixelp->blue = 0;
            }
        }
    }
}

void Executor::C_SetCPixel(INTEGER h, INTEGER v, RGBColor *pixelp)
{
    Rect temp_rect;

    GrafPtr port;
    bool cgrafport_p;

    RGBColor save_fg_rgb;
    GUEST<int32_t> save_fg;

    temp_rect.top = v;
    temp_rect.bottom = v + 1;
    temp_rect.left = h;
    temp_rect.right = h + 1;

    port = qdGlobals().thePort;
    cgrafport_p = CGrafPort_p(port);

    save_fg = PORT_FG_COLOR(port);
    if(cgrafport_p)
        save_fg_rgb = CPORT_RGB_FG_COLOR(port);

    RGBForeColor(pixelp);
    PenMode(patCopy);

    FillRect(&temp_rect, &qdGlobals().black);

    PORT_FG_COLOR(port) = save_fg;
    if(cgrafport_p)
        CPORT_RGB_FG_COLOR(port) = save_fg_rgb;
}

static int8_t
default_search_proc(RGBColor *rgb, GUEST<int32_t> *pixel)
{
    MatchRec *mr;

    mr = ptr_from_longint<MatchRec *>(GD_REF_CON(LM(TheGDevice)));

    if(mr->red == rgb->red
       && mr->green == rgb->green
       && mr->blue == rgb->blue)
        *pixel = mr->matchData;
    else
        *pixel = !mr->matchData;
    return true;
}

syn68k_addr_t
default_search_proc_stub(syn68k_addr_t dummy_addr, void *dummy)
{
    void *arg1, *arg2;
    syn68k_addr_t retval;
    int8_t result;

    retval = POPADDR();

    arg2 = (void *)SYN68K_TO_US(POPADDR());
    arg1 = (void *)SYN68K_TO_US(POPADDR());

    result = default_search_proc((RGBColor *)arg1, (GUEST<int32_t> *)arg2);
    WRITEUB(EM_A7, result);

    return retval;
}

void Executor::C_SeedCFill(BitMap *srcbp, BitMap *dstbp, const Rect *srcrp,
                           const Rect *dstrp, int16_t seedh, int16_t seedv,
                           ProcPtr matchprocp, int32_t matchdata)
{
    MatchRec mr;
    GUEST<LONGINT> save_ref_con;
    GUEST<Handle> save_pic_handle;
    GUEST<QDProcsPtr> save_graf_procs;
    GDHandle gdev;
    RGBColor pixel;
    BitMap temp_bitmap1, temp_bitmap2;
    Rect temp_rect;
    int row_words;
    int width, height;
    void *t;

    TEMP_ALLOC_DECL(temp_bitmap1_bits);
    TEMP_ALLOC_DECL(temp_bitmap2_bits);

    gdev = LM(TheGDevice);

    if(!matchprocp)
    {
        matchprocp = (ProcPtr)SYN68K_TO_US(callback_install(default_search_proc_stub, nullptr));
        mr.matchData = 0;
    }
    else
    {
        mr.matchData = matchdata;
    }

    GetCPixel(seedh, seedv, &pixel);

    mr.red = pixel.red;
    mr.green = pixel.green;
    mr.blue = pixel.blue;
    save_ref_con = GD_REF_CON(gdev);
    GD_REF_CON(gdev) = guest_cast<int32_t>(&mr);

    save_pic_handle = PORT_PIC_SAVE(qdGlobals().thePort);
    save_graf_procs = PORT_GRAF_PROCS(qdGlobals().thePort);

    PORT_PIC_SAVE(qdGlobals().thePort) = nullptr;
    PORT_GRAF_PROCS(qdGlobals().thePort) = nullptr;
    GD_SEARCH_PROC(gdev) = nullptr;
    AddSearch(matchprocp);

    width = RECT_WIDTH(srcrp);
    height = RECT_HEIGHT(srcrp);

    temp_rect.top = temp_rect.left = 0;
    temp_rect.right = width;
    temp_rect.bottom = height;

    row_words = (width + 15) / 16;
    temp_bitmap1.rowBytes = row_words * 2;
    TEMP_ALLOC_ALLOCATE(t, temp_bitmap1_bits, row_words * 2 * height);
    temp_bitmap1.baseAddr = (Ptr)t;
    memset(temp_bitmap1.baseAddr, '\377', row_words * 2 * height);
    temp_bitmap1.bounds = temp_rect;

    CopyBits(srcbp, &temp_bitmap1, srcrp, &temp_rect, srcCopy, nullptr);

    DelSearch(matchprocp);
    GD_REF_CON(gdev) = save_ref_con;

    temp_bitmap2 = temp_bitmap1;
    TEMP_ALLOC_ALLOCATE(t, temp_bitmap2_bits, row_words * 2 * height);
    temp_bitmap2.baseAddr = (Ptr)t;

    SeedFill(temp_bitmap1.baseAddr,
             temp_bitmap2.baseAddr,
             row_words * 2, row_words * 2,
             height, row_words, seedh, seedv);

    CopyBits(&temp_bitmap2, dstbp, &temp_rect, dstrp, srcCopy, nullptr);

    PORT_PIC_SAVE(qdGlobals().thePort) = save_pic_handle;
    PORT_GRAF_PROCS(qdGlobals().thePort) = save_graf_procs;

    TEMP_ALLOC_FREE(temp_bitmap1_bits);
    TEMP_ALLOC_FREE(temp_bitmap2_bits);
}

void Executor::C_CalcCMask(BitMap *srcbp, BitMap *dstbp, const Rect *srcrp,
                           const Rect *dstrp, RGBColor *seedrgbp, ProcPtr matchprocp,
                           int32_t matchdata)
{
    MatchRec mr;
    GUEST<LONGINT> save_ref_con;
    GUEST<Handle> save_pic_handle;
    GUEST<QDProcsPtr> save_graf_procs;
    GDHandle gdev;
    BitMap temp_bitmap1, temp_bitmap2;
    Rect temp_rect;
    int row_words;
    int width, height;
    void *t;

    TEMP_ALLOC_DECL(temp_bitmap1_bits);
    TEMP_ALLOC_DECL(temp_bitmap2_bits);

    gdev = LM(TheGDevice);

    if(!matchprocp)
    {
        matchprocp = (ProcPtr)SYN68K_TO_US(callback_install(default_search_proc_stub, nullptr));
        mr.matchData = 1;
    }
    else
    {
        mr.matchData = matchdata;
    }

    mr.red = seedrgbp->red;
    mr.green = seedrgbp->green;
    mr.blue = seedrgbp->blue;
    save_ref_con = GD_REF_CON(gdev);
    GD_REF_CON(gdev) = guest_cast<int32_t>(&mr);

    save_pic_handle = PORT_PIC_SAVE(qdGlobals().thePort);
    save_graf_procs = PORT_GRAF_PROCS(qdGlobals().thePort);

    PORT_PIC_SAVE(qdGlobals().thePort) = nullptr;
    PORT_GRAF_PROCS(qdGlobals().thePort) = nullptr;
    GD_SEARCH_PROC(gdev) = nullptr;
    AddSearch(matchprocp);

    width = RECT_WIDTH(srcrp);
    height = RECT_HEIGHT(srcrp);

    temp_rect.top = temp_rect.left = 0;
    temp_rect.right = width;
    temp_rect.bottom = height;

    row_words = (width + 15) / 16;
    temp_bitmap1.rowBytes = row_words * 2;
    TEMP_ALLOC_ALLOCATE(t, temp_bitmap1_bits, row_words * 2 * height);
    temp_bitmap1.baseAddr = (Ptr)t;
    memset(temp_bitmap1.baseAddr, '\377', row_words * 2 * height);
    temp_bitmap1.bounds = temp_rect;

    CopyBits(srcbp, &temp_bitmap1, srcrp, &temp_rect, srcCopy, nullptr);

    DelSearch(matchprocp);
    GD_REF_CON(gdev) = save_ref_con;

    temp_bitmap2 = temp_bitmap1;
    TEMP_ALLOC_ALLOCATE(t, temp_bitmap2_bits, row_words * 2 * height);
    temp_bitmap2.baseAddr = (Ptr)t;

    CalcMask(temp_bitmap1.baseAddr,
             temp_bitmap2.baseAddr,
             row_words * 2, row_words * 2,
             height, row_words);

    CopyBits(&temp_bitmap2, dstbp, &temp_rect, dstrp, srcCopy, nullptr);

    PORT_PIC_SAVE(qdGlobals().thePort) = save_pic_handle;
    PORT_GRAF_PROCS(qdGlobals().thePort) = save_graf_procs;

    TEMP_ALLOC_FREE(temp_bitmap1_bits);
    TEMP_ALLOC_FREE(temp_bitmap2_bits);
}
