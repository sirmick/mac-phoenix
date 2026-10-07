/* Copyright 1986-1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <QuickDraw.h>
#include <MenuMgr.h>
#include <WindowMgr.h>
#include <FontMgr.h>
#include <MemoryMgr.h>
#include <ToolboxEvent.h>
#include <Iconutil.h>

#include <menu/menu.h>
#include <quickdraw/quick.h>
#include <quickdraw/cquick.h>
#include <wind/wind.h>
#include <algorithm>

using namespace Executor;

static Rect *current_menu_rect;

#define TOP_ARROW_P() \
    (LM(TopMenuItem) < current_menu_rect->top)
#define BOTTOM_ARROW_P() \
    (LM(AtMenuBottom) > current_menu_rect->bottom)

static int16_t checksize, cloversize, lineheight, ascent;

#define UP (1 << 0)
#define DOWN (1 << 1)

typedef enum {
    uparrow,
    downarrow,
} arrowtype;

/*
 * I think SICNFLAG is correct.  I'm guessing from what I've seen in MacMan
 * and some sort of dollhouse construction set.  MacMan doesn't have any icon
 * sicn conflicts, but the dollhouse construction set did (though it's on a
 * CD-ROM somewhere and I don't know where).
 */

#define SICN_FLAG 0x1E
#define REDUCED_ICON_FLAG 0x1D

/* return true if there is an icon */

void Executor::cleanup_icon_info(icon_info_t *info)
{
    if(info->icon
       && info->color_icon_p)
    {
        DisposeCIcon((CIconHandle)info->icon);
    }
}

int Executor::get_icon_info(mextp item_info, icon_info_t *info, int need_icon_p)
{
    Handle h = nullptr;

    /* my default */
    info->width = info->height = 0;
    info->icon = nullptr;

    if(item_info->micon)
    {
        if(item_info->mkeyeq != SICN_FLAG)
        {
            h = GetResource("cicn"_4, 256 + item_info->micon);
            if(h)
            {
                CIconHandle icon;
                Rect *bounds;

                info->color_icon_p = true;

                icon = (CIconHandle)h;
                bounds = &(CICON_PMAP(icon).bounds);
                info->width = RECT_WIDTH(bounds) + ICON_PAD;
                info->height = RECT_HEIGHT(bounds) + ICON_PAD;
            }
            else
            {
                h = GetResource("ICON"_4, 256 + item_info->micon);
                if(h)
                {
                    info->color_icon_p = false;
                    info->width = 32 + ICON_PAD;
                    info->height = 32 + ICON_PAD;
                }
            }
        }
        if(h && item_info->mkeyeq == REDUCED_ICON_FLAG)
        {
            info->width = 16 + ICON_PAD;
            info->height = 16 + ICON_PAD;
        }
        else if(!h)
        {
            if(item_info->mkeyeq == SICN_FLAG)
                h = GetResource("SICN"_4, 256 + item_info->micon);
            if(h)
            {
                info->color_icon_p = false;
                info->width = 16 + ICON_PAD;
                info->height = 16 + ICON_PAD;
            }
            else
                return false;
        }
        gui_assert(h);
        if(need_icon_p)
        {
            if(info->color_icon_p)
                info->icon = (Handle)GetCIcon(256 + item_info->micon);
            else
            {
                info->icon = h;
                if(!*h)
                    LoadResource(h);
            }
        }
        return true;
    }
    else
        return false;
}

/* See IMV-236 */

static Boolean iskeyequiv(struct table::tableentry *tp)
{
    Boolean retval;

    if(tp->options->mkeyeq)
        retval = (tp->options->mkeyeq < 0x1b || tp->options->mkeyeq > 0x1f);
    else
        retval = false;

    return retval;
}

void size_menu(MenuHandle mh, tablePtr tablep)
{
    struct table::tableentry *tp, *ep;
    int16_t width, height, actual_height, max_height;

    width = height = actual_height = 0;
    /* the 32 is just a guess */
    max_height = qdGlobals().screenBits.bounds.bottom - 32;
    for(tp = tablep->entry, ep = tp + tablep->count; tp != ep; tp++)
    {
        icon_info_t icon_info;
        int w;

        w = checksize + 2;

        get_icon_info(tp->options, &icon_info, false);
        w += icon_info.width;

        height += tp[1].top - tp[0].top;
        TextFace(tp->options->mstyle);
        w += StringWidth(tp->name);
#if 0
      if (iskeyequiv(tp) || tp->options->mkeyeq == 0x1B)
#endif
        w += 2 * cloversize + 1;
        if(height < max_height)
            actual_height = height;
        if(width < w)
            width = w;
    }
    TextFace(0);
    (*mh)->menuWidth = width;
    (*mh)->menuHeight = actual_height;
}

