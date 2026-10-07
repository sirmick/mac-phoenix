/* Copyright 1994, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>

#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <MemoryMgr.h>

#include <quickdraw/depthconv.h>
#include <quickdraw/cquick.h>
#include <quickdraw/rgbutil.h>
#include <prefs/prefs.h>
#include <mman/tempalloc.h>
#include <quickdraw/picture.h>
#include <mman/mman.h>
#include <vdriver/vdriver.h>
#include <algorithm>

using namespace Executor;

static uint32_t depth_table_space[DEPTHCONV_MAX_UINT32_TABLE_SIZE];

static depthconv_func_t conversion_func = nullptr;

static int32_t cached_src_bpp = -1, cached_dst_bpp = -1;
static GUEST<int32_t> cached_src_seed_x = -1, cached_dst_seed_x = -1;

static ITabHandle target_itab;

rgb_spec_t Executor::mac_16bpp_rgb_spec, Executor::mac_32bpp_rgb_spec;

void Executor::pixmap_black_white(const PixMap *pixmap,
                                  uint32_t *black_return, uint32_t *white_return)
{
    if(pixmap->pixelType == RGBDirect)
    {
        const rgb_spec_t *rgb_spec;

        rgb_spec = pixmap_rgb_spec(pixmap);
        *black_return = rgb_spec->black_pixel;
        *white_return = rgb_spec->white_pixel;
    }
    else
    {
        int bpp;

        bpp = pixmap->pixelSize;

        *black_return = (1 << bpp) - 1;
        *white_return = 0;
    }
}

void Executor::gd_black_white(GDHandle gdh,
                              uint32_t *black_return, uint32_t *white_return)
{
    GDPtr gd;
    PixMapPtr gd_pmap;

    gd = *gdh;
    gd_pmap = (*gd->gdPMap);

    pixmap_black_white(gd_pmap, black_return, white_return);
}

void Executor::pixmap_free_copy(PixMap *pm)
{
    DisposePtr(pm->baseAddr);
}

void Executor::pixmap_copy(const PixMap *src_pm, const Rect *src_rect,
                           PixMap *return_pm, Rect *return_rect)
{
    /* alas, we must copy to a scratch bitmap */
    /* src is the bitmap in the active screen which cannot be
      accessed dirctly */
    int row_bytes;
    int width, height;

    width = RECT_WIDTH(src_rect);
    height = RECT_HEIGHT(src_rect);

    *return_rect = *src_rect;

    /* default pixmap field values are the same for src and return
      pixmaps */
    *return_pm = *src_pm;

    row_bytes = ((width * src_pm->pixelSize + 31) / 32) * 4;

    return_pm->baseAddr = NewPtr(height * row_bytes);
    return_pm->rowBytes = row_bytes | PIXMAP_DEFAULT_ROW_BYTES;
    return_pm->bounds = *return_rect;

    {
        uint32_t black_pixel, white_pixel;
        RgnHandle rgn;
        Rect tmp_rect;

        rgn = NewRgn();
        SectRect(return_rect, &return_pm->bounds, &tmp_rect);
        RectRgn(rgn, &tmp_rect);

        pixmap_black_white(src_pm, &black_pixel, &white_pixel);

        ROMlib_blt_rgn_update_dirty_rect(rgn, srcCopy,
                                         false, src_pm->pixelSize,
                                         src_pm, return_pm,
                                         src_rect, return_rect,
                                         black_pixel, white_pixel);
        DisposeRgn(rgn);
    }
}

uint32_t
pixel_from_rgb(RGBColor *color,
               const rgb_spec_t *rgb_spec)
{
    if(rgb_spec)
        return ((rgb_spec->rgbcolor_to_pixel)(rgb_spec, color, true));
    else
        return Color2Index(color);
}

void Executor::canonical_from_bogo_color(uint32_t index,
                                         const rgb_spec_t *rgb_spec,
                                         uint32_t *pixel_out,
                                         RGBColor *rgb_out)
{
    if(rgb_spec)
    {
        RGBColor t_color;
        rgb_spec_t *mac_rgb_spec;

        if(rgb_spec->bpp == 16)
        {
            mac_rgb_spec = &mac_16bpp_rgb_spec;
            if(rgb_spec == mac_rgb_spec)
            {
                if(pixel_out)
                    *pixel_out = CW_RAW(index);
                if(rgb_out)
                    (rgb_spec->pixel_to_rgbcolor)(rgb_spec, index, rgb_out);
                return;
            }
        }
        else if(rgb_spec->bpp == 32)
        {
            mac_rgb_spec = &mac_32bpp_rgb_spec;
            if(rgb_spec == mac_rgb_spec)
            {
                if(pixel_out)
                    *pixel_out = CL_RAW(index);
                if(rgb_out)
                    (rgb_spec->pixel_to_rgbcolor)(rgb_spec, index, rgb_out);
                return;
            }
        }
        else
            gui_fatal("unknown bpp");

        (mac_rgb_spec->pixel_to_rgbcolor)(mac_rgb_spec, index, &t_color);
        if(pixel_out)
            *pixel_out = (rgb_spec->rgbcolor_to_pixel)(rgb_spec, &t_color, true);
        if(rgb_out)
            *rgb_out = t_color;
    }
    else
    {
        if(pixel_out)
            *pixel_out = index;
        if(rgb_out)
            Index2Color(index, rgb_out);
    }
}

