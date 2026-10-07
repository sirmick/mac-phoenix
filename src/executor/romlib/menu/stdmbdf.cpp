/* Copyright 1990, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/*
 * Checked for HLock and HUnlock by ctm on Fri May 10 18:31:31 MDT 1991
 */

#include <base/common.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <MenuMgr.h>
#include <WindowMgr.h>
#include <MemoryMgr.h>
#include <ToolboxUtil.h>
#include <FontMgr.h>

#include <quickdraw/cquick.h>
#include <menu/menu.h>
#include <wind/wind.h>
#include <quickdraw/image.h>
#include <rsys/executor.h>
#include <prefs/options.h>
#include <algorithm>

/* apple image */
namespace Executor
{
#include "apple.cmap"
}
using namespace Executor;

enum
{
    APPLE_CHAR = 0x14,
    INFINITY_CHAR = 0xB0
};

/*
 * NOTE:  if sixbyteoffset == 0 then realhilite does the entire menubar
 */

static void
draw_menu_title(muelem *elt,
                int last_menu_p,
                int hilite_p,
                int16_t muright)
{
    int gray_title_p, dither_title_p = false;
    RGBColor title_color, bar_color;
    MenuHandle muhandle;
    char *title;
    Rect dstr;

    muhandle = elt->muhandle;

    menu_bar_color(&bar_color);
    menu_title_color(MI_ID(muhandle), &title_color);

    dstr.top = hilite_p ? 1 : 0;
    dstr.bottom = LM(MBarHeight) - 1;
    dstr.left = elt->muleft;
    if(last_menu_p)
        dstr.right = muright;
    else
        dstr.right = elt[1].muleft;

    RGBForeColor(hilite_p ? &bar_color : &title_color);
    RGBBackColor(hilite_p ? &title_color : &bar_color);

    EraseRect(&dstr);

    title = (char *)MI_DATA(muhandle);

#if defined(COLOR_APPLE_MENU_ICON)
    gd = LM(MainDevice);
    if(*title == 1
       && title[1] == '\024'
       && PIXMAP_PIXEL_SIZE(GD_PMAP(gd)) > 2)
    {
        /* draw the color apple */
        dstr = apple->bounds;
        OffsetRect(&dstr,
                   elt->muleft + MENULEFT - 6, 1);

        RGBForeColor(&ROMlib_black_rgb_color);
        RGBBackColor(&ROMlib_white_rgb_color);

        image_update_ctab(apple, hilite_p ? &title_color : &bar_color, 0);
        image_copy(apple, true,
                   &dstr, srcCopy);
    }
    else
#endif /* COLOR_APPLE_MENU_ICON */
    {
        gray_title_p = !(MI_ENABLE_FLAGS(muhandle) & 1);
        if(gray_title_p)
        {
            dither_title_p = !AVERAGE_COLOR(&bar_color, &title_color, 0x8000,
                                            &title_color);
            RGBForeColor(&title_color);
        }
        PORT_TX_MODE(qdGlobals().thePort) = srcCopy;
        MoveTo(elt->muleft + MENULEFT - 3, 14);
        if(ROMlib_AppleChar && title[0] == 1 && title[1] == APPLE_CHAR)
            DrawChar((char)ROMlib_AppleChar);
        else
            DrawString((StringPtr)title);
        if(dither_title_p)
        {
            PenPat(&qdGlobals().gray);
            PenMode(notPatBic);
            PaintRect(&dstr);
            PenMode(patCopy);
            PenPat(&qdGlobals().black);
        }
        /* resent the fg/bk colors */
        RGBForeColor(&ROMlib_black_rgb_color);
        RGBBackColor(&ROMlib_white_rgb_color);
    }
}

