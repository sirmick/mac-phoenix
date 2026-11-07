/* MacPhoenix: SaveRestoreBits ($A81E), 7.5.5's private save of the screen
 * under a menu or a balloon (CQuickDraw.yaml). Apple's record is a 12-byte
 * handle: the rectangle, then a handle to the pixels; here the pixels are
 * a PixMap of the main device's depth, copied the way the menu bar
 * definition keeps a menu's (menu/stdmbdf.cpp). */
#include <base/common.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <MemoryMgr.h>
#include <WindowMgr.h>
#include <quickdraw/cquick.h>
#include <wind/wind.h>
#include <mman/mman.h>

#include <algorithm>

using namespace Executor;

namespace
{
struct SavedBits
{
    GUEST<Rect> bounds;
    GUEST<PixMapHandle> pixmap;
};
static_assert(sizeof(SavedBits) == 12);
}

OSErr Executor::C_SaveBits(const Rect *bounds, INTEGER purgeable, GUEST<Handle> *bits)
{
    (void)purgeable;
    *bits = nullptr;
    GDHandle gd = LM(MainDevice);
    PixMapHandle gd_pixmap = GD_PMAP(gd);
    Rect r;
    if(!SectRect(bounds, &PIXMAP_BOUNDS(gd_pixmap), &r))
        return noErr; /* nothing on screen: an empty save */

    /* In the System heap: Apple's takes temporary memory, and an
       application's heap may not have room for a screenful. */
    TheZoneGuard guard(LM(SysZone));
    Handle h = NewHandle(sizeof(SavedBits));
    if(!h)
        return memFullErr;
    PixMapHandle pm = NewPixMap();
    if(!pm)
    {
        DisposeHandle(h);
        return memFullErr;
    }
    Rect *pb = &PIXMAP_BOUNDS(pm);
    *pb = r;
    pb->left = r.left & ~31; /* long-aligned rows, as the menus' */
    int height = RECT_HEIGHT(pb), width = RECT_WIDTH(pb);
    int bpp = PIXMAP_PIXEL_SIZE(gd_pixmap);
    ROMlib_copy_ctab(PIXMAP_TABLE(gd_pixmap), PIXMAP_TABLE(pm));
    pixmap_set_pixel_fields(*pm, bpp);
    int row_bytes = ((width * bpp + 31) / 32) * 4;
    PIXMAP_SET_ROWBYTES(pm, row_bytes);
    Ptr p = NewPtr(height * row_bytes);
    if(!p)
    {
        DisposePixMap(pm);
        DisposeHandle(h);
        return memFullErr;
    }
    PIXMAP_BASEADDR(pm) = p;
    {
        ThePortGuard guard(wmgr_port);
        WRAPPER_PIXMAP_FOR_COPY(wrapper);
        WRAPPER_SET_PIXMAP(wrapper, pm);
        CopyBits(PORT_BITS_FOR_COPY(wmgr_port), wrapper, pb, pb, srcCopy, nullptr);
    }
    SavedBits *sb = (SavedBits *)*h;
    sb->bounds = *pb;
    sb->pixmap = pm;
    *bits = h;
    return noErr;
}

OSErr Executor::C_DiscardBits(Handle bits)
{
    if(!bits)
        return noErr;
    PixMapHandle pm = ((SavedBits *)*bits)->pixmap;
    if(pm)
    {
        if(PIXMAP_BASEADDR(pm))
            DisposePtr(PIXMAP_BASEADDR(pm));
        DisposePixMap(pm);
    }
    DisposeHandle(bits);
    return noErr;
}

OSErr Executor::C_RestoreBits(Handle bits)
{
    if(!bits)
        return noErr;
    SavedBits *sb = (SavedBits *)*bits;
    PixMapHandle pm = sb->pixmap;
    if(pm)
    {
        Rect r = sb->bounds;
        ThePortGuard guard(wmgr_port);
        RgnHandle saved_clip = NewRgn();
        GetClip(saved_clip);
        ClipRect(&r);
        WRAPPER_PIXMAP_FOR_COPY(wrapper);
        WRAPPER_SET_PIXMAP(wrapper, pm);
        CopyBits(wrapper, PORT_BITS_FOR_COPY(wmgr_port), &r, &r, srcCopy, nullptr);
        SetClip(saved_clip);
        DisposeRgn(saved_clip);
    }
    return C_DiscardBits(bits);
}
