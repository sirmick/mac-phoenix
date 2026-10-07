/* Copyright 1994, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <MemoryMgr.h>
#include <ResourceMgr.h>

#include <quickdraw/quick.h>
#include <quickdraw/cquick.h>
#include <mman/mman.h>

#include <quickdraw/xdata.h>
#include <rsys/evil.h>

#include <util/handle_vector.h>

using namespace Executor;

void Executor::C_OpenCPort(CGrafPtr port)
{
    PixPatHandle temp_pixpat;

    /* set up port version before using any other macros */
    port->portVersion = static_cast<short>((3 << 14) | /* color quickdraw version */ 0);

    /* allocate storage for new CGrafPtr members, including portPixMap,
     pnPixPat, fillPixPat, bkPixPat, and grafVar */
    CPORT_PIXMAP(port) = NewPixMap();

    /* Free up the empty color table, since we're not going to use it. */
    DisposeHandle((Handle)PIXMAP_TABLE(CPORT_PIXMAP(port)));

    temp_pixpat = NewPixPat();
    PIXPAT_TYPE(temp_pixpat) = pixpat_type_orig;
    CPORT_PEN_PIXPAT(port) = temp_pixpat;

    temp_pixpat = NewPixPat();
    PIXPAT_TYPE(temp_pixpat) = pixpat_type_orig;
    CPORT_FILL_PIXPAT(port) = temp_pixpat;

    temp_pixpat = NewPixPat();
    PIXPAT_TYPE(temp_pixpat) = pixpat_type_orig;
    CPORT_BK_PIXPAT(port) = temp_pixpat;

    CPORT_GRAFVARS(port) = NewHandleClear(sizeof(GrafVars));

    /* allocate storage for members also present in GrafPort */
    PORT_VIS_REGION(port) = NewRgn();
    PORT_CLIP_REGION(port) = NewRgn();

    InitCPort(port);

    handle_vector<CGrafPtr, Handle, 2, true> portList(LM(PortList));
    portList.push_back(port);
    *(GUEST<int16_t>*) *LM(PortList) = portList.size();
}

void Executor::C_CloseCPort(CGrafPtr port)
{
    ClosePort((GrafPtr)port);
}

/* FIXME:
   do these belong here */

void Executor::C_InitCPort(CGrafPtr p)
{
    GDHandle gd;
    RgnHandle rh;

    if(!p || !CGrafPort_p(p))
        return;

    /* set the port early so we can call convenience functions
     that operate on qdGlobals().thePort */
    SetPort((GrafPtr)p);

    CPORT_VERSION(p) = CPORT_FLAG_BITS;
    // | /* color quickdraw version */ 0);

    gd = LM(TheGDevice);
    **CPORT_PIXMAP(p) = **GD_PMAP(gd);

    PORT_DEVICE(p) = 0;
    PORT_RECT(p) = GD_RECT(gd);
    PORT_PEN_LOC(p).h = PORT_PEN_LOC(p).v = 0;
    PORT_PEN_SIZE(p).h = PORT_PEN_SIZE(p).v = 1;
    PORT_PEN_MODE(p) = patCopy;
    PORT_PEN_VIS(p) = 0;
    PORT_TX_FONT(p) = 0;
    PORT_TX_FACE(p) = 0;
    *((char *)&p->txFace + 1) = 0; /* Excel & tests show we need to do this. */
    PORT_TX_MODE(p) = srcOr;
    PORT_TX_SIZE(p) = 0;
    PORT_SP_EXTRA(p) = 0;
    RGBForeColor(&ROMlib_black_rgb_color);
    RGBBackColor(&ROMlib_white_rgb_color);
    PORT_COLR_BIT(p) = 0;
    PORT_PAT_STRETCH(p) = 0;
    PORT_PIC_SAVE(p) = nullptr;
    PORT_REGION_SAVE(p) = nullptr;
    PORT_POLY_SAVE(p) = nullptr;
    PORT_GRAF_PROCS(p) = nullptr;

    PenPat(&qdGlobals().black);
    BackPat(&qdGlobals().white);

    /* hack */
    ROMlib_fill_pat(qdGlobals().black);

    /* initialize default values for newly allocated
     CGrafPtr */
    PORT_DEVICE(p) = 0;

    /* rgbOpColor of GrafVars field is set to black,
     rgbHiliteColor is set to the default value (where does this come from)
     and all other fields are zero'd */

    /* A test case shows that grafVars is allocated only if it was nullptr. */
    if(CPORT_GRAFVARS(p) == nullptr)
        CPORT_GRAFVARS(p) = NewHandleClear(sizeof(GrafVars));

    /* #warning "p->grafVars not initialized" */

    CPORT_OP_COLOR(p) = ROMlib_black_rgb_color;
    CPORT_HILITE_COLOR(p) = LM(HiliteRGB);

    CPORT_CH_EXTRA(p) = 0;
    /* represents the low word of a Fixed number
     whose value is 0.5 */
    CPORT_PENLOC_HFRAC(p) = 0x8000;

    rh = PORT_VIS_REGION(p);
    SetEmptyRgn(rh);
    RectRgn(rh, &GD_RECT(gd));

    rh = PORT_CLIP_REGION(p);
    SetEmptyRgn(rh);
    SetRectRgn(rh, -32767, -32767, 32767, 32767);
}

