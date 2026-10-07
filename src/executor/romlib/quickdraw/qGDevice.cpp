/* Copyright 1994, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <MemoryMgr.h>
#include <WindowMgr.h>
#include <MenuMgr.h>
#include <SysErr.h>

#include <quickdraw/cquick.h>
#include <wind/wind.h>
#include <vdriver/vdriver.h>
#include <commandline/flags.h>
#include <vdriver/autorefresh.h>
#include <rsys/redrawscreen.h>
#include <rsys/executor.h>
#include <base/functions.impl.h>
#include <util/handle_vector.h>
#include <quickdraw/region.h>

#include <iostream>

using namespace Executor;

static int initialBpp;

/*
 * determined experimentally -- this fix is needed for Energy Scheming because
 * they put a bogus test for mode >= 8 in their code to detect color.  NOTE:
 * the value that will get stored is most definitely not 8 or anything near
 * 8, since 8bpp will be translated to 0x83.
 */

static LONGINT
mode_from_bpp(int bpp)
{
    LONGINT retval;
    int log2;

    if(bpp >= 1 && bpp <= 32)
        log2 = ROMlib_log2[bpp];
    else
        log2 = -1;

    if(log2 != -1)
        retval = 0x80 | ROMlib_log2[bpp];
    else
    {
        warning_unexpected("bpp = %d", bpp);
        retval = 0x86; /* ??? */
    }
    return retval;
}

static void gd_setup_main_device()
{
    GDHandle gd = LM(MainDevice);
    bool color_p = !vdriver->isGrayscale();
    int bpp = vdriver->bpp();

    PixMapHandle gd_pixmap = GD_PMAP(gd);
    
    /* set the color bit, all other flag bits should be the same */
    if(color_p)
        GD_FLAGS(gd) |= 1 << gdDevType;
    else
        GD_FLAGS(gd) &= ~(1 << gdDevType);

    GD_TYPE(gd) = (bpp > 8 ? directType : clutType);

    pixmap_set_pixel_fields(*gd_pixmap, bpp);

    SetupVideoMemoryMapping(vdriver->framebuffer(), vdriver->rowBytes() * vdriver->height());
    PIXMAP_BASEADDR(gd_pixmap) = (Ptr)vdriver->framebuffer();
    PIXMAP_SET_ROWBYTES(gd_pixmap, vdriver->rowBytes());

    Rect *gd_rect = &GD_RECT(gd);
    gd_rect->top = gd_rect->left = 0;
    gd_rect->bottom = vdriver->height();
    gd_rect->right = vdriver->width();
    PIXMAP_BOUNDS(gd_pixmap) = *gd_rect;

    note_executor_changed_screen();

    if(bpp <= 8)
    {
        CTabHandle gd_color_table;

        gd_color_table = PIXMAP_TABLE(gd_pixmap);

        CTabHandle temp_color_table;

        temp_color_table = GetCTable(color_p
                                            ? bpp
                                            : (bpp + 32));
        if(temp_color_table == nullptr)
            gui_fatal("unable to get color table `%d'",
                        color_p ? bpp : (bpp + 32));
        ROMlib_copy_ctab(temp_color_table, gd_color_table);
        DisposeCTable(temp_color_table);

        CTAB_FLAGS(gd_color_table) = CTAB_GDEVICE_BIT;
        MakeITable(gd_color_table, GD_ITABLE(gd), GD_RES_PREF(gd));

        gd_update_colors();
    }
}

static void gd_allocate_main_device(void)
{
    if(vdriver->framebuffer() == nullptr)
        gui_fatal("vdriver not initialized, unable to allocate `LM(MainDevice)'");

    TheZoneGuard guard(LM(SysZone));

    SET_HILITE_BIT();
    LM(TheGDevice) = LM(MainDevice) = LM(DeviceList) = nullptr;

    GDHandle graphics_device = NewGDevice(/* no driver */ 0,
                                 mode_from_bpp(vdriver->bpp()));

    /* we are the main device, since there are currently no others */
    LM(TheGDevice) = LM(MainDevice) = graphics_device;

    /* set gd flags reflective of the main device */
    GD_FLAGS(graphics_device) |= (1 << mainScreen) | (1 << screenDevice) | (1 << screenActive)
                                           /* PacMan Deluxe avoids
						 GDevices with noDriver
						 set.  Looking around
						 Apple's site shows that
						 people set this bit when
						 they're creating offscreen
						 gDevices.  It's not clear
						 whether or not we should
						 be setting this bit.
					    | (1 << noDriver) */;

    gd_setup_main_device();


    /* add ourselves to the device list */
    GD_NEXT_GD(graphics_device) = LM(DeviceList);
    LM(DeviceList) = graphics_device;
}