void Executor::ROMlib_fg_bk(uint32_t *fg_pixel_out, uint32_t *bk_pixel_out,
                            RGBColor *fg_rgb_out, RGBColor *bk_rgb_out,
                            const rgb_spec_t *rgb_spec,
                            bool active_screen_addr_p,
                            bool indirect_p)
{
    GrafPtr current_port;
    uint32_t fg_pixel, bk_pixel;
    RGBColor fg_rgb, bk_rgb;

    current_port = qdGlobals().thePort;
    if(CGrafPort_p(current_port))
    {
        if(indirect_p)
        {
            fg_rgb = CPORT_RGB_FG_COLOR(current_port);
            fg_pixel = pixel_from_rgb(&fg_rgb, rgb_spec);
            bk_rgb = CPORT_RGB_BK_COLOR(current_port);
            bk_pixel = pixel_from_rgb(&bk_rgb, rgb_spec);
        }
        else
        {
            canonical_from_bogo_color(PORT_FG_COLOR(current_port), rgb_spec,
                                      &fg_pixel, &fg_rgb);
            canonical_from_bogo_color(PORT_BK_COLOR(current_port), rgb_spec,
                                      &bk_pixel, &bk_rgb);
        }
    }
    else
    {
        if(active_screen_addr_p)
        {
            uint32_t fg_color, bk_color;

            fg_color = PORT_FG_COLOR(current_port);
            fg_rgb = *ROMlib_qd_color_to_rgb(fg_color);
            fg_pixel = pixel_from_rgb(&fg_rgb, rgb_spec);

            bk_color = PORT_BK_COLOR(current_port);
            bk_rgb = *ROMlib_qd_color_to_rgb(bk_color);
            bk_pixel = pixel_from_rgb(&bk_rgb, rgb_spec);
        }
        else
        {
            const uint32_t black_pixel = 1, white_pixel = 0;

            if(PORT_FG_COLOR(current_port) == whiteColor)
            {
                fg_pixel = white_pixel;
                fg_rgb = ROMlib_white_rgb_color;
            }
            else
            {
                fg_pixel = black_pixel;
                fg_rgb = ROMlib_black_rgb_color;
            }

            if(PORT_BK_COLOR(current_port) == whiteColor)
            {
                bk_pixel = white_pixel;
                bk_rgb = ROMlib_white_rgb_color;
            }
            else
            {
                bk_pixel = black_pixel;
                bk_rgb = ROMlib_black_rgb_color;
            }
        }
    }
    if(fg_pixel_out)
        *fg_pixel_out = fg_pixel;
    if(bk_pixel_out)
        *bk_pixel_out = bk_pixel;

    if(fg_rgb_out)
        *fg_rgb_out = fg_rgb;
    if(bk_rgb_out)
        *bk_rgb_out = bk_rgb;
}

const rgb_spec_t *
Executor::pixmap_rgb_spec(const PixMap *pixmap)
{
    if(pixmap->pixelType == RGBDirect
       || pixmap->pixelType == vdriver_rgb_pixel_type)
    {
        if(vdriver->rgbSpec()
           && (active_screen_addr_p(pixmap)
               || pixmap->pixelType == vdriver_rgb_pixel_type))
            return vdriver->rgbSpec();
        else if(pixmap->pixelSize == 16)
            return &mac_16bpp_rgb_spec;
        else if(pixmap->pixelSize == 32)
            return &mac_32bpp_rgb_spec;
        else
            gui_fatal("unknown pixel size");
    }
    else
        return nullptr;
}

void Executor::pixmap_set_pixel_fields(PixMap *pixmap, int bpp)
{
    pixmap->packType = 0;
    pixmap->packSize = 0;

    if(bpp <= 8)
    {
        pixmap->pixelType = Indirect;
        pixmap->cmpSize = pixmap->pixelSize = bpp;
        pixmap->cmpCount = 1;
    }
    else
    {
        pixmap->pixelType = RGBDirect;
        pixmap->pixelSize = bpp;
        pixmap->cmpCount = 3;
        switch(bpp)
        {
            case 16:
                pixmap->cmpSize = 5;
                break;
            case 32:
                pixmap->cmpSize = 8;
                break;
        }
    }
}

