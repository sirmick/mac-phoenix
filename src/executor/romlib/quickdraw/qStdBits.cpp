/* Copyright 1986, 1988, 1989, 1990, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>

#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <MemoryMgr.h>
#include <ToolboxUtil.h>

#include <quickdraw/stdbits.h>
#include <quickdraw/quick.h>
#include <quickdraw/cquick.h>
#include <quickdraw/region.h>
#include <quickdraw/gworld.h>
#include <quickdraw/picture.h>
#include <mman/mman.h>
#include <commandline/flags.h>
#include <mman/tempalloc.h>

using namespace Executor;

static void ROMlib_real_copy_bits(PixMap *src, PixMap *dst,
                                  const Rect *src_rect, const Rect *dst_rect,
                                  INTEGER mode, RgnHandle mask);

static bool
src_dst_overlap_and_dst_below_src_p(const Rect *srcr, const Rect *dstr,
                                    int dh, int dv)
{
    if((dstr->top) < (srcr->bottom + dv)
       && (srcr->top + dv) <= (dstr->top)
       && (srcr->left + dh) < (dstr->right)
       && (dstr->left) < (srcr->right + dh))
        return true;
    else
        return false;
}

static inline bool
dy_zero_p(const Rect *srcr, const Rect *dstr,
          int dh, int dv)
{
    return (srcr->top + dv) == dstr->top;
}

void Executor::canonicalize_bogo_map_cleanup(const BitMap *bogo_map,
                                             struct cleanup_info *info)
{
    switch(info->cleanup_type)
    {
        case Executor::cleanup_info::cleanup_state:
            HSetState(info->data.handle, info->h_state);
            break;
        case Executor::cleanup_info::cleanup_free:
            DisposeHandle(info->data.handle);
            break;
        case Executor::cleanup_info::cleanup_unlock_gworld_pixels:
            UnlockPixels(info->data.pixmap_handle);
            break;
        case Executor::cleanup_info::cleanup_unlock_gworld_pixels_update:
        {
            gw_info_t *gw_info;

            gw_info = info->data.gw_info;

            UnlockPixels(gw_info->gw_pixmap);
            BITMAP_BASEADDR((PixMap *)bogo_map)
                = PIXMAP_BASEADDR(gw_info->gw_pixmap);
            break;
        }
        case Executor::cleanup_info::cleanup_none:
            break;
        default:
            gui_fatal("unknown cleanup type `%d'", info->cleanup_type);
    }
}

void Executor::canonicalize_bogo_map(const BitMap *bogo_map, PixMap **canonical_addr,
                                     struct cleanup_info *info)
{
    int high_bits = ((unsigned short)bogo_map->rowBytes) >> 14;
    int low_bit = ((unsigned short)bogo_map->rowBytes) & 1;

    switch(high_bits)
    {
        /* (BitMap *) */
        case 0:
        {
            PixMapHandle gd_pmap;
            PixMap *canonical = *canonical_addr;

            canonical->baseAddr = bogo_map->baseAddr;
            canonical->bounds = bogo_map->bounds;

            canonical->pmVersion = 0;

            /* no packing currently supported */
            canonical->packType = 0;
            canonical->packSize = 0;

            canonical->vRes = canonical->hRes = 72 << 16;

            gd_pmap = GD_PMAP(LM(TheGDevice));
            if(canonical->baseAddr == PIXMAP_BASEADDR(gd_pmap))
            {
                pixmap_set_pixel_fields(canonical, PIXMAP_PIXEL_SIZE(gd_pmap));
                canonical->rowBytes = (PIXMAP_ROWBYTES(gd_pmap)
                                       | PIXMAP_DEFAULT_ROW_BYTES);
                canonical->pmTable = PIXMAP_TABLE(gd_pmap);

                info->cleanup_type = Executor::cleanup_info::cleanup_none;
            }
            else
            {
                pixmap_set_pixel_fields(canonical, 1);

                canonical->rowBytes = (bogo_map->rowBytes
                                       | PIXMAP_DEFAULT_ROW_BYTES);
                info->cleanup_type = Executor::cleanup_info::cleanup_none;
                canonical->pmTable = validate_relative_bw_ctab();
            }
            canonical->planeBytes = 0;

            canonical->pmReserved = 0;
            break;
        }

        /* (unknown) */
        case 1:
            gui_abort();

        /* pointer to pixmap */
        case 2:
        {
            gw_info_t *gw_info;

            gw_info = (lookup_gw_info_by_gw_pixmap_baseaddr(BITMAP_BASEADDR((PixMap *)bogo_map)));
            if(gw_info)
            {
                LockPixels(gw_info->gw_pixmap);
                BITMAP_BASEADDR((PixMap *)bogo_map)
                    = PIXMAP_BASEADDR(gw_info->gw_pixmap);

                info->cleanup_type = Executor::cleanup_info::cleanup_unlock_gworld_pixels_update;
                info->data.gw_info = gw_info;
            }
            else
                info->cleanup_type = Executor::cleanup_info::cleanup_none;

            *canonical_addr = (PixMap *)bogo_map;
            break;
        }

        /* (CGrafPtr spew) */
        case 3:
            if(low_bit)
            {
                CGrafPtr gw;
                PixMapHandle pixmap;

                gw = (CGrafPtr)((char *)bogo_map - 2);
                pixmap = CPORT_PIXMAP(gw);

                *canonical_addr = *pixmap;
                info->cleanup_type = Executor::cleanup_info::cleanup_unlock_gworld_pixels;
                info->data.pixmap_handle = pixmap;

                /* lock the pixels */
                LockPixels(pixmap);
            }
            else
            {
                PixMapHandle pixmap_handle = *(GUEST<PixMapHandle> *)bogo_map;

                *canonical_addr = *pixmap_handle;

                info->cleanup_type = Executor::cleanup_info::cleanup_state;
                info->data.pixmap_handle = pixmap_handle;
                info->h_state = HGetState((Handle)pixmap_handle);

                /* lock the pixmap handle */
                HSetState((Handle)pixmap_handle, info->h_state | LOCKBIT);

                break;
            }
    }