GDHandle Executor::C_NewGDevice(INTEGER ref_num, LONGINT mode)
{
    GDHandle this2;
    GUEST<Handle> h;
    GUEST<PixMapHandle> pmh;

    TheZoneGuard guard(LM(SysZone));

    this2 = (GDHandle)NewHandle((Size)sizeof(GDevice));

    if(this2 == nullptr)
        return this2;

    /* initialize fields; for some of these, i'm not sure what
	  the value should be */
    GD_ID(this2) = 0; /* ??? */

    /* CLUT graphics device by default */
    GD_TYPE(this2) = clutType;

    /* how do i allocate a new inverse color table? */
    h = NewHandle(0);
    GD_ITABLE(this2) = guest_cast<ITabHandle>(h);
    GD_RES_PREF(this2) = DEFAULT_ITABLE_RESOLUTION;

    GD_SEARCH_PROC(this2) = nullptr;
    GD_COMP_PROC(this2) = nullptr;

    GD_FLAGS(this2) = 0;
    /* mode_from_bpp (1)  indicates b/w hardware */
    if(mode != mode_from_bpp(1))
        GD_FLAGS(this2) |= 1 << gdDevType;

    pmh = NewPixMap();
    GD_PMAP(this2) = pmh;
    CTAB_FLAGS(PIXMAP_TABLE(GD_PMAP(this2))) |= CTAB_GDEVICE_BIT;

    GD_REF_CON(this2) = 0; /* ??? */
    GD_REF_NUM(this2) = ref_num; /* ??? */
    GD_MODE(this2) = mode; /* ??? */

    GD_NEXT_GD(this2) = nullptr;

    GD_RECT(this2).top = 0;
    GD_RECT(this2).left = 0;
    GD_RECT(this2).bottom = 0;
    GD_RECT(this2).right = 0;

    /* handle to cursor's expanded data/mask? */
    GD_CCBYTES(this2) = 0;
    GD_CCDEPTH(this2) = 0;
    GD_CCXDATA(this2) = nullptr;
    GD_CCXMASK(this2) = nullptr;

    GD_RESERVED(this2) = 0;

    /* if mode is -1, this2 is a user created gdevice,
	  and InitGDevice () should not be called (IMV-122) */
    if(mode != -1)
        InitGDevice(ref_num, mode, this2);

    return this2;
}

void Executor::gd_update_colors()
{
    GDHandle gd = LM(TheGDevice);

    if(GD_TYPE(gd) != clutType)
        /* no updates to do here */
        return;

    auto gd_ctab = PIXMAP_TABLE(GD_PMAP(gd));

    vdriver_color_t colors[256];
    int n = CTAB_SIZE(gd_ctab) + 1;
    assert(n <= 256);

    auto ctab = CTAB_TABLE(gd_ctab);

    for(int i = 0; i < n; i++)
    {
        colors[i] = { ctab[i].rgb.red, ctab[i].rgb.green, ctab[i].rgb.blue };
    }

    vdriver->setColors(n, colors);
}
/* it seems that `gd_ref_num' describes which device to initialize,
   and `mode' tells it what mode to start it in */
void Executor::C_InitGDevice(INTEGER gd_ref_num, LONGINT mode, GDHandle gdh)
{
}

void Executor::C_SetDeviceAttribute(GDHandle gdh, INTEGER attribute,
                                    Boolean value)
{
    if(value)
        GD_FLAGS(gdh) |= 1 << attribute;
    else
        GD_FLAGS(gdh) &= ~(1 << attribute);
}

void Executor::C_SetGDevice(GDHandle gdh)
{
    if(LM(TheGDevice) != gdh)
    {
        LM(TheGDevice) = gdh;
        ROMlib_invalidate_conversion_tables();
    }
}