static void
draw_right_arrow(Rect *menu_rect, MenuHandle mh, int item, int invert_p)
{
    Rect dst_rect;
    BitMap arrow_bitmap;
    static const unsigned char right_arrow_bits[] = {
        image_bits(10000000),
        image_bits(11000000),
        image_bits(11100000),
        image_bits(11110000),
        image_bits(11111000),
        image_bits(11111100),
        image_bits(11111000),
        image_bits(11110000),
        image_bits(11100000),
        image_bits(11000000),
        image_bits(10000000),
    };
    int x, y;

    arrow_bitmap.baseAddr = (Ptr)right_arrow_bits;
    arrow_bitmap.rowBytes = 1;
    SetRect(&arrow_bitmap.bounds, 0, 0, /* right, bottom */ 6, 11);

    y = menu_rect->top + 2;
    x = menu_rect->right - 14;

    dst_rect.top = y;
    dst_rect.left = x;
    dst_rect.bottom = y + 11;
    dst_rect.right = x + 6;

    CopyBits(&arrow_bitmap, PORT_BITS_FOR_COPY(qdGlobals().thePort),
             &arrow_bitmap.bounds, &dst_rect, srcCopy, nullptr);
}

static void
draw_arrow(Rect *menu_rect, MenuHandle mh, arrowtype arrdir)
{
    BitMap arrow_bitmap;
    Rect dst_rect, erase_rect;
    int top_of_item;
    RGBColor bk_color, title_color;
    unsigned char up_arrow_bits[] = {
        image_bits(00000100), image_bits(00000000),
        image_bits(00001110), image_bits(00000000),
        image_bits(00011111), image_bits(00000000),
        image_bits(00111111), image_bits(10000000),
        image_bits(01111111), image_bits(11000000),
        image_bits(11111111), image_bits(11100000),
    };
    unsigned char down_arrow_bits[] = {
        image_bits(11111111), image_bits(11100000),
        image_bits(01111111), image_bits(11000000),
        image_bits(00111111), image_bits(10000000),
        image_bits(00011111), image_bits(00000000),
        image_bits(00001110), image_bits(00000000),
        image_bits(00000100), image_bits(00000000),
    };

    if(arrdir == uparrow)
    {
        arrow_bitmap.baseAddr = (Ptr)up_arrow_bits;
        arrow_bitmap.rowBytes = 2;
        SetRect(&arrow_bitmap.bounds, 0, 0, /* right, bottom */ 11, 6);

        top_of_item = menu_rect->top;
    }
    else if(arrdir == downarrow)
    {
        arrow_bitmap.baseAddr = (Ptr)down_arrow_bits;
        arrow_bitmap.rowBytes = 2;
        SetRect(&arrow_bitmap.bounds, 0, 0, /* right, bottom */ 11, 6);

        top_of_item = menu_rect->bottom - lineheight;
    }
    else
        gui_abort();

    menu_bk_color(MI_ID(mh), &bk_color);
    menu_title_color(MI_ID(mh), &title_color);

    RGBForeColor(&title_color);
    RGBBackColor(&bk_color);

    erase_rect.left = menu_rect->left;
    erase_rect.right = menu_rect->right;
    erase_rect.top = top_of_item;
    erase_rect.bottom = top_of_item + lineheight;
    EraseRect(&erase_rect);

    dst_rect.top = top_of_item + 5;
    dst_rect.left = menu_rect->left + checksize;
    dst_rect.bottom = top_of_item + 5 + /* arrows are `6' tall */ 6;
    dst_rect.right = menu_rect->left + checksize
                        + /* arrows are `11' wide */ 11;
    CopyBits(&arrow_bitmap, PORT_BITS_FOR_COPY(qdGlobals().thePort),
             &arrow_bitmap.bounds, &dst_rect, srcCopy, nullptr);

    /* resent the fg/bk colors */
    RGBForeColor(&ROMlib_black_rgb_color);
    RGBBackColor(&ROMlib_white_rgb_color);
}