static void
realhilite(int16_t offset, highstate h)
{
    muelem *mp;

    /* NOTE:  right now we aren't paying attention to h, and we are blindly
	 inverting whatever we're supposed to.  When we do color this
	 will be unacceptable, and if we appear to have menu highliting
	 bugs then we should probably check this out. */

    if(offset >= 0)
    {
        if(offset > 0)
        {
            mp = (muelem *)((char *)*LM(MenuList) + offset);

            draw_menu_title(mp,
                            offset == (*MENULIST)->muoff,
                            h == HILITE,
                            (*MENULIST)->muright);
        }
        else
        {
            RGBColor bar_color;
            int mbar_height;
            menulist *menulistp;
            muelem *mp, *mpend;
            Rect r;

            /* toggle the entire menu bar */
            mbar_height = LM(MBarHeight);

            r = PORT_RECT(wmgr_port);
            r.bottom = mbar_height - 1;

            if(h == HILITE)
                menu_title_color(0, &bar_color);
            else
                menu_bar_color(&bar_color);
            RGBBackColor(&bar_color);

            EraseRect(&r);

            PenSize(1, 1);
            MoveTo(r.left, mbar_height - 1);
            LineTo(r.right - 1, mbar_height - 1);

            HLock(LM(MenuList));
            menulistp = *MENULIST;
            mpend = menulistp->mulist + menulistp->muoff / sizeof(muelem);
            for(mp = menulistp->mulist; mp != mpend; mp++)
                draw_menu_title(mp, mp == mpend - 1,
                                h == HILITE, menulistp->muright);
            HUnlock(LM(MenuList));
        }
    }
}

static void
mbdf_draw(int32_t draw_p)
{
    RGBColor bar_color;
    menulist *menulistp;
    muelem *mp, *mpend;
    Rect r;

    r = PORT_RECT(wmgr_port);
    r.bottom = LM(MBarHeight) - 1;
    ClipRect(&r);

    menu_bar_color(&bar_color);
    RGBBackColor(&bar_color);
    EraseRect(&r);

    PenSize(1, 1);
    MoveTo(r.left, LM(MBarHeight) - 1);
    LineTo(r.right - 1, LM(MBarHeight) - 1);
    if(draw_p == DRAWMENUBAR)
    {
        /* draw titles */
        r.bottom = r.bottom - 1;

        PORT_TX_FACE(wmgr_port) = (Style)0;
        PORT_TX_FONT(wmgr_port) = LM(SysFontFam);

        HLock(LM(MenuList));
        menulistp = *MENULIST;
        mpend = menulistp->mulist + menulistp->muoff / sizeof(muelem);
        for(mp = menulistp->mulist; mp != mpend; mp++)
            draw_menu_title(mp, mp == mpend - 1, false, menulistp->muright);
        HUnlock(LM(MenuList));

        /* highlite title if necessary */
        if(LM(TheMenu))
            realhilite(ROMlib_mentosix(LM(TheMenu)), HILITE);

        /* set ClipRgn to full open */
        ClipRect(&PORT_RECT(wmgr_port));
    }

    /* resent the fg/bk colors */
    RGBForeColor(&ROMlib_black_rgb_color);
    RGBBackColor(&ROMlib_white_rgb_color);
}

static LONGINT hit(LONGINT);
static void calc(LONGINT offset);
static void dispose();
static void height();
static Rect *getrect(LONGINT);
static mbdfentry *offtomep(LONGINT);
static void savealt(LONGINT);
static void resetalt(LONGINT);
static RgnHandle menurgn(RgnHandle);

static LONGINT hit(LONGINT mousept)
{
    Point p;
    muelem *mp, *mpend;
    mbdfentry *mbdfp, *mbdfep;

    p.h = LoWord(mousept);
    p.v = HiWord(mousept);

    if(p.v < LM(MBarHeight))
    {
        mpend = (*MENULIST)->mulist + (*MENULIST)->muoff / sizeof(muelem);
        for(mp = (*MENULIST)->mulist; mp != mpend && mp->muleft <= p.h; mp++)
            ;
        if(mp == (*MENULIST)->mulist || p.h > (*MENULIST)->muright)
            /*-->*/ return NOTHITINMBAR;
        else
            /*-->*/ return (char *)(mp - 1) - (char *)*LM(MenuList);
    }
    else
    {
        mbdfep = (mbdfentry *)*LM(MBSaveLoc);
        for(mbdfp = (mbdfentry *)((char *)mbdfep + ((mbdfheader *)mbdfep)->lastMBSave);
            mbdfp != mbdfep && !PtInRect(p, &mbdfp->mbRectSave); mbdfp--)
            ;
        if(mbdfp == mbdfep)
            /*-->*/ return NOTHIT;
        else
            /*-->*/ return mbdfp->mbMLOffset;
    }
}