void Executor::C_SetPortPix(PixMapHandle pixmap)
{
    CPORT_PIXMAP(theCPort) = pixmap;
}

static const LONGINT high_bits_to_colors[2][2][2] = {
    {
        {
            blackColor, blueColor,
        },
        { greenColor, cyanColor },
    },
    {
        {
            redColor, magentaColor,
        },
        { yellowColor, whiteColor },
    },
};

void Executor::C_RGBForeColor(RGBColor *color)
{
    if(CGrafPort_p(qdGlobals().thePort))
    {
        CPORT_RGB_FG_COLOR(theCPort) = *color;

        /* pick the best color and store it into `theCPort->fgColor' */
        PORT_FG_COLOR(theCPort) = Color2Index(color);
    }
    else
    {
        /* GrafPort */
        int basic_qd_color;

        basic_qd_color
            = high_bits_to_colors
                [color->red >> 15]
                [color->green >> 15]
                [color->blue >> 15];

        ForeColor(basic_qd_color);
    }
}

void Executor::C_RGBBackColor(RGBColor *color)
{

#if defined(EVIL_ILLUSTRATOR_7_HACK)
    if(ROMlib_evil_illustrator_7_hack)
    {
        color = (RGBColor *)alloca(sizeof(RGBColor));
        color->red = 65535;
        color->green = 65535;
        color->blue = 65535;
    }
#endif

    if(CGrafPort_p(qdGlobals().thePort))
    {
        CPORT_RGB_BK_COLOR(theCPort) = *color;

        /* pick the best color and store it into `theCPort->bkColor' */
        PORT_BK_COLOR(theCPort) = Color2Index(color);
    }
    else
    {
        /* GrafPort */
        int basic_qd_color;

        basic_qd_color
            = high_bits_to_colors
                [color->red >> 15]
                [color->green >> 15]
                [color->blue >> 15];

        BackColor(basic_qd_color);
    }
}

void Executor::C_GetForeColor(RGBColor *color)
{
    if(CGrafPort_p(qdGlobals().thePort))
        *color = CPORT_RGB_FG_COLOR(theCPort);
    else
        *color = *(ROMlib_qd_color_to_rgb(PORT_FG_COLOR(qdGlobals().thePort)));
}

void Executor::C_GetBackColor(RGBColor *color)
{
    if(CGrafPort_p(qdGlobals().thePort))
        *color = CPORT_RGB_BK_COLOR(theCPort);
    else
        *color = *(ROMlib_qd_color_to_rgb(PORT_BK_COLOR(qdGlobals().thePort)));
}