#if !defined(NDEBUG)
    if((RECT_WIDTH(&BITMAP_BOUNDS(*canonical_addr))
        * (*canonical_addr)->pixelSize
        / 8)
       > BITMAP_ROWBYTES(*canonical_addr))
        warning_unexpected("unlikely map");
#endif
}

static void
write_copybits_picdata(PixMap *src, PixMap *dst,
                       const Rect *src_rect, const Rect *dst_rect,
                       int16_t mode, RgnHandle mask)
{
    int32_t zero = 0;
    int16_t opcode;
    GUEST<int16_t> swapped_mode;
    int16_t row_bytes;
    GUEST<int16_t> temp_pixmap_row_bytes;
    GUEST<int16_t> pack_type;
    int16_t pixel_size;
    int height;
    int i;
    bool direct_bits_p;

    {
        /* always copy the src pixmap since the code below, as it is
       written, will always record the _entire_ src, which is very bad
       if the src is the screen, or some other large image */
        /* it is also necessary to copy when we are the src is the actual
       screen */
        PixMap *_src;
        Rect *_src_rect;

        _src = (PixMap *)alloca(sizeof *_src);
        _src_rect = (Rect *)alloca(sizeof *_src_rect);

        TheZoneGuard guard(LM(ApplZone));

        if(FreeMemSys() >= FreeMem())
            LM(TheZone) = LM(SysZone);

        pixmap_copy(src, src_rect,
                    _src, _src_rect);

        src = _src;
        src_rect = _src_rect;
    }

    row_bytes = BITMAP_ROWBYTES(src);
    pixel_size = src->pixelSize;

    direct_bits_p = (pixel_size == 16 || pixel_size == 32);
    if(pixel_size == 32)
        pack_type = 2;
    else
        pack_type = 0;

    if(direct_bits_p)
        opcode = mask ? OP_DirectBitsRgn : OP_DirectBitsRect;
    else if(row_bytes < 8)
        opcode = mask ? OP_BitsRgn : OP_BitsRect;
    else
        opcode = mask ? OP_PackBitsRgn : OP_PackBitsRect;

    ROMlib_drawingpicupdate();
    PICOP(opcode);

    if(direct_bits_p)
    {
        GUEST<int32_t> swapped_bogo_baseaddr = 0xFF;

        PICWRITE(&swapped_bogo_baseaddr, sizeof swapped_bogo_baseaddr);
    }

    temp_pixmap_row_bytes = src->rowBytes | PIXMAP_DEFAULT_ROW_BYTES;
    PICWRITE(&temp_pixmap_row_bytes, sizeof temp_pixmap_row_bytes);

    PICWRITE(&src->bounds, sizeof src->bounds);
    PICWRITE(&src->pmVersion, sizeof src->pmVersion);

    /* pack type of bits stored in the picture may not necessarily match
     the pack type of the actual pixmap */
    PICWRITE(&pack_type, sizeof pack_type);

    PICWRITE(&src->packSize, sizeof src->packSize);
    PICWRITE(&src->hRes, sizeof src->hRes);
    PICWRITE(&src->vRes, sizeof src->vRes);
    PICWRITE(&src->pixelType, sizeof src->pixelType);
    PICWRITE(&src->pixelSize, sizeof src->pixelSize);
    PICWRITE(&src->cmpCount, sizeof src->cmpCount);
    PICWRITE(&src->cmpSize, sizeof src->cmpSize);
    PICWRITE(&src->planeBytes, sizeof src->planeBytes);
    PICWRITE(&zero, sizeof zero);
    PICWRITE(&src->pmReserved, sizeof src->pmReserved);

    if(!direct_bits_p)
    {
        HLockGuard guard(src->pmTable);
        CTabPtr ctab;

        ctab = (*src->pmTable);

        /* write out the src color table */
        PICWRITE(&zero, sizeof zero);
        PICWRITE(&ctab->ctFlags, sizeof ctab->ctFlags);
        PICWRITE(&ctab->ctSize, sizeof ctab->ctSize);
        for(i = 0; i <= ctab->ctSize; i++)
        {
            ColorSpec *elt;

            elt = &ctab->ctTable[i];
            PICWRITE(&elt->value, sizeof elt->value);
            PICWRITE(&elt->rgb, sizeof elt->rgb);
        }
    }

    PICWRITE(src_rect, sizeof *src_rect);
    PICWRITE(dst_rect, sizeof *dst_rect);
    swapped_mode = mode;
    PICWRITE(&swapped_mode, sizeof swapped_mode);
    if(mask)
    {
        HLockGuard guard(mask);
        PICWRITE(*mask, (*mask)->rgnSize);
    }
    height = RECT_HEIGHT(&src->bounds);
    if(row_bytes < 8 || pack_type == 2)
    {
        if(pack_type == 2)
        {
            uint8_t *current, *end;

            current = (uint8_t *)src->baseAddr + 1;
            end = current + row_bytes * height;

            while(current < end)
            {
                PICWRITE(current, 3 * sizeof *current);
                current += 4;
            }
            if((row_bytes / 4 * height) & 1)
                PICWRITE("", 1);
        }
        else
            PICWRITE(src->baseAddr, row_bytes * height);
    }
    else
    {
        Ptr ip;

        int parity;
        uint8_t *packed_line;
        int8_t *countloc;
        int16_t count, countsize;
        GUEST<int16_t> swappedcount;
        uint8_t *baseaddr;

        /* i copied the code below from the executor 1.2 implementation
         of StdBits pic recording.  i can't say i fully understand it */

        /* #### why the extra 5 bytes? */
        packed_line = (uint8_t *)alloca(row_bytes + 5);
        baseaddr = (uint8_t *)src->baseAddr;
        ip = (Ptr)baseaddr;
        parity = 0;

        if(row_bytes > 250)
        {
            countloc = (int8_t *)&swappedcount;
            countsize = 2;
        }
        else
        {
            countloc = (int8_t *)&swappedcount + 1;
            countsize = 1;
        }

        for(i = 0; i < height; i++)
        {
            GUEST<Ptr> op = (Ptr)packed_line;
            gui_assert((uint8_t *)ip == &baseaddr[row_bytes * i]);
            GUEST<Ptr> ip2 = ip;
            PackBits(&ip2, &op, row_bytes);
            ip = ip2;
            count = op - (Ptr)packed_line;
            parity += count + countsize;
            swappedcount = count;
            PICWRITE(countloc, countsize);
            PICWRITE(packed_line, count);
        }
        /* even things up */
        if(parity & 1)
            PICWRITE("", 1);
    }

    pixmap_free_copy(src);
}