static void erasearrow(Rect *, tablePtr, Boolean);
static void popuprect(MenuHandle, Rect *, Point, GUEST<INTEGER> *, tablePtr);

static void erasearrow(Rect *rp, tablePtr tablep, Boolean upordown)
{
    Rect r;
    INTEGER x, y;

    x = rp->left + checksize;
    if(upordown == UP)
        y = rp->top + 5;
    else
        y = rp->bottom - lineheight + 5;
    r.top = y;
    r.left = x;
    r.bottom = y + 6;
    r.right = x + 11;
    EraseRect(&r);
}

static void
draw_item(Rect *rp, struct table::tableentry *tp, int32_t bit, int item, MenuHandle mh,
          int invert_p)
{
    RGBColor bk_color, mark_color, title_color, command_color;
    int16_t top, bottom, v;
    Rect rtmp;
    int draw_right_arrow_p;
    icon_info_t icon_info;
    int draw_icon_p;
    int divider_p;
    int active_p;
    bool dither_p, dither_cmd_p;

    dither_p = false;
    dither_cmd_p = false;
    draw_right_arrow_p = tp->options->mkeyeq == 0x1B;
    divider_p = tp->name[0] && (tp->name[1] == '-');
    /* active vs grayed out */
    bit |= 1;
    active_p = !((MI_ENABLE_FLAGS(mh) & bit) != bit
                 || divider_p);

    top = tp[0].top + LM(TopMenuItem);
    bottom = tp[1].top + LM(TopMenuItem);

    v = top + ascent;

    draw_icon_p = get_icon_info(tp->options, &icon_info, true);
    if(draw_icon_p)
        v += (icon_info.height - lineheight) / 2;

    menu_item_colors(MI_ID(mh), item,
                     &bk_color, &title_color, &mark_color, &command_color);
#define FAIL goto failure
#define DONE goto done
#define DO_BLOCK_WITH_FAILURE(try_block, fail_block) \
    {                                                \
        {                                            \
            try_block                                \
        }                                            \
        goto done;                                   \
    failure:                                         \
    {                                                \
        fail_block                                   \
    }                                                \
    /* fall through */                               \
    done:;                                           \
    }
    if(!active_p)
    {
        gui_assert(!invert_p);

        DO_BLOCK_WITH_FAILURE({
	  if (!AVERAGE_COLOR (&bk_color, &title_color, 0x8000,
			      &title_color))
	    FAIL;
	  if (!AVERAGE_COLOR (&bk_color, &mark_color, 0x8000,
			      &mark_color))
	    FAIL;
	  if (!AVERAGE_COLOR (&bk_color, &command_color, 0x8000,
			      &command_color))
	    FAIL; },
                              {
                                  dither_p = true;
                                  dither_cmd_p = true;

                                  if(memcmp(&bk_color,
                                            &ROMlib_white_rgb_color, sizeof(RGBColor)))
                                      warning_unexpected("can't dither menu item");
                                  title_color = mark_color = command_color = ROMlib_black_rgb_color;
                              });
    }
    RGBBackColor(invert_p ? &title_color : &bk_color);

    rtmp.top = top;
    rtmp.bottom = bottom;
    rtmp.left = rp->left;
    rtmp.right = rp->right;

    EraseRect(&rtmp);

    if(tp->options->mmarker
       && !divider_p
       && !draw_right_arrow_p)
    {
        RGBForeColor(invert_p ? &bk_color : &mark_color);
        MoveTo(rp->left + 2, v);
        DrawChar(tp->options->mmarker);
    }

    /* draw bw icons with the text color (?) */
    RGBForeColor(invert_p ? &bk_color : &title_color);

    /* draw the icon */
    if(draw_icon_p)
    {
        rtmp.top = top + ICON_PAD / 2;
        rtmp.left = rp->left + checksize + 2;
        rtmp.bottom = rtmp.top + (icon_info.height - ICON_PAD);
        rtmp.right = rtmp.left + (icon_info.width - ICON_PAD);

        if(icon_info.color_icon_p)
            PlotCIcon(&rtmp, (CIconHandle)icon_info.icon);
        else
            PlotIcon(&rtmp, icon_info.icon);
    }

    if(divider_p)
    {
        RGBForeColor(&title_color);
        MoveTo(rp->left, v - 4);
        LineTo(rp->right, v - 4);
    }
    else
    {
        RGBForeColor(invert_p ? &bk_color : &title_color);

        MoveTo(rp->left + icon_info.width + checksize + 2, v);
        TextFace(tp->options->mstyle);
        DrawString(tp->name);
        TextFace(0);
    }
    rtmp.left = rp->left;
    rtmp.right = rp->right;
    rtmp.top = top;
    rtmp.bottom = bottom;
    if((iskeyequiv(tp) || draw_right_arrow_p)
       && !divider_p)
    {
        if(draw_right_arrow_p)
        {
            RGBForeColor(invert_p ? &bk_color : &title_color);
            draw_right_arrow(&rtmp, mh, item, invert_p);
        }
        else
        {
            INTEGER new_left;

            RGBForeColor(invert_p ? &bk_color : &command_color);

            new_left = rp->right - (2 * cloversize + 1);
            MoveTo(new_left, v);
            DrawChar(commandMark);
            DrawChar(tp->options->mkeyeq);
            if(dither_cmd_p && !dither_p)
            {
                Rect r;

                r = rtmp;
                r.left = new_left;
                PenMode(notPatBic);
                PenPat(&qdGlobals().gray);
                PaintRect(&r);
                PenPat(&qdGlobals().black);
                PenMode(patCopy);
            }
        }
    }
    /* if the entire table is disabled, so is every entry */
    if(dither_p)
    {
        PenMode(notPatBic);
        PenPat(&qdGlobals().gray);
        PaintRect(&rtmp);
        PenPat(&qdGlobals().black);
        PenMode(patCopy);
    }
    cleanup_icon_info(&icon_info);

    /* resent the fg/bk colors */
    RGBForeColor(&ROMlib_black_rgb_color);
    RGBBackColor(&ROMlib_white_rgb_color);
}