void Executor::C_DisposeGDevice(GDHandle gdh)
{
    DisposeHandle((Handle)GD_ITABLE(gdh));
    DisposePixMap(GD_PMAP(gdh));

    /* FIXME: do other cleanup */
    DisposeHandle((Handle)gdh);
}

GDHandle Executor::C_GetGDevice()
{
    return LM(TheGDevice);
}

GDHandle Executor::C_GetDeviceList()
{
    return LM(DeviceList);
}

GDHandle Executor::C_GetMainDevice()
{
    GDHandle retval;

    retval = LM(MainDevice);

    /* Unfortunately, Realmz winds up dereferencing non-existent
     memory unless noDriver is set, but PacMan Deluxe will have
     trouble if that bit is set. */

    if(ROMlib_creator == "RLMZ"_4)
        GD_FLAGS(retval) |= 1 << noDriver;
    else
        GD_FLAGS(retval) &= ~(1 << noDriver);

    return retval;
}

GDHandle Executor::C_GetMaxDevice(const Rect *globalRect)
{
    /* FIXME:
     currently we have only a single device, so that has
     the max pixel depth for any given region (tho we would
     probably see if it intersects the main screen and return
     nullptr otherwise */
    return LM(MainDevice);
}

GDHandle Executor::C_GetNextDevice(GDHandle cur_device)
{
    return GD_NEXT_GD(cur_device);
}

void Executor::C_DeviceLoop(RgnHandle rgn,
                            DeviceLoopDrawingUPP drawing_proc,
                            LONGINT user_data, DeviceLoopFlags flags)
{
    GDHandle gd;
    GUEST<RgnHandle> save_vis_rgn_x;
    RgnHandle sect_rgn, gd_rect_rgn;

    save_vis_rgn_x = PORT_VIS_REGION(qdGlobals().thePort);

    sect_rgn = NewRgn();
    gd_rect_rgn = NewRgn();

    /* Loop over all GDevices, looking for active screens. */
    for(gd = LM(DeviceList); gd; gd = GD_NEXT_GD(gd))
        if((GD_FLAGS(gd) & ((1 << screenDevice)
                                 | (1 << screenActive)))
           == ((1 << screenDevice) | (1 << screenActive)))
        {
            /* NOTE: I'm blowing off some flags that come into play when
	 * you have multiple screens.  I don't think anything terribly
	 * bad would happen even if we had multiple screens, but we
	 * don't.  We can worry about it later.
	 */

            if(!(flags & allDevices))
            {
                Rect gd_rect, *port_bounds;

                /* Map the GDevice rect into qdGlobals().thePort local coordinates. */
                gd_rect = GD_RECT(gd);
                port_bounds = &(PORT_BOUNDS(qdGlobals().thePort));
                OffsetRect(&gd_rect,
                           port_bounds->left,
                           port_bounds->top);

                /* Intersect GDevice rect with the specified region. */
                RectRgn(gd_rect_rgn, &gd_rect);
                SectRgn(gd_rect_rgn, rgn, sect_rgn);

                SectRgn(sect_rgn, PORT_VIS_REGION(qdGlobals().thePort), sect_rgn);

                /* Save it away in qdGlobals().thePort. */
                PORT_VIS_REGION(qdGlobals().thePort) = sect_rgn;
            }

            if((flags & allDevices) || !EmptyRgn(sect_rgn))
            {
                drawing_proc(PIXMAP_PIXEL_SIZE(GD_PMAP(gd)),
                             GD_FLAGS(gd), gd, user_data);
            }
        }

    PORT_VIS_REGION(qdGlobals().thePort) = save_vis_rgn_x;

    DisposeRgn(gd_rect_rgn);
    DisposeRgn(sect_rgn);
}

Boolean Executor::C_TestDeviceAttribute(GDHandle gdh, INTEGER attribute)
{
    Boolean retval;

    retval = (GD_FLAGS(gdh) & (1 << attribute)) != 0;
    return retval;
}

// FIXME: #warning ScreenRes is duplicate with toolutil.cpp
void Executor::C_ScreenRes(GUEST<INTEGER> *h_res, GUEST<INTEGER> *v_res)
{
    *h_res = PIXMAP_HRES(GD_PMAP(LM(MainDevice))) >> 16;
    *v_res = PIXMAP_VRES(GD_PMAP(LM(MainDevice))) >> 16;
}