void Executor::ROMlib_bogo_stdbits(BitMap *src_bogo_map, BitMap *dst_bogo_map,
                                   const Rect *src_rect, const Rect *dst_rect,
                                   short mode, RgnHandle mask)
{
    Rect dummy_rect;

    PixMap dummy_space[2];
    PixMap *src = &dummy_space[0], *dst = &dummy_space[1];

    struct cleanup_info cleanup_info[2];

    if(dst_rect->bottom <= dst_rect->top
       || dst_rect->right <= dst_rect->left
       || (mask && !SectRect(dst_rect, &(*mask)->rgnBBox, &dummy_rect)))
        return;

    /* the incoming source and destinations can be any one of the
     following;

     a `BitMap *', the high two bits of rowBytes must be zero,
     or CGrafPort coerced to a GrafPort with the PortBits
     passed in; in this case the high two bits of rowBytes
     must be `11'; and the baseAddr field is a `PixMapHandle'

     each of these types are canonicalized into a `PixMap *', which
     is what we work with here */
    /* these functions lock the PixMapHandle if src or dst
     are from a coerced CGrafPort; we must make sure
     to unlock those handles on the way out */
    canonicalize_bogo_map(src_bogo_map, &src, &cleanup_info[0]);
    canonicalize_bogo_map(dst_bogo_map, &dst, &cleanup_info[1]);

    if(qdGlobals().thePort->picSave)
    {
        write_copybits_picdata(src, dst, src_rect, dst_rect, mode, mask);
    }

    if(PORT_PEN_VIS(qdGlobals().thePort) < 0)
        return;

    ROMlib_real_copy_bits(src, dst, src_rect, dst_rect, mode, mask);

    canonicalize_bogo_map_cleanup(src_bogo_map, &cleanup_info[0]);
    canonicalize_bogo_map_cleanup(dst_bogo_map, &cleanup_info[1]);
}