static void
draw_menu(MenuHandle mh, Rect *rp, tablePtr tablep)
{
    struct table::tableentry *tp, *ep;
    int16_t topcutoff, bottomcutoff;

    if(LM(TopMenuItem) < rp->top)
        topcutoff = rp->top - LM(TopMenuItem) + lineheight;
    else
        topcutoff = 0;

    if(LM(AtMenuBottom) > rp->bottom)
        bottomcutoff = rp->bottom - LM(TopMenuItem) - lineheight;
    else
        bottomcutoff = 32767;

    for(tp = tablep->entry, ep = tp + tablep->count;
        tp[0].top < bottomcutoff && tp != ep;
        tp++)
        if(tp[1].top > topcutoff)
        {
            int32_t bit;
            int nitem;

            nitem = (tp - tablep->entry) + 1;
            bit = 1 << nitem;
            draw_item(rp, tp, bit, nitem, mh, false);
        }
    if(rp->top > LM(TopMenuItem))
        draw_arrow(rp, mh, uparrow);
    if(rp->bottom < LM(AtMenuBottom))
        draw_arrow(rp, mh, downarrow);
    (*MBSAVELOC)->mbUglyScroll = 0;
}

static void
fliprect(Rect *rp, int16_t i, tablePtr tablep, Rect *flipr)
{
    struct table::tableentry *tp;

    tp = &tablep->entry[i - 1];
    flipr->left = rp->left;
    flipr->right = rp->right;
    flipr->top = tp[0].top + LM(TopMenuItem);
    flipr->bottom = tp[1].top + LM(TopMenuItem);
}

