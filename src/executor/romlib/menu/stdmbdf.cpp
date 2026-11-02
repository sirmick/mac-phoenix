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
#include <mman/mman.h>
#include <wind/wind.h>
#include <quickdraw/image.h>
#include <rsys/executor.h>
#include <prefs/options.h>
#include <prefs/prefs.h>
#include <algorithm>
#include <Iconutil.h>
#include <TextEdit.h>
#include <ScriptMgr.h>
#include <Appearance.h>

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

static bool platinum_bar();

/* MacPhoenix: 8.x's Apple menu title is the System's colour apple (icon
   family -16386), 16 wide in a 28-wide title, as 8.6 lays it out on a G3
   (difftest menu bar.left.*) */
enum { kAppleTitleIcon = -16386, kAppleTitleWidth = 28 };

static bool apple_title_p(MenuHandle mh)
{
    return platinum_bar() && MI_DATA(mh)[0] == 1 && MI_DATA(mh)[1] == APPLE_CHAR;
}

/* MacPhoenix: where a title is drawn, hit and its menu hangs from: on 8.x
   a pixel left of its menuLeft (the G3: the Apple title's highlight and
   menu from 9, its menuLeft 10), the system menus at the right excepted */
static int title_left(const muelem *mp)
{
    return mp->muleft - (platinum_bar() && !ROMlib_system_menu_at_right(mp->muhandle) ? 1 : 0);
}

/* MacPhoenix: a system menu's title is an icon suite (menu/sysmenu.cpp). */
static int title_width(MenuHandle mh)
{
    if(ROMlib_icon_title_p(mh))
        return kSystemMenuTitleWidth;
    if(apple_title_p(mh))
        return kAppleTitleWidth;
    HLockGuard guard(mh);
    return StringWidth((*mh)->menuData) + SLOP;
}

/* MacPhoenix: 8.x's menu bar is platinum (AppearanceLib's menu calls,
   appearance_draw.cpp): titles over it in black, #777 when disabled, white
   when selected. */
static bool platinum_bar()
{
    return system_version >= 0x800;
}

static Rect bar_rect()
{
    Rect r = PORT_RECT(wmgr_port);
    r.top = 0;
    r.bottom = LM(MBarHeight);
    return r;
}

static void platinum_title(muelem *elt, bool hilite_p, bool bar_selected)
{
    MenuHandle mh = elt->muhandle;
    Rect bar = bar_rect(), tr = bar;
    tr.left = title_left(elt);
    tr.right = tr.left + title_width(mh);
    if(hilite_p)
        DrawThemeMenuTitle(&bar, &tr, kThemeMenuSelected, 0, nullptr, 0);
    else if(!bar_selected)
    {
        RgnHandle save = NewRgn();
        GetClip(save);
        ClipRect(&tr);
        DrawThemeMenuBarBackground(&bar, kThemeMenuBarNormal, 0);
        SetClip(save);
        DisposeRgn(save);
    }
    bool white = hilite_p || bar_selected;
    if(apple_title_p(mh))
    {
        Rect ir;
        ir.top = (LM(MBarHeight) - 16) / 2;
        ir.bottom = ir.top + 16;
        ir.left = title_left(elt) + (kAppleTitleWidth - 16) / 2;
        ir.right = ir.left + 16;
        PlotIconID(&ir, atNone, white ? ttSelected : !(MI_ENABLE_FLAGS(mh) & 1) ? ttDisabled : ttNone,
                   kAppleTitleIcon);
        return;
    }
    if(ROMlib_icon_title_p(mh))
    {
        Rect ir;
        ir.top = (LM(MBarHeight) - 16) / 2;
        ir.bottom = ir.top + 16;
        ir.left = elt->muleft + (kSystemMenuTitleWidth - 16) / 2;
        ir.right = ir.left + 16;
        PlotIconSuite(&ir, atNone, white ? ttSelected : ttNone, ROMlib_icon_title_suite(mh));
        return;
    }
    static RGBColor disabled = { 0x7777, 0x7777, 0x7777 };
    RGBForeColor(white ? &ROMlib_white_rgb_color
                 : !(MI_ENABLE_FLAGS(mh) & 1) ? &disabled
                                               : &ROMlib_black_rgb_color);
    PORT_TX_MODE(qdGlobals().thePort) = srcOr;
    MoveTo(title_left(elt) + MENULEFT - 4, 14);
    char *title = (char *)MI_DATA(mh);
    if(ROMlib_AppleChar && title[0] == 1 && title[1] == APPLE_CHAR)
        DrawChar((char)ROMlib_AppleChar);
    else
        DrawString((StringPtr)title);
    PORT_TX_MODE(qdGlobals().thePort) = srcCopy;
    RGBForeColor(&ROMlib_black_rgb_color);
}