static void StdBitsPicSaveFlag(const BitMap *src_bogo_map,
                                  const Rect *src_rect, const Rect *dst_rect,
                                  INTEGER mode, RgnHandle mask,
                                  Boolean savepic)
{
    Rect dummy_rect;

    /* we want the actual port bits, no fooling; so don't use the
     accessor macros */
    BitMap *dst_bogo_map = &qdGlobals().thePort->portBits;

    PixMap dummy_space[2];
    PixMap *src = &dummy_space[0], *dst = &dummy_space[1];

    struct cleanup_info cleanup_info[2];

    if(dst_rect->bottom <= dst_rect->top
       || dst_rect->right <= dst_rect->left
       || (mask && !SectRect(dst_rect, &(*mask)->rgnBBox, &dummy_rect)))
        return;

    /* the incoming source and destinations can be any one of the
     following;

     a `BitMap *', the high two bits of rowBytes must be zero,
     or CGrafPort coerced to a GrafPort with the PortBits
     passed in; in this case the high two bits of rowBytes
     must be `11'; and the baseAddr field is a `PixMapHandle'

     each of these types are canonicalized into a `PixMap *', which
     is what we work with here */
    /* these functions lock the PixMapHandle if src or dst
     are from a coerced CGrafPort; we must make sure
     to unlock those handles on the way out */
    canonicalize_bogo_map(src_bogo_map, &src, &cleanup_info[0]);
    canonicalize_bogo_map(dst_bogo_map, &dst, &cleanup_info[1]);

    if(savepic)
    {
        if(qdGlobals().thePort->picSave)
        {
            write_copybits_picdata(src, dst, src_rect, dst_rect, mode, mask);
        }
    }

    if(PORT_PEN_VIS(qdGlobals().thePort) < 0)
        return;

    ROMlib_real_copy_bits(src, dst, src_rect, dst_rect, mode, mask);

    canonicalize_bogo_map_cleanup(src_bogo_map, &cleanup_info[0]);
    canonicalize_bogo_map_cleanup(dst_bogo_map, &cleanup_info[1]);
}

