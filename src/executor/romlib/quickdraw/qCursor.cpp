/* Copyright 1986-1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
/* ack */
#include <WindowMgr.h>
#include <CQuickDraw.h>
#include <EventMgr.h>
#include <OSEvent.h>
#include <MemoryMgr.h>

#include <quickdraw/cquick.h>
#include <mman/mman.h>
#include <res/resource.h>

#include <osevent/osevent.h>

#include <quickdraw/quick.h>
#include <vdriver/vdriver.h>

using namespace Executor;

static CCrsrHandle current_ccrsr;
static Cursor current_crsr;

static bool current_cursor_valid_p = false;
static int current_cursor_color_p;

void Executor::cursor_reset_current_cursor(void)
{
    current_cursor_valid_p = false;
    if(current_cursor_color_p)
        SetCCursor(current_ccrsr);
    else
        SetCursor(&current_crsr);
}

static void ROMlib_showcursor()
{
    if(!LM(CrsrVis))
    {
        vdriver->setCursorVisible(true);
        LM(CrsrVis) = true;
    }
}

static void ROMlib_restorecursor()
{
    if(LM(CrsrVis))
    {
        vdriver->setCursorVisible(false);
        LM(CrsrVis) = false;
    }
}

void Executor::ROMlib_showhidecursor() /* INTERNAL */
{
    if(LM(CrsrState) == 0)
        ROMlib_showcursor();
    else
        ROMlib_restorecursor();
}

void Executor::C_SetCursor(Cursor *cp)
{
    if(current_cursor_valid_p
       && !current_cursor_color_p
       && !memcmp(&current_crsr, cp, sizeof(Cursor)))
        return;

    if(vdriver->cursorDepth() == 1)
    {
        vdriver->setCursor((char *)cp->data, (unsigned short *)cp->mask,
                           cp->hotSpot.h, cp->hotSpot.v);
    }
    else
    {
        PixMap data_pixmap, target_pixmap;
        char *data_baseaddr;
        /* rowbytes of expanded cursor data */
        int rowbytes;

        data_pixmap.baseAddr = (Ptr)cp->data;
        data_pixmap.rowBytes = 2;
        data_pixmap.bounds = ROMlib_cursor_rect;
        data_pixmap.cmpCount = 1;
        data_pixmap.pixelType = 0;
        data_pixmap.pixelSize = data_pixmap.cmpSize = 1;
        data_pixmap.pmTable = validate_relative_bw_ctab();

        rowbytes = (16 * vdriver->cursorDepth()) / 8;
        data_baseaddr = (char *)alloca(16 * rowbytes);

        target_pixmap.baseAddr = (Ptr)data_baseaddr;
        target_pixmap.rowBytes = rowbytes;
        target_pixmap.bounds = ROMlib_cursor_rect;
        target_pixmap.cmpCount = 1;
        target_pixmap.pixelType = 0;
        target_pixmap.pixelSize = target_pixmap.cmpSize
            = vdriver->cursorDepth();
        /* the target pixmap colortable is not used by `convert_pixmap ()'
	 target_pixmap.pmTable = ...; */

        convert_pixmap(&data_pixmap, &target_pixmap,
                       &ROMlib_cursor_rect, nullptr);

        vdriver->setCursor(data_baseaddr, (unsigned short *)cp->mask,
                           cp->hotSpot.h, cp->hotSpot.v);
    }

    current_crsr = *cp;
    current_cursor_color_p = false;
    current_cursor_valid_p = true;

    if(LM(CrsrState) == 0)
    {
        LM(CrsrVis) = false;
        ROMlib_showcursor();
    }
}

void Executor::C_InitCursor()
{
    LM(CrsrState) = 0;
    SetCursor(&qdGlobals().arrow);
    LM(CrsrVis) = false;
    ROMlib_showcursor();
}

void Executor::C_HideCursor() /* IMI-168 */
{
    ROMlib_restorecursor();
    LM(CrsrState) = LM(CrsrState) - 1;
}

void Executor::C_ShowCursor() /* IMI-168 */
{
    if(++LM(CrsrState) == 0)
        ROMlib_showcursor();
    if(LM(CrsrState) > 0)
        LM(CrsrState) = 0;
}

static void wewantpointermovements(INTEGER x)
{
    LM(CrsrState) = LM(CrsrState) + x;
    ROMlib_bewaremovement = true;
}

void Executor::C_ObscureCursor() /* IMI-168 */
{
    ROMlib_restorecursor();
    wewantpointermovements(0);
}

void Executor::C_ShieldCursor(Rect *rp, Point p) /* IMI-474 */
{
    EventRecord evt;
    Point ep;

    GetOSEvent(0, &evt);
    ep.v = evt.where.v + p.v;
    ep.h = evt.where.h + p.h;
    if(PtInRect(ep, rp))
        HideCursor();
    else
        wewantpointermovements(-1);
}

typedef GUEST<ccrsr_res_ptr> *ccrsr_res_handle;