/* The whole bar, selected or not, its titles over it */
static void platinum_bar_draw(bool selected, bool titles)
{
    Rect bar = bar_rect();
    DrawThemeMenuBarBackground(&bar, selected ? kThemeMenuBarSelected : kThemeMenuBarNormal, 0);
    if(!titles)
        return;
    PORT_TX_FACE(wmgr_port) = (Style)0;
    PORT_TX_FONT(wmgr_port) = LM(SysFontFam);
    HLockGuard guard(LM(MenuList));
    menulist *ml = *MENULIST;
    muelem *end = ml->mulist + ml->muoff / sizeof(muelem);
    for(muelem *mp = ml->mulist; mp != end; mp++)
        platinum_title(mp, false, selected);
}

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

    if(platinum_bar())
    {
        platinum_title(elt, hilite_p, false);
        return;
    }

    muhandle = elt->muhandle;

    menu_bar_color(&bar_color);
    menu_title_color(MI_ID(muhandle), &title_color);

    dstr.top = hilite_p ? 1 : 0;
    dstr.bottom = LM(MBarHeight) - 1;
    dstr.left = elt->muleft;
    dstr.right = elt->muleft + title_width(muhandle);

    RGBForeColor(hilite_p ? &bar_color : &title_color);
    RGBBackColor(hilite_p ? &title_color : &bar_color);

    EraseRect(&dstr);

    title = (char *)MI_DATA(muhandle);

    if(ROMlib_icon_title_p(muhandle))
    {
        Rect ir;
        ir.top = (LM(MBarHeight) - 16) / 2;
        ir.bottom = ir.top + 16;
        ir.left = elt->muleft + (kSystemMenuTitleWidth - 16) / 2;
        ir.right = ir.left + 16;
        PlotIconSuite(&ir, atNone, hilite_p ? ttSelected : ttNone,
                      ROMlib_icon_title_suite(muhandle));
        RGBForeColor(&ROMlib_black_rgb_color);
        RGBBackColor(&ROMlib_white_rgb_color);
        return;
    }

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
            if(platinum_bar())
            {
                platinum_bar_draw(h == HILITE, true);
                return;
            }
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

    if(platinum_bar())
    {
        r = bar_rect();
        ClipRect(&r);
        platinum_bar_draw(false, draw_p == DRAWMENUBAR);
        if(draw_p == DRAWMENUBAR && LM(TheMenu))
            realhilite(ROMlib_mentosix(LM(TheMenu)), HILITE);
        ClipRect(&PORT_RECT(wmgr_port));
        RGBForeColor(&ROMlib_black_rgb_color);
        RGBBackColor(&ROMlib_white_rgb_color);
        return;
    }

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

/* Message 15 of the 7.5.5 MBDF: hilite nothing, clear the bar without its
   titles and draw the Pascal string param2 across it in bold, inset (10,1),
   TETextBox-justified by param1's high byte.  If bit 7 of param1's low byte
   is set, the font and size are the system font of script (low byte & $7F). */