INTEGER Executor::C_HasDepth(GDHandle gdh, INTEGER bpp, INTEGER which_flags,
                             INTEGER flags)
{
    flags &= ~1;
    which_flags &= ~1;

    if(gdh != LM(MainDevice)
       || bpp == 0)
        return false;

    return (vdriver->isAcceptableMode(0, 0, bpp, ((which_flags & (1 << gdDevType))
                                                      ? (flags & (1 << gdDevType)) == (1 << gdDevType)
                                                      : vdriver->isGrayscale())));
}

static void gd_update_all_ports(GDHandle gdh, GUEST<Ptr> oldBaseAddr, Rect oldGDRect)
{
    PixMapHandle gd_pixmap = GD_PMAP(gdh);

    /* FIXME: assuming (1) all windows are on the current
     graphics device, and (2) the rowbytes and baseaddr
     of the gdevice cannot change */
    /* set the pixel size, rowbytes, etc
     of windows and the window manager color graphics port */

    if(LM(QDExist) == EXIST_YES)
    {
        qdGlobals().screenBits.baseAddr = PIXMAP_BASEADDR(gd_pixmap);
        qdGlobals().screenBits.rowBytes = PIXMAP_ROWBYTES(gd_pixmap) / PIXMAP_PIXEL_SIZE(gd_pixmap);
        qdGlobals().screenBits.bounds = PIXMAP_BOUNDS(gd_pixmap);

        // FIXME: this is not what the Mac does.
        // on MacOS, a lowmem global at 0xD66 contains a Handle to a system heap block
        // that contains a list of all GrafPorts in the system.
        // It starts with a two-byte count and then contains the specified number of pointers.
        // It is updated by Open[C]Port and Close[C]Port.
        // When reconfiguring displays, the DisplayManager updates bitmaps/pixmaps/colors for
        // all ports, and portRects/regions for all screen-sized ports.

        handle_vector<GrafPtr, Handle, 2, true> portList(LM(PortList));

        assert(std::find(portList.begin(), portList.end(), LM(WMgrPort)) != portList.end());
        assert(std::find(portList.begin(), portList.end(), GrafPtr(LM(WMgrCPort))) != portList.end());


        Rect screen = PIXMAP_BOUNDS(gd_pixmap);

        for(GrafPtr gp : portList)
        {
            Rect newBounds = PORT_BOUNDS(gp);
            newBounds.right = newBounds.left - screen.left + screen.right;
            newBounds.bottom = newBounds.top - screen.top + screen.bottom;
            PORT_BOUNDS(gp) = newBounds;

            if(CGrafPort_p(gp))
            {
                PixMapHandle port_pixmap = CPORT_PIXMAP(gp);
                if(PIXMAP_BASEADDR(port_pixmap) != oldBaseAddr)
                    continue;

                pixmap_set_pixel_fields(*port_pixmap, PIXMAP_PIXEL_SIZE(gd_pixmap));
                PIXMAP_SET_ROWBYTES(port_pixmap, PIXMAP_ROWBYTES(gd_pixmap));
                

                ROMlib_copy_ctab(PIXMAP_TABLE(gd_pixmap),
                                 PIXMAP_TABLE(port_pixmap));

                ThePortGuard guard(gp);
                RGBForeColor(&CPORT_RGB_FG_COLOR(gp));
                RGBBackColor(&CPORT_RGB_BK_COLOR(gp));
            }
            else
            {
                if(PORT_BITS(gp).baseAddr != oldBaseAddr)
                    continue;
                BITMAP_SET_ROWBYTES(&PORT_BITS(gp),
                                      PIXMAP_ROWBYTES(gd_pixmap));
            }

            if(EqualRect(&PORT_RECT(gp), &oldGDRect))
            {
                PORT_RECT(gp) = PIXMAP_BOUNDS(gd_pixmap);
                
                RgnHandle rgn = PORT_VIS_REGION(gp);
                if(RGN_SMALL_P(rgn) && EqualRect(&RGN_BBOX(rgn), &oldGDRect))
                    RectRgn(rgn, &PIXMAP_BOUNDS(gd_pixmap));
                
                rgn = PORT_CLIP_REGION(gp);
                if(RGN_SMALL_P(rgn) && EqualRect(&RGN_BBOX(rgn), &oldGDRect))
                    RectRgn(rgn, &PIXMAP_BOUNDS(gd_pixmap));
            }
        }
    }
}