static void calc(LONGINT offset)
{
    MenuHandle mh;
    INTEGER left, titsize;
    muelem *mp, *mep, *firstmp;
    menulist *menulistp;

    PORT_TX_FACE(wmgr_port) = (Style)0;
    PORT_TX_FONT(wmgr_port) = LM(SysFontFam);

    HLock(LM(MenuList));
    menulistp = *MENULIST;
    firstmp = menulistp->mulist;
    if(offset == 0)
        mp = firstmp;
    else
        mp = (muelem *)((char *)menulistp + offset);
    if(mp == firstmp)
        left = MENULEFT;
    else
    {
        mh = mp[-1].muhandle;
        HLock((Handle)mh);
        titsize = StringWidth((*mh)->menuData) + SLOP;
        HUnlock((Handle)mh);
        left = mp[-1].muleft + titsize;
    }
    for(mep = (muelem *)((char *)menulistp + menulistp->muoff) + 1;
        mp < mep; mp++)
    {
        mp->muleft = left;
        mh = mp->muhandle;
        HLock((Handle)mh);
        titsize = StringWidth((*mh)->menuData) + SLOP;
        HUnlock((Handle)mh);
        left += titsize;
    }
    menulistp->muright = left;
    HUnlock(LM(MenuList));
}

static void
init()
{
    if(!LM(MBSaveLoc))
    {
        LM(MBSaveLoc) = NewHandle((Size)MBDFSTRUCTBYTES);
        (*MBSAVELOC)->mbCustomStorage = nullptr;
    }
    (*MBSAVELOC)->lastMBSave = 0;
}

static void dispose()
{
}

static void
mbdfhilite(int32_t hilitestate)
{
    int16_t loword;

    loword = LoWord(hilitestate);
    switch(HiWord(hilitestate))
    {
        case 0:
            realhilite(loword, HILITE);
            break;
        case 1:
            realhilite(loword, RESTORE);
            break;
    }
}

static void height()
{
    FontInfo fi;

    GetFontInfo(&fi);
    LM(MBarHeight) = fi.ascent + fi.descent + fi.leading + 4;
}