CCrsrHandle Executor::C_GetCCursor(INTEGER crsr_id)
{
    CCrsrHandle ccrsr_handle;
    ccrsr_res_handle res_handle;

    ccrsr_handle = (CCrsrHandle)NewHandle(sizeof(CCrsr));

    res_handle = (ccrsr_res_handle)ROMlib_getrestid("crsr"_4, crsr_id);
    if(res_handle == nullptr)
        return nullptr;

    HLockGuard guard1(ccrsr_handle), guard2(res_handle);
    ccrsr_res_ptr resource;
    CCrsrPtr ccrsr;
    CTabPtr tmp_ctab;
    int ccrsr_data_size;
    int ccrsr_data_offset;
    int ccrsr_ctab_size;
    int ccrsr_ctab_offset;

    int cursor_pixel_map_offset;
    PixMapPtr cursor_pixel_map_resource;
    PixMapHandle cursor_pixel_map;

    GUEST<Handle> h;

    resource = *res_handle;
    ccrsr = *ccrsr_handle;

    BlockMoveData((Ptr)&resource->crsr, (Ptr)ccrsr,
                  sizeof(CCrsr));

    /* NOTE: they're overloading
	  crsrMap to have an offset rather than a handle */
    cursor_pixel_map_offset = guest_cast<int32_t>(ccrsr->crsrMap);
    cursor_pixel_map_resource
        = (PixMapPtr)((char *)resource + cursor_pixel_map_offset);

    cursor_pixel_map = NewPixMap();
    ccrsr->crsrMap = cursor_pixel_map;
    BlockMoveData((Ptr)cursor_pixel_map_resource,
                  (Ptr)*cursor_pixel_map,
                  sizeof *cursor_pixel_map_resource);

    ccrsr_data_offset = guest_cast<int32_t>(ccrsr->crsrData);

    ccrsr_ctab_offset = (int)PIXMAP_TABLE_AS_OFFSET(ccrsr->crsrMap);
    ccrsr_data_size = ccrsr_ctab_offset - ccrsr_data_offset;

    ccrsr->crsrData = NewHandle(ccrsr_data_size);
    BlockMoveData((Ptr)resource + ccrsr_data_offset,
                  (*ccrsr->crsrData),
                  ccrsr_data_size);

    tmp_ctab = (CTabPtr)((char *)resource + ccrsr_ctab_offset);
    ccrsr_ctab_size = CTAB_STORAGE_FOR_SIZE(tmp_ctab->ctSize);
    h = NewHandle(ccrsr_ctab_size);
    PIXMAP_TABLE(ccrsr->crsrMap) = guest_cast<CTabHandle>(h);
    BlockMoveData((Ptr)tmp_ctab,
                  (Ptr)*PIXMAP_TABLE(ccrsr->crsrMap),
                  ccrsr_ctab_size);

    return ccrsr_handle;
}

Rect Executor::ROMlib_cursor_rect = {
    0, 0, 16, 16,
};

/* ### this isn't a cursor specific routine, and it may be helpful
   elsewhere, but since it isn't clear exactly what to compare, we'll
   leave it here for now */

static bool
pixmap_eq_p(PixMapHandle pm0, PixMapHandle pm1)
{
    return (PIXMAP_PIXEL_SIZE(pm0) == PIXMAP_PIXEL_SIZE(pm1)
            && PIXMAP_PIXEL_TYPE(pm0) == PIXMAP_PIXEL_TYPE(pm1)
            && PIXMAP_CMP_COUNT(pm0) == PIXMAP_CMP_COUNT(pm1)
            && PIXMAP_CMP_SIZE(pm0) == PIXMAP_CMP_SIZE(pm1)
            && (CTAB_SEED(PIXMAP_TABLE(pm0)) == CTAB_SEED(PIXMAP_TABLE(pm1)))
            && !memcmp(&PIXMAP_BOUNDS(pm0),
                       &PIXMAP_BOUNDS(pm1),
                       sizeof(Rect)));
}