void Executor::gd_vdriver_mode_changed()
{
    PixMapHandle gd_pixmap = GD_PMAP(LM(MainDevice));

    GUEST<Ptr> oldBase = PIXMAP_BASEADDR(gd_pixmap);    // store this pointer as GUEST<Ptr> because we might be changing memory mappings
    Rect oldRect = PIXMAP_BOUNDS(gd_pixmap);
    gd_setup_main_device();
    gd_update_all_ports(LM(MainDevice), oldBase, oldRect);

    if(LM(WWExist) == EXIST_YES)
    {
        ROMLib_InitGrayRgn();
        redraw_screen();
        ROMlib_rootless_update();
    }
}

OSErr Executor::C_SetDepth(GDHandle gdh, INTEGER bpp, INTEGER which_flags,
                           INTEGER flags)
{
    if(gdh != LM(MainDevice))
    {
        warning_unexpected("Setting the depth of a device not the screen; "
                           "this violates bogus assumptions in SetDepth.");
    }

    PixMapHandle gd_pixmap = GD_PMAP(gdh);

    if(bpp == PIXMAP_PIXEL_SIZE(gd_pixmap))
        return noErr;
    GUEST<Ptr> oldBase = PIXMAP_BASEADDR(gd_pixmap);    // store this pointer as GUEST<Ptr> because we might be changing memory mappings
    Rect oldRect = PIXMAP_BOUNDS(gd_pixmap);

    HideCursor();

    if(bpp == 0 || !vdriver->setMode(0, 0, bpp, ((which_flags & (1 << gdDevType)) ? !(flags & (1 << gdDevType)) : vdriver->isGrayscale())))
    {
        /* IMV says this returns non-zero in error case; not positive
	 cDepthErr is correct; need to verify */
        ShowCursor();
        return cDepthErr;
    }
    if(vdriver->framebuffer() == nullptr)
        gui_fatal("vdriver not initialized, unable to change bpp");


    gd_setup_main_device();

    cursor_reset_current_cursor();

    ShowCursor();

    gd_update_all_ports(gdh, oldBase, oldRect);

    /* Redraw the screen if that's what changed. */
    if(gdh == LM(MainDevice))
        redraw_screen();

    return noErr;
}

void Executor::ROMlib_InitGDevices(int width, int height, int bpp, bool grayscale)
{
    /* Set up the current graphics mode appropriately. */
    if(!vdriver->setMode(width, height, bpp, grayscale))
    {
        fprintf(stderr, "Could not set graphics mode.\n");
        exit(-12);
    }

    if(vdriver->framebuffer() == 0)
        abort();

    if(vdriver->isGrayscale())
    {
        /* Choose a nice light gray hilite color. */
        LM(HiliteRGB).red = (unsigned short)0xAAAA;
        LM(HiliteRGB).green = (unsigned short)0xAAAA;
        LM(HiliteRGB).blue = (unsigned short)0xAAAA;
    }
    else
    {
        /* how about a nice yellow hilite color? no, it's ugly. */
        LM(HiliteRGB).red = (unsigned short)0x9999;
        LM(HiliteRGB).green = (unsigned short)0xCCCC;
        LM(HiliteRGB).blue = (unsigned short)0xCCCC;
    }



    /* initialize the mac rgb_spec's */
    make_rgb_spec(&mac_16bpp_rgb_spec,
                    16, true, 0,
                    5, 10, 5, 5, 5, 0,
                    CL_RAW(GetCTSeed()));

    make_rgb_spec(&mac_32bpp_rgb_spec,
                    32, true, 0,
                    8, 16, 8, 8, 8, 0,
                    CL_RAW(GetCTSeed()));

    gd_allocate_main_device();
    initialBpp = vdriver->bpp();
}

void Executor::ResetToInitialDepth()
{
    SetDepth(LM(MainDevice), initialBpp, 0, 0);
}