void Executor::C_PenPixPat(PixPatHandle new_pen)
{
    if(CGrafPort_p(qdGlobals().thePort))
    {
        PixPatHandle old_pen;

        old_pen = CPORT_PEN_PIXPAT(theCPort);
        if(old_pen == new_pen)
            return;

        if(old_pen && (PIXPAT_TYPE(old_pen) == pixpat_type_orig))
            DisposePixPat(old_pen);

        CPORT_PEN_PIXPAT(theCPort) = new_pen;
    }
    else
        PORT_PEN_PAT(qdGlobals().thePort) = PIXPAT_1DATA(new_pen);
}

void Executor::C_BackPixPat(PixPatHandle new_bk)
{
    if(CGrafPort_p(qdGlobals().thePort))
    {
        PixPatHandle old_bk;

        old_bk = CPORT_BK_PIXPAT(theCPort);
        if(old_bk == new_bk)
            return;

        if(old_bk && PIXPAT_TYPE(old_bk) == pixpat_type_orig)
            DisposePixPat(old_bk);

        CPORT_BK_PIXPAT(theCPort) = new_bk;
    }
    else
        PORT_BK_PAT(qdGlobals().thePort) = PIXPAT_1DATA(new_bk);
}

void Executor::ROMlib_fill_pixpat(PixPatHandle new_fill)
{
    if(CGrafPort_p(qdGlobals().thePort))
    {
        PixPatHandle old_fill;

        old_fill = CPORT_FILL_PIXPAT(theCPort);
        if(old_fill == new_fill)
            return;

#if 0
      if (PIXPAT_TYPE (old_fill) == pixpat_type_orig)
	DisposePixPat (old_fill);
#endif

        CPORT_FILL_PIXPAT(theCPort) = new_fill;
    }
    else
        PORT_BK_PAT(qdGlobals().thePort) = PIXPAT_1DATA(new_fill);
}

/* where is FillPixPat */

void Executor::C_OpColor(RGBColor *color)
{
    if(!CGrafPort_p(qdGlobals().thePort))
        return;

    (*(GrafVarsHandle)CPORT_GRAFVARS(theCPort))->rgbOpColor = *color;
}

void Executor::C_HiliteColor(RGBColor *color)
{
    if(!CGrafPort_p(qdGlobals().thePort))
        return;

    (*(GrafVarsHandle)CPORT_GRAFVARS(theCPort))->rgbHiliteColor = *color;
}

/* PixMap operations */

PixMapHandle Executor::C_NewPixMap()
{
    PixMapHandle pixmap;

    pixmap = (PixMapHandle)NewHandle(sizeof(PixMap));
    if(pixmap == nullptr)
    {
#if defined(FLAG_ALLOCATION_FAILURES)
        gui_fatal("allocation failure");
#endif
        return nullptr;
    }

    HLock((Handle)pixmap); /* so we can use accessor macros even when we're
  				calling routines that move memory */

    /* All PixMap fields except the ColorTable come from LM(TheGDevice).
   * The ColorTable is allocated but not initialized.  (IMV-70)
   */
    if(LM(TheGDevice))
    {
        **pixmap = **GD_PMAP(LM(TheGDevice));
    }
    else
    {
        /* If LM(TheGDevice) is nullptr, we fill in some useful default values.
       * This is a hack to make Executor bootstrap properly.
       */
        memset(*pixmap, 0, sizeof(PixMap));
        (*pixmap)->rowBytes = PIXMAP_DEFAULT_ROW_BYTES;
        PIXMAP_HRES(pixmap) = PIXMAP_VRES(pixmap) = 72 << 16;
        PIXMAP_PIXEL_TYPE(pixmap) = 0;
        PIXMAP_CMP_COUNT(pixmap) = 1;
    }

    /* The ColorTable is set to an empty ColorTable (IMV-70). */
    PIXMAP_TABLE(pixmap)
        = (CTabHandle)NewHandleClear(sizeof(ColorTable));

    HUnlock((Handle)pixmap);
    return pixmap;
}