void Executor::C_SetCCursor(CCrsrHandle ccrsr)
{
    if(current_cursor_valid_p
       && current_cursor_color_p)
    {
        int current_ccrsr_data_size, ccrsr_data_size;

        current_ccrsr_data_size = GetHandleSize(CCRSR_DATA(current_ccrsr));
        ccrsr_data_size = GetHandleSize(CCRSR_DATA(ccrsr));

        if(current_ccrsr_data_size == ccrsr_data_size
           && !memcmp(*CCRSR_DATA(current_ccrsr),
                      *CCRSR_DATA(ccrsr),
                      ccrsr_data_size)
           && CCRSR_TYPE(current_ccrsr) == CCRSR_TYPE(ccrsr)
           && !memcmp(CCRSR_1DATA(current_ccrsr),
                      CCRSR_1DATA(ccrsr),
                      sizeof(Bits16))
           && !memcmp(CCRSR_MASK(current_ccrsr),
                      CCRSR_MASK(ccrsr),
                      sizeof(Bits16))
           && !memcmp(&CCRSR_HOT_SPOT(current_ccrsr),
                      &CCRSR_HOT_SPOT(ccrsr),
                      sizeof(Point))
           && pixmap_eq_p(CCRSR_MAP(current_ccrsr),
                          CCRSR_MAP(ccrsr)))
            return;
    }

    {
        HLockGuard guard(ccrsr);
        GDHandle gdev;
        PixMapHandle gd_pmap;
        GUEST<Point> *hot_spot;

        gdev = LM(MainDevice);
        gd_pmap = GD_PMAP(gdev);

        hot_spot = &CCRSR_HOT_SPOT(ccrsr);

        if(vdriver->cursorDepth() > 2)
        {
            Handle ccrsr_xdata;

            ccrsr_xdata = CCRSR_XDATA(ccrsr);
            if(!ccrsr_xdata)
            {
                ccrsr_xdata = NewHandle(32 * vdriver->cursorDepth());
                CCRSR_XDATA(ccrsr) = ccrsr_xdata;
            }

            if(CCRSR_XVALID(ccrsr) == 0
               || (CCRSR_XVALID(ccrsr) != vdriver->cursorDepth())
               || (CCRSR_ID(ccrsr) != CTAB_SEED(PIXMAP_TABLE(gd_pmap))))
            {
                SetHandleSize(ccrsr_xdata, 32 * vdriver->cursorDepth());

                HLockGuard guard1(CCRSR_MAP(ccrsr)), guard2(CCRSR_DATA(ccrsr));

                PixMap src;
                PixMap ccrsr_xmap;

                /* only fields used by `convert_pixmap ()',
		       baseAddr is filled in below */
                memset(&ccrsr_xmap, 0, sizeof ccrsr_xmap);
                ccrsr_xmap.rowBytes = 2 * vdriver->cursorDepth();
                ccrsr_xmap.pixelSize = vdriver->cursorDepth();

                src = **CCRSR_MAP(ccrsr);
                src.baseAddr = *CCRSR_DATA(ccrsr);
                HLockGuard guard3(ccrsr_xdata);
                ccrsr_xmap.baseAddr = *ccrsr_xdata;
                convert_pixmap(&src, &ccrsr_xmap,
                               &ROMlib_cursor_rect, nullptr);

                CCRSR_XVALID(ccrsr) = vdriver->cursorDepth();
                CCRSR_ID(ccrsr) = CTAB_SEED(PIXMAP_TABLE(gd_pmap));
            }

            /* Actually set the current cursor. */
            HLockGuard guard(ccrsr_xdata);
            vdriver->setCursor((char *)*ccrsr_xdata,
                               (unsigned short *)CCRSR_MASK(ccrsr),
                               hot_spot->h, hot_spot->v);
        }
        else
        {
            vdriver->setCursor((char *)CCRSR_1DATA(ccrsr),
                               (unsigned short *)CCRSR_MASK(ccrsr),
                               hot_spot->h, hot_spot->v);
        }
    }

    /* copy the cursor so if there is a depth change, we have the cursor
     data around to reset the cursor; a pain in the butt */
    if(current_ccrsr != ccrsr)
    {
        int data_size;

        if(!current_ccrsr)
        {
            TheZoneGuard guard(LM(SysZone));
            current_ccrsr = (CCrsrHandle)NewHandle(sizeof(CCrsr));
            CCRSR_DATA(current_ccrsr) = NewHandle(0);
            CCRSR_XDATA(current_ccrsr) = nullptr;
            CCRSR_MAP(current_ccrsr) = NewPixMap();
        }

        /* copy the cursor structure */
        CCRSR_TYPE(current_ccrsr) = CCRSR_TYPE(ccrsr);
        memcpy(CCRSR_1DATA(current_ccrsr), CCRSR_1DATA(ccrsr),
               sizeof(Bits16));
        memcpy(CCRSR_MASK(current_ccrsr), CCRSR_MASK(ccrsr),
               sizeof(Bits16));
        CCRSR_HOT_SPOT(current_ccrsr) = CCRSR_HOT_SPOT(ccrsr);

        /* copy the pixmap */
        CopyPixMap(CCRSR_MAP(ccrsr), CCRSR_MAP(current_ccrsr));

        /* copy the cursor data */
        data_size = GetHandleSize(CCRSR_DATA(ccrsr));
        SetHandleSize(CCRSR_DATA(current_ccrsr), data_size);
        memcpy(*CCRSR_DATA(current_ccrsr),
               *CCRSR_DATA(ccrsr), data_size);

        /* invalidate this cursor */
        CCRSR_XVALID(current_ccrsr) = 0;
        CCRSR_ID(current_ccrsr) = -1;

        current_cursor_valid_p = true;
        current_cursor_color_p = true;
    }
}

void Executor::C_DisposeCCursor(CCrsrHandle ccrsr)
{
    /* #warning "implement DisposeCCursor" */
    warning_unimplemented("");
}

void Executor::C_AllocCursor()
{
    /* This function is a NOP for us as far as I know. */
    warning_unexpected("AllocCursor called -- this is unusual");
}