void Executor::C_StdBits(const BitMap *src_bogo_map, const Rect *src_rect,
                         const Rect *dst_rect, INTEGER mode,
                         RgnHandle mask) /* destination is alawys the current port */
{
    StdBitsPicSaveFlag(src_bogo_map, src_rect, dst_rect, mode, mask, true);
}

static void
ROMlib_real_copy_bits_helper(PixMap *src, PixMap *dst,
                             const Rect *src_rect, const Rect *dst_rect,
                             INTEGER mode, RgnHandle mask)
{
    GDHandle the_gd;
    PixMapHandle the_gd_pmap;
    GrafPtr current_port;

    Rect tmp_mask_rect;
    uint32_t bk_color, fg_color;

    /* region used to compute the complete mask region */
    RgnHandle mask_region;

    /* bits of a pixel position in `src' that have a resolution
     below that of a byte */
    int dst_sub_byte_bits;

    /* final depth of the destination */
    int dst_depth;

    const rgb_spec_t *dst_rgb_spec;

    TEMP_ALLOC_DECL(temp_depth_bits);
    TEMP_ALLOC_DECL(temp_scale_bits);
    TEMP_ALLOC_DECL(temp_overlap_bits);

    the_gd = LM(TheGDevice);
    the_gd_pmap = GD_PMAP(the_gd);
    current_port = qdGlobals().thePort;

    dst_rgb_spec = pixmap_rgb_spec(dst);
    dst_depth = dst->pixelSize;

    switch(dst_depth)
    {
        case 1:
            dst_sub_byte_bits = 7;
            break;
        case 2:
            dst_sub_byte_bits = 3;
            break;
        case 4:
            dst_sub_byte_bits = 1;
            break;
        default:
            dst_sub_byte_bits = 0;
            break;
    }

    ROMlib_fg_bk(&fg_color, &bk_color, nullptr, nullptr, dst_rgb_spec,
                 active_screen_addr_p(dst), dst_depth <= 8);

    if(dst_depth == 1)
    {   // FIXME: HACK - do not colorize when dst is 1-bit
        // need to check behaviour of real macs,
        // maybe this is only needed because CopyMask calls CopyBits
        // to copy the mask without setting a port
        fg_color = 1;
        bk_color = 0;
    }

    /* if the source and dest differ in depths, perform a depth
     conversion on the src, so it matches that of the depth */
    if(src->pixelSize != dst->pixelSize
       || (src->pmTable != dst->pmTable && (src->pixelSize < 16
                                            && (CTAB_SEED(src->pmTable)
                                                    /* we assume the destination has the same color table as
		  the current graphics device */
                                                    != CTAB_SEED(PIXMAP_TABLE(GD_PMAP(the_gd)))
                                                && CTAB_SEED(src->pmTable) != 0))))
    {
        PixMap *new_src = (PixMap *)alloca(sizeof(PixMap));
        /* convert_pixmap expects the src rect to be aligned to byte
	 boundaries; compute that in `widened_src_rect' */
        Rect *widened_src_rect = (Rect *)alloca(sizeof(Rect));
        int src_depth, src_sub_byte_bits;
        void *new_src_bits;
        int n_bytes_needed;

        src_depth = src->pixelSize;

        switch(src_depth)
        {
            case 1:
                src_sub_byte_bits = 7;
                break;
            case 2:
                src_sub_byte_bits = 3;
                break;
            case 4:
                src_sub_byte_bits = 1;
                break;
            default:
                src_sub_byte_bits = 0;
                break;
        }

        widened_src_rect->top = src_rect->top;
        widened_src_rect->bottom = src_rect->bottom;

        /* translate the left (right) src_rect coords to absolute bitmap
	 coordinates; and round down (up) to the byte boundary, and
	 re-translate to boundary-relative bitmap coords */
        widened_src_rect->left
            = ((src_rect->left - src->bounds.left)
                  & ~src_sub_byte_bits)
                 + src->bounds.left;
        widened_src_rect->right
            = (((src_rect->right - src->bounds.left)
                   + src_sub_byte_bits)
                  & ~src_sub_byte_bits)
                 + src->bounds.left;

        /* the new_src should `be a pixmap' (have the pixmap bits set in
         the rowBytes) only if the dst is a pixmap; convert_pixmap
         does different things if the destination is a bitmap */
        new_src->rowBytes = (PIXMAP_DEFAULT_ROW_BYTES
                             | ((((RECT_WIDTH(widened_src_rect)
                                     * dst_depth)
                                    + 31)
                                   / 32)
                                  * 4));

        /* Allocate temporary storage for the new_src_bits bitmap. */

        n_bytes_needed = (BITMAP_ROWBYTES(new_src)
                          * (src_rect->bottom - src_rect->top));

        TEMP_ALLOC_ALLOCATE(new_src_bits, temp_depth_bits, n_bytes_needed);
        new_src->baseAddr = (Ptr)new_src_bits;

        pixmap_set_pixel_fields(new_src, dst_depth);
        new_src->pmTable = PIXMAP_TABLE(the_gd_pmap);

        /* don't initialize the color table of new_src; we assume that
	 it has the same color space as the current graphics device */

        convert_pixmap(src, new_src, widened_src_rect, nullptr);

        src = new_src;
        /* `convert_pixmap ()' jukes the coordinates of the
	 new_src bitmap so that `src_rect' is still correct */
    }

    /* if the source and dest rectangles are not equivalent, scale the
     source rectangle */
    if(RECT_WIDTH(src_rect) != RECT_WIDTH(dst_rect)
       || RECT_HEIGHT(src_rect) != RECT_HEIGHT(dst_rect))
    {
        PixMap *new_src = (PixMap *)alloca(sizeof(PixMap));
        int new_src_row_bytes;
        void *scale_base;

        new_src_row_bytes
            = (((RECT_WIDTH(dst_rect) * dst_depth
                 + /* dst_sub_byte_bits */ 7)
                / 8)
               + 3)
            & ~3;
        new_src->rowBytes = new_src_row_bytes | PIXMAP_DEFAULT_ROW_BYTES;

        TEMP_ALLOC_ALLOCATE(scale_base, temp_scale_bits,
                            new_src_row_bytes * RECT_HEIGHT(dst_rect));
        new_src->baseAddr = (Ptr)scale_base;

        pixmap_set_pixel_fields(new_src, dst_depth);

        scale_blt_bitmap((blt_bitmap_t *)src,
                         (blt_bitmap_t *)new_src,
                         src_rect, dst_rect, ROMlib_log2[dst_depth]);

        src = new_src;
        src_rect = dst_rect;
    }

    /* compute the mask region before checking if the source and dest
     overlap becuase we only double buffer if dy is nonzero */
    /* intersect the region mask with qdGlobals().thePort bounds, qdGlobals().thePort rect, the
     destination rect, and the port {clip, vis} regions */
    mask_region = NewRgn();

    SectRect(dst_rect, &dst->bounds, &tmp_mask_rect);
    RectRgn(mask_region, &tmp_mask_rect);
    SectRgn(PORT_CLIP_REGION(current_port), mask_region, mask_region);
    SectRgn(PORT_VIS_REGION(current_port), mask_region, mask_region);
    if(mask)
        SectRgn(mask, mask_region, mask_region);

    if(src->baseAddr == dst->baseAddr
       && (src_dst_overlap_and_dst_below_src_p(src_rect, dst_rect,
                                               dst->bounds.left - src->bounds.left,
                                               dst->bounds.top - src->bounds.top))
       && (!RGN_SMALL_P(mask_region)
           || dy_zero_p(src_rect, dst_rect,
                        dst->bounds.left - src->bounds.left,
                        dst->bounds.top - src->bounds.top)))
    {
        PixMap *new_src;
        void *overlap_bits;

        Rect copy_rect;
        Rect clipped_src_rect;
        int height, offset;

        /* the source and destination are overlapped;
	 create a new src bitmap */
        new_src = (PixMap *)alloca(sizeof *new_src);

        SectRect(&src->bounds, src_rect, &clipped_src_rect);

        height = RECT_HEIGHT(&clipped_src_rect);
        offset = clipped_src_rect.top - src_rect->top;

        copy_rect.top = src_rect->top + offset;
        copy_rect.bottom = src_rect->top + offset + height;
        copy_rect.left = src->bounds.left;
        copy_rect.right = src->bounds.right;

        new_src->rowBytes = src->rowBytes;
        TEMP_ALLOC_ALLOCATE(overlap_bits, temp_overlap_bits,
                            height * BITMAP_ROWBYTES(src));
        new_src->baseAddr = (Ptr)overlap_bits;
        new_src->bounds = copy_rect;

        pixmap_set_pixel_fields(new_src, dst_depth);
        new_src->pmTable = src->pmTable;

        /* pause picture recording since we don't want the recursive
	 call to copy the overlapping image to be recorded */

        {
            uint32_t black_pixel, white_pixel;
            RgnHandle rgn;

            rgn = NewRgn();
            RectRgn(rgn, &copy_rect);

            pixmap_black_white(new_src, &black_pixel, &white_pixel);

            ROMlib_blt_rgn_update_dirty_rect(rgn, srcCopy,
                                             false, dst_depth,
                                             src, new_src,
                                             &copy_rect, &copy_rect,
                                             black_pixel, white_pixel);
            DisposeRgn(rgn);
        }

        src = new_src;
    }

    ROMlib_blt_rgn_update_dirty_rect(mask_region, mode, false, dst_depth,
                                     src, dst, src_rect, dst_rect,
                                     fg_color, bk_color);

    DisposeRgn(mask_region);

    TEMP_ALLOC_FREE(temp_overlap_bits);
    TEMP_ALLOC_FREE(temp_scale_bits);
    TEMP_ALLOC_FREE(temp_depth_bits);
}