void Executor::C_DisposePixMap(PixMapHandle pixmap)
{
    if(pixmap)
    {
        DisposeCTable(PIXMAP_TABLE(pixmap));
        DisposeHandle((Handle)pixmap);
    }
}

void Executor::C_CopyPixMap(PixMapHandle src, PixMapHandle dst)
{
    CTabHandle dst_ctab;

    /* save away the destination ctab; it is going to get clobbered. */
    dst_ctab = PIXMAP_TABLE(dst);

    /* #warning "determine actual CopyPixMap behavior" */
    **dst = **src;

    PIXMAP_TABLE(dst) = dst_ctab;
    ROMlib_copy_ctab(PIXMAP_TABLE(src), dst_ctab);
}

/* PixPat operations */

PixPatHandle Executor::C_NewPixPat()
{
    PixPatHandle pixpat;
    Handle xdata;

    pixpat = (PixPatHandle)NewHandle(sizeof(PixPat));

    xdata = NewHandleClear(sizeof(xdata_t));

    HASSIGN_6(pixpat,
              patMap, NewPixMap(),
              patData, NewHandle(0),
              patType, pixpat_type_color,
              patXMap, nullptr,
              patXData, xdata,
              patXValid, -1);

    return pixpat;
}

typedef struct pixpat_res *pixpat_res_ptr;

typedef GUEST<pixpat_res_ptr> *pixpat_res_handle;

PixPatHandle Executor::C_GetPixPat(INTEGER pixpat_id)
{
    pixpat_res_handle pixpat_res;
    PixPatHandle pixpat;
    PixMapHandle patmap;
    int pixpat_data_offset, pixpat_data_size;
    CTabPtr ctab_ptr;
    int ctab_size;
    Handle xdata;

    pixpat_res = (pixpat_res_handle)GetResource("ppat"_4, pixpat_id);
    if(pixpat_res == nullptr)
        return (PixPatHandle)nullptr;
    if(*pixpat_res == nullptr)
        LoadResource((Handle)pixpat_res);

    pixpat = (PixPatHandle)NewHandle(sizeof(PixPat));
    patmap = (PixMapHandle)NewHandle(sizeof(PixMap));
    **pixpat = (*pixpat_res)->pixpat;
    **patmap = (*pixpat_res)->patmap;

    {
        int pixpat_type;

        pixpat_type = PIXPAT_TYPE(pixpat);

        /* ### are `rgb' (and `old_style'?) valid pattern types for pixpat
       resources */
        if(pixpat_type != pixpat_old_style_pattern
           && pixpat_type != pixpat_color_pattern
           && pixpat_type != pixpat_rgb_pattern)
        {
            warning_unexpected("unknown pixpat type `%d'",
                               toHost(PIXPAT_TYPE(pixpat)));
            PIXPAT_TYPE(pixpat) = pixpat_color_pattern;
        }
    }

    gui_assert(ptr_to_longint(PIXPAT_MAP(pixpat)) == sizeof(PixPat));

    PIXPAT_MAP(pixpat) = patmap;

    PIXPAT_XVALID(pixpat) = -1;

    xdata = NewHandle(sizeof(xdata_t));
    memset(*xdata, 0, sizeof(xdata_t));
    PIXPAT_XDATA(pixpat) = xdata;
    PIXPAT_XMAP(pixpat) = nullptr;

    pixpat_data_offset = PIXPAT_DATA_AS_OFFSET(pixpat);
    pixpat_data_size = (PIXMAP_TABLE_AS_OFFSET(patmap)
                        - pixpat_data_offset);

    HLock((Handle)pixpat);
    PIXPAT_DATA(pixpat) = NewHandle(pixpat_data_size);
    HUnlock((Handle)pixpat);

    BlockMoveData((Ptr)((char *)*pixpat_res + pixpat_data_offset),
                  *PIXPAT_DATA(pixpat),
                  pixpat_data_size);

    HLockGuard guard(pixpat_res);
    /* ctab_ptr is a pointer into the pixpat_res_handle;
	  make sure no allocations are done while it is in use */
    ctab_ptr = (CTabPtr)((char *)*pixpat_res
                         + (int)PIXMAP_TABLE_AS_OFFSET(patmap));
    ctab_size = (sizeof(ColorTable)
                 + (sizeof(ColorSpec) * ctab_ptr->ctSize));

    /* SetHandleSize ((Handle) PIXMAP_TABLE (patmap), ctab_size); */

    HLock((Handle)patmap);
    PIXMAP_TABLE(patmap) = (CTabHandle)NewHandle(ctab_size);
    HUnlock((Handle)patmap);

    BlockMoveData((Ptr)ctab_ptr,
                  (Ptr)*PIXMAP_TABLE(patmap),
                  ctab_size);

    /* ctab_ptr->ctSeed = GetCTSeed (); */
    CTAB_SEED(PIXMAP_TABLE(patmap)) = GetCTSeed();

#if 0
  gui_assert (GetHandleSize (pixpat_res)
	      == (sizeof (struct pixpat_res)
		  + pixpat_data_size
		  + ctab_size));
#endif /* 0 */

    return pixpat;
}