static void
sort_color_table(CTabHandle dsth, const CTabHandle srch)
{
    CTabPtr src, dst;
    INTEGER src_ct_size;

    src = *srch;
    dst = *dsth;

    /* Claris Home Page has some PICTs with color tables that are too large,
     so we make sure we don't try to copy too much. */

    src_ct_size = std::min(src->ctSize, dst->ctSize);

    if(src->ctFlags & CTAB_GDEVICE_BIT)
    {
        dst->ctSeed = src->ctSeed;
        memcpy(dst->ctTable, src->ctTable,
               sizeof src->ctTable[0] * (src_ct_size + 1));
    }
    else
    {
        ColorSpec *src_table, *dst_table;
        int max_ctab_elt;
        int i;

        src_table = src->ctTable;
        dst_table = dst->ctTable;
        max_ctab_elt = dst->ctSize;
        dst->ctSeed = src->ctSeed;
        for(i = src_ct_size; i >= 0; i--)
            dst_table[src_table[i].value & max_ctab_elt].rgb
                = src_table[i].rgb;
    }
}

void Executor::convert_pixmap(const PixMap *src, PixMap *dst,
                              const Rect *rect,
                              CTabPtr conv_table)
{
    GDHandle the_gd;
    int src_bpp, dst_bpp;
    int width, height;
    GUEST<int32_t> dst_seed_x, src_seed_x;
    const rgb_spec_t *src_rgb_spec, *dst_rgb_spec;

    uint8_t *src_base, *dst_base;
    int16_t src_row_bytes, dst_row_bytes;

    TEMP_ALLOC_DECL(temp_scratch_pm_bits);

    /* Grab some useful information about the PixMaps. */
    src_bpp = src->pixelSize;
    dst_bpp = dst->pixelSize;

    width = RECT_WIDTH(rect);
    height = RECT_HEIGHT(rect);

    /* Make sure both bits per pixel are powers of 2 less than or equal
     to 32. */
    gui_assert(src_bpp <= 32 && dst_bpp <= 32
               && (src_bpp & (src_bpp - 1)) == 0
               && (dst_bpp & (dst_bpp - 1)) == 0);

    the_gd = LM(TheGDevice);

#define BPP_PIXEL_TYPE(bpp) ((bpp) > 8 ? RGBDirect : Indirect)
#define MUNGE(a, b) ((a) + (b)*0x100)
    switch(MUNGE(BPP_PIXEL_TYPE(src_bpp),
                 BPP_PIXEL_TYPE(dst_bpp)))
    {
        case MUNGE(Indirect, Indirect):
        {
            src_seed_x = CTAB_SEED(src->pmTable); /* big endian */
            dst_seed_x = CTAB_SEED(PIXMAP_TABLE(GD_PMAP(the_gd)));

            if(src_bpp != cached_src_bpp || dst_bpp != cached_dst_bpp
               || src_seed_x != cached_src_seed_x || dst_seed_x != cached_dst_seed_x
               || conv_table)
            {
                if(conv_table)
                {
                    conversion_func
                        = depthconv_make_ind_to_ind_table(depth_table_space,
                                                          src_bpp, dst_bpp, nullptr,
                                                          conv_table->ctTable);
                    /* Remember the specs for the table we just created. */
                    cached_src_seed_x = -1;
                    cached_dst_seed_x = -1;
                }
                else
                {
                    CTabHandle target_table, src_table;
                    CTabHandle mapping;
                    int max_mapping_elt, max_src_elt;

                    src_table = src->pmTable;
                    max_src_elt = CTAB_SIZE(src_table);
                    max_mapping_elt = (1 << src_bpp) - 1;
                    mapping
                        = (CTabHandle)(NewHandle(CTAB_STORAGE_FOR_SIZE(max_mapping_elt)));
                    CTAB_SIZE(mapping) = max_mapping_elt;
                    CTAB_FLAGS(mapping) = 0;
                    /* the source color table may not specify all  possible
 		   colors, so set unspecified colors to some sane value
 		   
 		   really, we only need to set the `value' field
 		   of the color specs */
                    if(max_mapping_elt > max_src_elt)
                        memset(&CTAB_TABLE(mapping)[max_src_elt + 1], 0,
                               (max_mapping_elt - max_src_elt) * sizeof(ColorSpec));

                    sort_color_table(mapping, src_table);

                    if(dst_bpp == 1 && BITMAP_P(dst))
                        target_table = ROMlib_bw_ctab;
                    else
                        target_table = nullptr;

                    GetSubTable(mapping, DEFAULT_ITABLE_RESOLUTION,
                                target_table);

                    conversion_func
                        = depthconv_make_ind_to_ind_table(depth_table_space,
                                                          src_bpp, dst_bpp, nullptr,
                                                          CTAB_TABLE(mapping));

                    /* free up the mapping table */
                    DisposeHandle((Handle)mapping);

                    /* Remember the specs for the table we just created. */
                    cached_src_seed_x = src_seed_x;
                    cached_dst_seed_x = dst_seed_x;
                }
            }
            break;
        }
        case MUNGE(Indirect, RGBDirect):
        {
            CTabHandle src_table;

            src_table = src->pmTable;
            src_seed_x = CTAB_SEED(src_table);

            dst_rgb_spec = pixmap_rgb_spec(dst);
            dst_seed_x.raw(dst_rgb_spec->seed_x);

            if(src_bpp != cached_src_bpp || dst_bpp != cached_dst_bpp
               || src_seed_x != cached_src_seed_x || dst_seed_x != cached_dst_seed_x)
            {
                conversion_func
                    = depthconv_make_ind_to_rgb_table(depth_table_space, src_bpp,
                                                      nullptr, CTAB_TABLE(src_table),
                                                      dst_rgb_spec);
            }
            break;
        }
        case MUNGE(RGBDirect, Indirect):
        {
            CTabHandle target_table;
            static CTabHandle cached_target_table;

            src_rgb_spec = pixmap_rgb_spec(src);
            src_seed_x.raw(src_rgb_spec->seed_x);

            if(dst_bpp == 1 && BITMAP_P(dst))
                target_table = ROMlib_bw_ctab;
            else
                target_table = PIXMAP_TABLE(GD_PMAP(the_gd));

            if(!target_itab)
            {
                TheZoneGuard guard(LM(SysZone));

                target_itab = (ITabHandle)NewHandle(sizeof(ITab));
                ITAB_SEED(target_itab) = -1;
            }
            // FIXME: #warning ctm added questionable caching here
            if(target_table != cached_target_table || ITAB_SEED(target_itab) != CTAB_SEED(target_table))
            {
                MakeITable(target_table, target_itab, DEFAULT_ITABLE_RESOLUTION);
                cached_target_table = target_table;
            }
            dst_seed_x = CTAB_SEED(target_table);

            if(src_bpp != cached_src_bpp || dst_bpp != cached_dst_bpp
               || src_seed_x != cached_src_seed_x || dst_seed_x != cached_dst_seed_x)
            {
                conversion_func
                    = depthconv_make_rgb_to_ind_table(depth_table_space, dst_bpp,
                                                      nullptr,
                                                      target_table, target_itab,
                                                      src_rgb_spec);
            }
            break;
        }
        case MUNGE(RGBDirect, RGBDirect):
        {
            src_rgb_spec = pixmap_rgb_spec(src);
            dst_rgb_spec = pixmap_rgb_spec(dst);

            src_seed_x.raw(src_rgb_spec->seed_x);
            dst_seed_x.raw(dst_rgb_spec->seed_x);

            if(src_bpp != cached_src_bpp || dst_bpp != cached_dst_bpp
               || src_seed_x != cached_src_seed_x || dst_seed_x != cached_dst_seed_x)
            {
                conversion_func
                    = depthconv_make_rgb_to_rgb_table(depth_table_space, nullptr,
                                                      src_rgb_spec, dst_rgb_spec);
            }
            break;
        }
        default:
            gui_fatal("unknown pixel types");
    }

    /* Make sure we're doing something useful. */
    gui_assert((src_seed_x != dst_seed_x
                || conv_table)
               || src_bpp != dst_bpp);

    cached_src_bpp = src_bpp;
    cached_dst_bpp = dst_bpp;

    PIXMAP_ASSERT_NOT_SCREEN(dst);

    /* using BITMAP_... on a PixMap * is slimy */
    src_row_bytes = BITMAP_ROWBYTES(src);
    src_base = (uint8_t *)(src->baseAddr
                         + (rect->top - src->bounds.top) * src_row_bytes
                         + (rect->left - src->bounds.left) * src_bpp / 8);

    dst_row_bytes = BITMAP_ROWBYTES(dst);
    dst_base = (uint8_t *)dst->baseAddr;

    (*conversion_func)(depth_table_space,
                       src_base, src_row_bytes, dst_base, dst_row_bytes,
                       0, 0, height, width);

    dst->bounds = *rect;

    TEMP_ALLOC_FREE(temp_scratch_pm_bits);
}