#if 1

/*
 * If we're copying a big 1-bpp bitmap to a small 8-bpp pixmap, we really
 * want to first copy to a temporary small 1-bpp bitmap and then copy that
 * to a small 8-bpp pixmap, otherwise we expand our large bitmap by a factor
 * of 8, which can be excessive.
 */

static void
ROMlib_real_copy_bits(PixMap *src, PixMap *dst,
                      const Rect *src_rect, const Rect *dst_rect,
                      INTEGER mode, RgnHandle mask)
{
    bool shrink_first_p;

    if(src->pixelSize == dst->pixelSize)
        shrink_first_p = false;
    else
    {
        int default_nbits, new_nbits;

        default_nbits = (RECT_WIDTH(src_rect) * dst->pixelSize * RECT_HEIGHT(src_rect));

        new_nbits = ((RECT_WIDTH(dst_rect) * src->pixelSize * RECT_HEIGHT(dst_rect)) + (RECT_WIDTH(dst_rect) * dst->pixelSize * RECT_HEIGHT(dst_rect)));
        shrink_first_p = (new_nbits < default_nbits);
    }

    if(!shrink_first_p)
        ROMlib_real_copy_bits_helper(src, dst, src_rect, dst_rect, mode, mask);
    else
    {
        PixMap *new_src;
        int temp_bytes_needed;
        void *temp_bits;
        INTEGER temp_row_bytes;
        int src_depth;
        TEMP_ALLOC_DECL(temp_alloc_bits);

        new_src = (PixMap *)alloca(sizeof *new_src);
        src_depth = src->pixelSize;
        temp_row_bytes = (RECT_WIDTH(dst_rect) * src_depth + 31) / 32 * 4;
        temp_bytes_needed = temp_row_bytes * RECT_HEIGHT(dst_rect);
        TEMP_ALLOC_ALLOCATE(temp_bits, temp_alloc_bits, temp_bytes_needed);

        *new_src = *src;
        new_src->baseAddr = (Ptr)temp_bits;
        new_src->rowBytes = temp_row_bytes | PIXMAP_DEFAULT_ROW_BYTES;
        new_src->bounds = *dst_rect;

        scale_blt_bitmap((blt_bitmap_t *)src, (blt_bitmap_t *)new_src,
                         src_rect, dst_rect, ROMlib_log2[src_depth]);
        ROMlib_real_copy_bits_helper(new_src, dst, dst_rect, dst_rect, mode,
                                     mask);

        TEMP_ALLOC_FREE(temp_alloc_bits);
    }
}
#endif