void Executor::C_DisposePixPat(PixPatHandle pixpat_h)
{
    if(pixpat_h)
    {
        /* ##### determine which of these checks are necessary, and which
	 should be asserts that the handles are non-nullptr */
        if(PIXPAT_MAP(pixpat_h))
            DisposePixMap(PIXPAT_MAP(pixpat_h));
        if(PIXPAT_DATA(pixpat_h))
            DisposeHandle(PIXPAT_DATA(pixpat_h));
        /* We ignore the xmap field, so no need to free it. */
        if(PIXPAT_XDATA(pixpat_h))
            xdata_free((xdata_handle_t)PIXPAT_XDATA(pixpat_h));

        DisposeHandle((Handle)pixpat_h);
    }
}

void Executor::C_CopyPixPat(PixPatHandle src, PixPatHandle dst)
{
    int data_size;

    PIXPAT_TYPE(dst) = PIXPAT_TYPE(src);
    CopyPixMap(PIXPAT_MAP(src), PIXPAT_MAP(dst));

    data_size = GetHandleSize(PIXPAT_DATA(src));
    SetHandleSize(PIXPAT_DATA(dst), data_size);
    memcpy(*PIXPAT_DATA(dst), *PIXPAT_DATA(src), data_size);
    PIXPAT_XVALID(dst) = -1;
    PIXPAT_1DATA(dst) = PIXPAT_1DATA(src);
}

void Executor::C_MakeRGBPat(PixPatHandle pixpat, RGBColor *color)
{
    PixMapHandle patmap;

    PIXPAT_TYPE(pixpat) = pixpat_rgb_pattern;
    PIXPAT_XVALID(pixpat) = -1;

    /* ##### resolve the meaning of the actual PixPat fields */

    patmap = PIXPAT_MAP(pixpat);
    PIXMAP_SET_ROWBYTES(patmap, 2);
    PIXMAP_BOUNDS(patmap) = ROMlib_pattern_bounds;
    /* create a table with 5 entries, the last of which
     will be the desired rgb color */
    SetHandleSize((Handle)PIXMAP_TABLE(patmap),
                  (Size)(sizeof(ColorTable) + (4 * sizeof(ColorSpec))));

    CTAB_SEED(PIXMAP_TABLE(patmap)) = GetCTSeed();
    CTAB_SIZE(PIXMAP_TABLE(patmap)) = 5;
    CTAB_TABLE(PIXMAP_TABLE(patmap))
    [4].rgb
        = *color;
}