static void
save(int16_t offset, Rect *rect)
{
    GDHandle gd;
    PixMapHandle gd_pixmap;
    muelem *mup;
    mbdfentry *mep;
    Rect save_rect;
    Ptr p;

    gd = LM(TheGDevice);
    gd_pixmap = GD_PMAP(gd);

    {
        HLockGuard guard(LM(MBSaveLoc));

        int current_mb_save;

        current_mb_save = (*MBSAVELOC)->lastMBSave + sizeof(mbdfentry);
        (*MBSAVELOC)->lastMBSave = current_mb_save;
        mep = (mbdfentry *)((char *)*MBSAVELOC + current_mb_save);

        mep->mbRectSave = *rect;

        PixMapHandle save_pmh;
        int gd_bpp;
        int row_bytes;

        Rect *bounds;
        int height;
        int width;

        save_pmh = NewPixMap();
        if(save_pmh == nullptr)
            goto failure;

        mep->mbMenuDir = MBRIGHTDIR;
        mep->mbMLOffset = offset;
        mup = (muelem *)((char *)*LM(MenuList) + offset);
        mep->mbMLHandle = mup->muhandle;
        mep->mbReserved = 0;
        save_rect.top = rect->top - 1;
        save_rect.left = rect->left - 1;
        save_rect.bottom = rect->bottom + 2;
        save_rect.right = rect->right + 2;

        bounds = &PIXMAP_BOUNDS(save_pmh);

        *bounds = save_rect;
        /* long align the left boundary */
        bounds->left = save_rect.left & ~31;
        bounds->right = std::min(bounds->right,
                               PORT_BOUNDS(qdGlobals().thePort).right);

        height = RECT_HEIGHT(bounds);
        width = RECT_WIDTH(bounds);

        gd_bpp = PIXMAP_PIXEL_SIZE(gd_pixmap);

        ROMlib_copy_ctab(PIXMAP_TABLE(gd_pixmap),
                         PIXMAP_TABLE(save_pmh));

        pixmap_set_pixel_fields(*save_pmh, gd_bpp);

        row_bytes = ((width * gd_bpp + 31) / 32) * 4;
        PIXMAP_SET_ROWBYTES(save_pmh, row_bytes);

        p = NewPtr(height * row_bytes);
        if(LM(MemErr) != noErr)
        {
            DisposePixMap(save_pmh);
            goto failure;
        }
        PIXMAP_BASEADDR(save_pmh) = p;

        {
            WRAPPER_PIXMAP_FOR_COPY(wrapper);

            WRAPPER_SET_PIXMAP(wrapper, save_pmh);

            CopyBits(PORT_BITS_FOR_COPY(qdGlobals().thePort), wrapper,
                     &save_rect, &save_rect, srcCopy, nullptr);
        }

        mep->mbBitsSave = (Handle)save_pmh;

        goto done;
    failure:
        mep->mbBitsSave = nullptr;
    done:;
    }

    /* Erase to white -- colored backgrounds can be drawn by individual
	 mdefs.  If we erase this to the ostensible background color, MacClade
	 will get a black background on its custom Tool menu. */

    RGBForeColor(&ROMlib_white_rgb_color);
    PaintRect(rect);

    PenNormal();
    RGBForeColor(&ROMlib_black_rgb_color);

    save_rect.right = save_rect.right - 1;
    save_rect.bottom = save_rect.bottom - 1;
    FrameRect(&save_rect);

    MoveTo(save_rect.right, save_rect.top + 3);
    LineTo(save_rect.right, save_rect.bottom);
    MoveTo(save_rect.left + 3, save_rect.bottom);
    LineTo(save_rect.right, save_rect.bottom);

    ClipRect(rect);
}

static void
restore(void)
{
    HLockGuard guard(LM(MBSaveLoc));
    mbdfentry *mep;
    PixMapHandle save_pmh;
    Rect save_rect;

    mep = (mbdfentry *)((char *)*MBSAVELOC
                        + (*MBSAVELOC)->lastMBSave);
    save_rect = mep->mbRectSave;
    save_rect.top = save_rect.top - 1;
    save_rect.left = save_rect.left - 1;
    save_rect.bottom = save_rect.bottom + 2;
    save_rect.right = save_rect.right + 2;

    save_pmh = (PixMapHandle)mep->mbBitsSave;

    if(save_pmh == nullptr)
    {
        RgnHandle rh;

        rh = NewRgn();
        RectRgn(rh, &save_rect);
        PaintBehind((WindowPeek)FrontWindow(), rh);
        DisposeRgn(rh);
    }
    else
    {
        {
            WRAPPER_PIXMAP_FOR_COPY(wrapper);

            WRAPPER_SET_PIXMAP(wrapper, save_pmh);
            CopyBits(wrapper, PORT_BITS_FOR_COPY(qdGlobals().thePort),
                     &save_rect, &save_rect, srcCopy, nullptr);
        }

        DisposePtr(PIXMAP_BASEADDR(save_pmh));
        DisposePixMap(save_pmh);
    }

    mep->mbBitsSave = nullptr;
    (*MBSAVELOC)->lastMBSave
        = (*MBSAVELOC)->lastMBSave - sizeof(mbdfentry);
}