static void
doupdown(MenuHandle mh, Rect *rp, tablePtr tablep, Boolean upordown,
         GUEST<int16_t> *itemp)
{
    INTEGER offset;
    Rect scrollr, updater, rtmp;
    struct table::tableentry *tp, *ep;
    RgnHandle updatergn;
    LONGINT bit;

    if(*itemp)
    {
        /* flip (rp, (*itemp), tablep); */
        draw_item(rp, &tablep->entry[*itemp - 1], 1 << *itemp,
                  *itemp, mh, false);
        *itemp = 0;
    }
    if((*MBSAVELOC)->mbUglyScroll)
    {
        /* don't sroll the scroll arrows */
        scrollr = *rp;
        if(TOP_ARROW_P())
            scrollr.top = scrollr.top + lineheight;
        if(BOTTOM_ARROW_P())
            scrollr.bottom = scrollr.bottom - lineheight;

        updater = *rp;

        if(upordown == UP)
        {
            offset = std::min<INTEGER>(lineheight, rp->top - LM(TopMenuItem));
            LM(TopMenuItem) = LM(TopMenuItem) + offset;
            LM(AtMenuBottom) = LM(AtMenuBottom) + offset;
            if(TOP_ARROW_P())
            {
                updater.top = scrollr.top;
                updater.bottom = updater.top + lineheight;
            }
            else
            {
                updater.top = rp->top;
                updater.bottom = updater.top + 2 * lineheight;
                erasearrow(rp, tablep, UP);
            }
        }
        else
        {
            offset = std::max<INTEGER>(-lineheight, rp->bottom - LM(AtMenuBottom));
            LM(TopMenuItem) = LM(TopMenuItem) + offset;
            LM(AtMenuBottom) = LM(AtMenuBottom) + offset;
            if(BOTTOM_ARROW_P())
            {
                updater.bottom = scrollr.bottom;
                updater.top = updater.bottom - lineheight;
            }
            else
            {
                updater.bottom = rp->bottom;
                updater.top = updater.bottom - 2 * lineheight;
                erasearrow(rp, tablep, DOWN);
            }
        }
        updatergn = NewRgn();
        ScrollRect(&scrollr, 0, offset, updatergn);
        DisposeRgn(updatergn);
        ClipRect(&updater);
        for(tp = tablep->entry, ep = tp + tablep->count, bit = 1 << 1;
            tp[0].top < updater.bottom - LM(TopMenuItem) && tp != ep;
            tp++, bit <<= 1)
            if(tp[1].top > updater.top - LM(TopMenuItem))
                draw_item(rp, tp, tp - tablep->entry + 1, bit, mh, false);
        rtmp.top = rtmp.left = -32767;
        rtmp.bottom = rtmp.right = 32767;
        ClipRect(&rtmp);
    }
    else
        (*MBSAVELOC)->mbUglyScroll = 1;
    if(upordown == DOWN)
    {
        if(LM(AtMenuBottom) >= rp->bottom)
            draw_arrow(rp, mh, downarrow);
    }
    else
    {
        if(LM(TopMenuItem) <= rp->top)
            draw_arrow(rp, mh, uparrow);
    }
}

void choose_menu(MenuHandle mh, Rect *rp, Point p, GUEST<int16_t> *itemp, tablePtr tablep)
{
    int32_t nitem;
    struct table::tableentry *tp, *ep;
    Rect valid_rect, clip_rect;
    INTEGER menu_id;

    menu_id = MI_ID(mh);

    valid_rect.left = rp->left;
    valid_rect.right = rp->right;
    valid_rect.top = std::max(rp->top, LM(TopMenuItem));
    valid_rect.bottom = std::min(rp->bottom, LM(AtMenuBottom));

    clip_rect.left = rp->left;
    clip_rect.right = rp->right;
    clip_rect.top = TOP_ARROW_P() ? rp->top + lineheight
                                     : toHost(rp->top);
    clip_rect.bottom = BOTTOM_ARROW_P() ? rp->bottom - lineheight
                                           : toHost(rp->bottom);
    ClipRect(&clip_rect);

    if(*itemp < 0)
        *itemp = 0;
    if(PtInRect(p, &valid_rect))
    {
        if(BOTTOM_ARROW_P()
           && p.v >= rp->bottom - lineheight)
            doupdown(mh, rp, tablep, DOWN, itemp);
        else if(TOP_ARROW_P()
                && p.v < rp->top + lineheight)
            doupdown(mh, rp, tablep, UP, itemp);
        else
        {
            int32_t bit;

            for(tp = tablep->entry, ep = tp + tablep->count;
                tp != ep && p.v >= tp->top + LM(TopMenuItem);
                tp++)
                ;
            nitem = tp - tablep->entry;
            LM(MenuDisable) = (menu_id << 16) | (uint16_t)nitem;

            bit = (1 << nitem) | 1;
            if((MI_ENABLE_FLAGS(mh) & bit) != bit
               || (tp[-1].name[0] && tp[-1].name[1] == '-'))
                nitem = 0;
            if(*itemp != nitem)
            {
                if(*itemp)
                    /* redraw this guy normally */
                    draw_item(rp, &tablep->entry[*itemp - 1], 1 << *itemp,
                              *itemp, mh, false);
                if(nitem)
                    draw_item(rp, &tablep->entry[nitem - 1], 1 << nitem, nitem, mh, true);
                *itemp = nitem;
            }
            if(nitem)
                fliprect(rp, nitem, tablep, &(*MBSAVELOC)->mbItemRect);
        }
    }
    else if(*itemp)
    {
        nitem = *itemp;
        draw_item(rp, &tablep->entry[nitem - 1], 1 << nitem, nitem, mh, false);
        *itemp = 0;
    }
    clip_rect.top = clip_rect.left = -32767;
    clip_rect.bottom = clip_rect.right = 32767;
    ClipRect(&clip_rect);
}