static void
drawmsg(int16_t param1, int32_t param2)
{
    StringPtr msg = (StringPtr)SYN68K_TO_US(param2);
    Rect r;

    HiliteMenu(0);
    mbdf_draw(-1);
    if(param1 & 0x80)
    {
        int32_t fs = GetScriptVariable(param1 & 0x7F, 84 /* smScriptSysFondSize */);
        TextFont(fs >> 16);
        TextSize(fs & 0xFFFF);
    }
    TextFace(bold);
    r = PORT_RECT(wmgr_port);
    r.bottom = LM(MBarHeight) - 1;
    InsetRect(&r, 10, 1);
    if(msg)
        TETextBox((Ptr)(msg + 1), msg[0], &r, param1 >> 8);
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
        /* titles need not be contiguous: the system menus are at the
           right */
        mpend = (*MENULIST)->mulist + (*MENULIST)->muoff / sizeof(muelem);
        for(mp = (*MENULIST)->mulist; mp != mpend; mp++)
            if(p.h >= title_left(mp) && p.h < title_left(mp) + title_width(mp->muhandle))
                /*-->*/ return (char *)mp - (char *)*LM(MenuList);
        /*-->*/ return NOTHITINMBAR;
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

    /* MacPhoenix: always lay out the whole bar: application menus from the
       left (muright ends them; 8.x's Help menu follows them), the other
       system menus right-aligned. */
    (void)offset;
    HLock(LM(MenuList));
    menulistp = *MENULIST;
    firstmp = menulistp->mulist;
    mep = (muelem *)((char *)menulistp + menulistp->muoff) + 1;
    left = MENULEFT;
    int system_width = 0;
    for(mp = firstmp; mp < mep; mp++)
    {
        mh = mp->muhandle;
        titsize = title_width(mh);
        if(ROMlib_system_menu_at_right(mh))
        {
            system_width += titsize;
            continue;
        }
        mp->muleft = left;
        left += titsize;
    }
    menulistp->muright = left;
    left = PORT_RECT(wmgr_port).right - kSystemMenuRightMargin - system_width;
    for(mp = firstmp; mp < mep; mp++)
    {
        mh = mp->muhandle;
        if(!ROMlib_system_menu_at_right(mh))
            continue;
        mp->muleft = left;
        left += title_width(mh);
    }
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

        /* MacPhoenix: in the System heap, as Apple's SaveRestoreBits takes
           temporary memory: an application's heap may not have room (the
           Finder's has ~27K free with Balloon Help on), and the menu
           would then be repainted over rather than restored. */
        TheZoneGuard zone_guard(LM(SysZone));
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

    /* MacPhoenix: 8.x's menu frame, platinum (the save area has room for
       its outline and shadow) */
    if(platinum_bar())
    {
        DrawThemeMenuBackground(rect, kThemeMenuTypePullDown);
        ClipRect(rect);
        return;
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
        /* 8.6: 4 in from the item's right */
        r.left = (*MBSAVELOC)->mbItemRect.right - (platinum_bar() ? 4 : 5);
        r.bottom = r.top + (*mh)->menuHeight;
        r.right = r.left + (*mh)->menuWidth;
    }
    else
    { /* regular */
        r.top = LM(MBarHeight);
        /* 8.6: the outline under the title's left */
        r.left = title_left(mp) + (platinum_bar() ? 1 : 0);
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

/* MacPhoenix: 7.5.5's MBDF message 14, as its code lays it out (System
   MBDF 0 +$109c).  The title of the menu at MenuList offset `offset`:
   one pixel inside the bar top and bottom, from a pixel left of the title
   to three pixels past the next title's left (lastRight for the last
   application menu, the bar's right margin for the last system menu).
   Offset 0 is the whole bar. */
static bool system_menu_at(INTEGER offset)
{
    muelem *mp = (muelem *)((char *)*MENULIST + offset);
    return ROMlib_system_menu_at_right(mp->muhandle);
}

static Rect title_rect(INTEGER offset)
{
    menulist *ml = *MENULIST;
    Rect r = PORT_RECT(wmgr_port);
    r.bottom = r.top + LM(MBarHeight);
    if(offset == 0)
        return r;

    INTEGER lastoff = ml->muoff;
    muelem *mp = (muelem *)((char *)ml + offset);
    INTEGER left = r.left;
    r.top += 1;
    r.bottom -= 1;
    r.left = left + mp->muleft - 1;
    INTEGER right = r.right - 10;
    if(offset == lastoff)
        ;
    else if(!system_menu_at(offset) && system_menu_at(offset + sizeof(muelem)))
        right = left + ml->muright;
    else if(left + (mp + 1)->muleft <= right)
        right = left + (mp + 1)->muleft;
    r.right = right + 3;
    return r;
}

/* Parameter -1: from the first title to lastRight.  -2: the system menus,
   which sit together at the end of the list.  Beyond the last menu:
   empty. */
static void titlerect(INTEGER param, Rect *out)
{
    menulist *ml = *MENULIST;
    INTEGER firstoff = (char *)ml->mulist - (char *)ml;
    INTEGER lastoff = ml->muoff;
    Rect r = { 0, 0, 0, 0 };
    if(param >= 0 && param <= lastoff)
        r = title_rect(param);
    else if(lastoff > 0 && param == -1)
    {
        r = title_rect(firstoff);
        r.right = PORT_RECT(wmgr_port).left + ml->muright;
    }
    else if(lastoff > 0 && param == -2)
    {
        INTEGER off = lastoff;
        while(off > firstoff && system_menu_at(off - sizeof(muelem)))
            off -= sizeof(muelem);
        if(system_menu_at(off))
        {
            r = title_rect(off);
            r.right = title_rect(lastoff).right;
        }
    }
    *out = r;
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
        case mbDrawMsg:
            drawmsg(param1, param2);
            break;
        case mbTitleRect:
            titlerect(param1, (Rect *)SYN68K_TO_US(param2));
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

/* MenusLib's StandardMBDF: 8.x's MBDF 0 is a PowerPC stub that passes its
   arguments on to it. */
LONGINT Executor::C_StandardMBDF(INTEGER selector, INTEGER message,
                                 INTEGER parameter1, LONGINT parameter2)
{
    return C_mbdf0(selector, message, parameter1, parameter2);
}