static Rect *getrect(LONGINT offset)
{
    INTEGER hiword;
    static Rect r;
    MenuHandle mh;
    muelem *mp;
    INTEGER dh, dv;

    hiword = HiWord(offset);
    mp = (muelem *)((char *)*LM(MenuList) + LoWord(offset));
    mh = mp->muhandle;
    if((*mh)->menuWidth == -1 || (*mh)->menuHeight == -1)
        CalcMenuSize(mh);
    if(hiword)
    { /* hierarchical */
        /* note 7 and 5 below are guesses */
        r.top = std::max<INTEGER>((*MBSAVELOC)->mbItemRect.top, LM(MBarHeight) + 7);
        r.left = (*MBSAVELOC)->mbItemRect.right - 5;
        r.bottom = r.top + (*mh)->menuHeight;
        r.right = r.left + (*mh)->menuWidth;
    }
    else
    { /* regular */
        r.top = LM(MBarHeight);
        r.left = mp->muleft;
        r.bottom = r.top + (*mh)->menuHeight;
        r.right = r.left + (*mh)->menuWidth;
    }
    dh = qdGlobals().screenBits.bounds.right - 10 - r.right;
    if(dh > 0)
        dh = 0;
    dv = qdGlobals().screenBits.bounds.bottom - 10 - r.bottom;
    if(dv > 0)
        dv = 0;
    OffsetRect(&r, dh, dv);
    return &r;
}

static mbdfentry *offtomep(LONGINT offset)
{
    mbdfentry *mbdfp, *mbdfep;

    mbdfep = (mbdfentry *)*LM(MBSaveLoc);
    for(mbdfp = (mbdfentry *)((char *)mbdfep + ((mbdfheader *)mbdfep)->lastMBSave);
        mbdfp != mbdfep && mbdfp->mbMLOffset != offset; mbdfp--)
        ;
    return mbdfp == mbdfep ? 0 : mbdfp;
}

static void savealt(LONGINT offset)
{
    mbdfentry *mep;

    mep = offtomep(offset);
    mep->mbTopScroll = LM(TopMenuItem);
    mep->mbBotScroll = LM(AtMenuBottom);
}

static void resetalt(LONGINT offset)
{
    mbdfentry *mep;

    mep = offtomep(offset);
    LM(TopMenuItem) = mep->mbTopScroll;
    LM(AtMenuBottom) = mep->mbBotScroll;
}

static RgnHandle menurgn(RgnHandle rgn)
{
    Rect r;

    if(LM(MBarHeight) <= 0)
        height();
    r = PORT_RECT(wmgr_port);
    r.bottom = LM(MBarHeight);
    RectRgn(rgn, &r);
    SectRgn(rgn, LM(GrayRgn), rgn);
    return rgn;
}

int32_t Executor::C_mbdf0(int16_t sel, int16_t mess, int16_t param1,
                          int32_t param2)
{
    int32_t retval;
    GUEST<GrafPtr> saveport;
    draw_state_t draw_state;
        
    retval = 0;
    if(mess != mbInit) /* fun w/ Word 6 */
    {
        GetPort(&saveport);
        SetPort(wmgr_port);
        draw_state_save(&draw_state);
    }

    switch(mess)
    {
        case mbDraw:
            mbdf_draw(param2);
            break;
        case mbHit:
            retval = hit(param2);
            break;
        case mbCalc:
            calc(param2);
            break;
        case mbInit:
            init();
            break;
        case mbDispose:
            dispose();
            break;
        case mbHilite:
            ClipRect(&PORT_RECT(wmgr_port)); /* full open */
            mbdfhilite(param2);
            break;
        case mbHeight:
            height();
            break;
        case mbSave:
            ClipRect(&PORT_RECT(wmgr_port)); /* full open */
            save(param1, (Rect *)SYN68K_TO_US(param2));
            break;
        case mbRestore:
            ClipRect(&PORT_RECT(wmgr_port)); /* full open */
            restore();
            break;
        case mbRect:
            retval = US_TO_SYN68K(getrect(param2));
            break;
        case mbSaveAlt:
            savealt(param2);
            break;
        case mbResetAlt:
            resetalt(param2);
            break;
        case mbMenuRgn:
            retval = (LONGINT)
                US_TO_SYN68K(menurgn((RgnHandle)SYN68K_TO_US(param2)));
            break;
    }
    if(mess != mbInit)
    {
        draw_state_restore(&draw_state);
        SetPort(saveport);
    }
    return retval;
}