static void popuprect(MenuHandle mh, Rect *rp, Point p, GUEST<INTEGER> *itemp,
                      tablePtr tablep)
{
    struct table::tableentry *tp;
    INTEGER vmax;

    if((*mh)->menuWidth == -1 || (*mh)->menuHeight == -1)
        CalcMenuSize(mh);
    rp->top = p.v - tablep->entry[*itemp - 1].top;
    rp->left = p.h;
    rp->right = rp->left + (*mh)->menuWidth;
    *itemp = rp->top;

    for(tp = tablep->entry; rp->top < LM(MBarHeight); tp++)
        rp->top = rp->top + (tp[1].top - tp[0].top);

    rp->bottom = rp->top + (*mh)->menuHeight;

    vmax = qdGlobals().screenBits.bounds.bottom - 2; /* subtract 2 for frame */
    for(tp = tablep->entry + tablep->count - 1; rp->bottom > vmax; --tp)
        rp->bottom = rp->bottom - (tp[1].top - tp[0].top);
    rp->top = rp->bottom - (*mh)->menuHeight;
    for(tp = tablep->entry; rp->top < LM(MBarHeight); tp++)
        rp->top = rp->top + (tp[1].top - tp[0].top);
}

void Executor::C_mdef0(INTEGER mess, MenuHandle mh, Rect *rp, Point p,
                       GUEST<INTEGER> *item)
{
    FontInfo fi;
    char *sp;
    INTEGER count, v;
    tableHandle th;
    tablePtr tp;
    struct table::tableentry *tabp;

    current_menu_rect = rp;

#define MSWTEST
#if defined(MSWTEST)
    PORT_TX_FONT(qdGlobals().thePort) = LM(SysFontFam);
    PORT_TX_FACE(qdGlobals().thePort) = 0;
    PORT_TX_MODE(qdGlobals().thePort) = srcOr;
#endif /* MSWTEST */

    GetFontInfo(&fi);
    checksize = CharWidth(checkMark) + 1; /* used to use widMax - 1 here */
    lineheight = fi.ascent + fi.descent + fi.leading;
    ascent = fi.ascent;
    cloversize = CharWidth(commandMark);

    for(sp = (char *)*mh + SIZEOFMINFO + (*mh)->menuData[0], count = 0;
        *sp;
        sp += (unsigned char)*sp + SIZEOFMEXT, count++)
        ;
    th = (tableHandle)NewHandle((Size)sizeof(table) + count * sizeof(struct table::tableentry));
    HLock((Handle)th);
    tp = *th;
    tp->lasttick = TickCount();
    tp->count = count;
    v = 0;
    for(sp = (char *)*mh + SIZEOFMINFO + (*mh)->menuData[0], tabp = tp->entry;
        *sp;
        sp += (unsigned char)*sp + SIZEOFMEXT, tabp++)
    {
        icon_info_t icon_info;

        tabp->name = (StringPtr)sp;
        tabp->options = (mextp)(sp + (unsigned char)*sp + 1);
        tabp->top = v;
        get_icon_info(tabp->options, &icon_info, false);
        v += icon_info.height ? std::max<INTEGER>(icon_info.height, lineheight) : lineheight;
    }
    tabp->top = v;
    LM(AtMenuBottom) = LM(TopMenuItem) + v;

    switch(mess)
    {
        case mDrawMsg:
            draw_menu(mh, rp, tp);
            break;
        case mChooseMsg:
            choose_menu(mh, rp, p, item, tp);
            break;
        case mSizeMsg:
            size_menu(mh, tp);
            break;
        case mPopUpRect:
        {
            /*
	     * MacWriteII seems to like the 'h' and 'v' reversed.
	     */
            INTEGER temp;

            temp = p.h;
            p.h = p.v;
            p.v = temp;
        }
            popuprect(mh, rp, p, item, tp);
            break;
    }
    HUnlock((Handle)th);
    DisposeHandle((Handle)th);
}
