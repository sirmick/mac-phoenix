/* Copyright 1995, 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

// FIXME: #warning the icon suite representation is our own brew -- tests should
// FIXME: #warning be written and we should do what the Mac does

#include <base/common.h>

#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <Iconutil.h>

#include <quickdraw/cquick.h>
#include <res/resource.h>
#include <mman/mman.h>
#include <rsys/icon.h>
#include <base/functions.impl.h>

#include <algorithm>
#include <cstring>

using namespace Executor;

#define ICON_RETURN_ERROR(error)                                     \
    do{                                                               \
        OSErr _error_ = (error);                                     \
                                                                     \
        if(_error_ != noErr)                                         \
            warning_unexpected("error `%s', `%d'", #error, _error_); \
        return _error_;                                              \
    }while(false)

OSErr Executor::C_PlotIconID(const Rect *rect, IconAlignmentType align,
                             IconTransformType transform, short res_id)
{
    Handle icon_suite;
    OSErr err;

    err = GetIconSuite(out(icon_suite), res_id, svAllAvailableData);
    if(err != noErr)
        ICON_RETURN_ERROR(err);

    err = PlotIconSuite(rect, align, transform, icon_suite);
    if(err != noErr)
        ICON_RETURN_ERROR(err);

    DisposeIconSuite(icon_suite, false);

    ICON_RETURN_ERROR(noErr);
}

void Executor::C_PlotIcon(const Rect *rect, Handle icon)
{
    if(icon == nullptr)
        return;

    if(!*icon)
        LoadResource(icon);

    HLockGuard guard(icon);
    BitMap bm;

    bm.baseAddr = *icon;
    bm.rowBytes = 4;
    bm.bounds.left = bm.bounds.top = 0;
    if(GetHandleSize(icon) == 2 * 16)
    {
        bm.rowBytes = 2;
        bm.bounds.bottom = 16;
    }
    else
    {
        bm.rowBytes = 4;
        bm.bounds.bottom = 32;
    }
    bm.bounds.right = bm.bounds.bottom;
    CopyBits(&bm, PORT_BITS_FOR_COPY(qdGlobals().thePort), &bm.bounds, rect,
             srcCopy, nullptr);
}

OSErr Executor::C_PlotIconHandle(const Rect *rect, IconAlignmentType align,
                                 IconTransformType transform, Handle icon)
{
    /* #### change plotting routines to respect alignment and transform */
    if(align != atNone)
        warning_unimplemented("unhandled icon alignment `%d'", align);
    if(transform != ttNone)
        warning_unimplemented("unhandled icon transform `%d'", transform);

    PlotIcon(rect, icon);

    ICON_RETURN_ERROR(noErr);
}

void Executor::C_PlotCIcon(const Rect *rect, CIconHandle icon)
{
    /* when plotting, `ignore' the current fg/bk colors */
    GrafPtr current_port;
    RGBColor bk_rgb, fg_rgb;
    GUEST<int32_t> bk_color, fg_color;
    int cgrafport_p;

    if(!icon)
        return;

    current_port = qdGlobals().thePort;

    cgrafport_p = CGrafPort_p(current_port);
    if(cgrafport_p)
    {
        fg_rgb = CPORT_RGB_FG_COLOR(current_port);
        bk_rgb = CPORT_RGB_BK_COLOR(current_port);
    }
    fg_color = PORT_FG_COLOR(current_port);
    bk_color = PORT_BK_COLOR(current_port);

    RGBForeColor(&ROMlib_black_rgb_color);
    RGBBackColor(&ROMlib_white_rgb_color);

    HLockGuard guard(icon);
    PixMapHandle gd_pixmap;

    BitMap *mask_bm;
    BitMap *icon_bm;

    icon_bm = &CICON_BMAP(icon);

    mask_bm = &CICON_MASK(icon);
    BITMAP_BASEADDR(mask_bm) = (Ptr)CICON_MASK_DATA(icon);

    gd_pixmap = GD_PMAP(LM(MainDevice));

    if((PORT_BASEADDR(current_port) == PIXMAP_BASEADDR(gd_pixmap)
        && PIXMAP_PIXEL_SIZE(gd_pixmap) > 2)
       || (CGrafPort_p(current_port)
           && PIXMAP_PIXEL_SIZE(CPORT_PIXMAP(current_port)) > 2)
       || !BITMAP_ROWBYTES(icon_bm))
    {
        Handle icon_data;

        icon_data = CICON_DATA(icon);
        HLockGuard guard(icon_data);

        PixMap *icon_pm;

        icon_pm = &CICON_PMAP(icon);
        BITMAP_BASEADDR(icon_pm) = *icon_data;

        CopyMask((BitMap *)icon_pm,
                 mask_bm,
                 PORT_BITS_FOR_COPY(current_port),
                 &BITMAP_BOUNDS(icon_pm),
                 &BITMAP_BOUNDS(mask_bm),
                 /* #### fix up the need for this cast */
                 (Rect *)rect);
    }
    else
    {
        Rect *icon_bm_bounds;
        Ptr bm_baseaddr;
        int height;
        int mask_data_size;

        icon_bm_bounds = &BITMAP_BOUNDS(icon_bm);

        height = RECT_HEIGHT(icon_bm_bounds);
        mask_data_size = BITMAP_ROWBYTES(icon_bm) * height;
        bm_baseaddr = (Ptr)((char *)CICON_MASK_DATA(icon)
                            + mask_data_size);

        BITMAP_BASEADDR(icon_bm) = bm_baseaddr;
        CopyMask(icon_bm,
                 mask_bm,
                 PORT_BITS_FOR_COPY(current_port),
                 icon_bm_bounds,
                 &BITMAP_BOUNDS(mask_bm),
                 /* #### fix up the need for this cast */
                 (Rect *)rect);
    }

    if(cgrafport_p)
    {
        CPORT_RGB_FG_COLOR(current_port) = fg_rgb;
        CPORT_RGB_BK_COLOR(current_port) = bk_rgb;
    }
    PORT_FG_COLOR(current_port) = fg_color;
    PORT_BK_COLOR(current_port) = bk_color;
}

OSErr Executor::C_PlotCIconHandle(const Rect *rect, IconAlignmentType align,
                                  IconTransformType transform, CIconHandle icon)
{
    /* #### change plotting routines to respect alignment and transform */
    if(align != atNone)
        warning_unimplemented("unhandled icon alignment `%d'", align);
    if(transform != ttNone)
        warning_unimplemented("unhandled icon transform `%d'", transform);

    PlotCIcon(rect, icon);

    ICON_RETURN_ERROR(noErr);
}

OSErr Executor::C_PlotSICNHandle(const Rect *rect, IconAlignmentType align,
                                 IconTransformType transform, Handle icon)
{
    /* #### change plotting routines to respect alignment and transform */
    if(align != atNone)
        warning_unimplemented("unhandled icon alignment `%d'", align);
    if(transform != ttNone)
        warning_unimplemented("unhandled icon transform `%d'", transform);

    PlotIcon(rect, icon);

    ICON_RETURN_ERROR(noErr);
}

Handle Executor::C_GetIcon(short icon_id)
{
    return ROMlib_getrestid("ICON"_4, icon_id);
}

CIconHandle Executor::C_GetCIcon(short icon_id)
{
    CIconHandle cicon_handle;
    CIconHandle cicon_res_handle;
    CIconPtr cicon_res;
    int height;
    int mask_data_size;
    int bmap_data_size;
    int new_size;

    cicon_res_handle = (CIconHandle)ROMlib_getrestid("cicn"_4, icon_id);
    if(cicon_res_handle == nullptr)
        return nullptr;

    cicon_res = *cicon_res_handle;
    height = RECT_HEIGHT(&cicon_res->iconPMap.bounds);
    mask_data_size = cicon_res->iconMask.rowBytes * height;
    bmap_data_size = cicon_res->iconBMap.rowBytes * height;
    new_size = sizeof(CIcon) - sizeof(INTEGER) + mask_data_size + bmap_data_size;

    cicon_handle = (CIconHandle)NewHandle(new_size);
    HLockGuard guard1(cicon_handle), guard2(cicon_res_handle);

    CTabPtr tmp_ctab;
    int mask_data_offset;
    int bmap_data_offset;
    int pmap_ctab_offset;
    int pmap_ctab_size;
    int pmap_data_offset;
    int pmap_data_size;
    CIconPtr cicon;

    cicon = *cicon_handle;
    cicon_res = *cicon_res_handle;

    BlockMoveData((Ptr)cicon_res, (Ptr)cicon, new_size);

    mask_data_offset = 0;

    bmap_data_offset = mask_data_size;

    pmap_ctab_offset = bmap_data_offset + bmap_data_size;
    tmp_ctab = (CTabPtr)((char *)&cicon_res->iconMaskData + pmap_ctab_offset);
    pmap_ctab_size = sizeof(ColorTable) + (tmp_ctab->ctSize
                                           * sizeof(ColorSpec));

    pmap_data_offset = pmap_ctab_offset + pmap_ctab_size;
    pmap_data_size = (cicon->iconPMap.rowBytes
                      & ROWBYTES_VALUE_BITS)
        * height;

    cicon->iconMask.baseAddr = nullptr;

    cicon->iconBMap.baseAddr = nullptr;

    {
        CTabHandle color_table;

        color_table
            = (CTabHandle)NewHandle(pmap_ctab_size);
        BlockMoveData((Ptr)&cicon_res->iconMaskData + pmap_ctab_offset,
                      (Ptr)*color_table,
                      pmap_ctab_size);
        CTAB_SEED(color_table) = GetCTSeed();
        cicon->iconPMap.pmTable = color_table;

        cicon->iconPMap.baseAddr = nullptr;
        cicon->iconData = NewHandle(pmap_data_size);
        BlockMoveData((Ptr)&cicon_res->iconMaskData + pmap_data_offset,
                      (Ptr)(*cicon->iconData),
                      pmap_data_size);
    }

    return cicon_handle;
}

void Executor::C_DisposeCIcon(CIconHandle icon)
{
    DisposeHandle(CICON_DATA(icon));
    DisposeHandle((Handle)CICON_PMAP(icon).pmTable);
    DisposeHandle((Handle)icon);
}

typedef struct
{
    RGBColor rgb_color;
    Str255 string;
} label_info_t;

static label_info_t labels[7] = {
    {
        {
            0, 0, 0,
        },
        "\011Essential",
    },
    {
        {
            0, 0, 0,
        },
        "\03Hot",
    },
    {
        {
            0, 0, 0,
        },
        "\013In Progress",
    },
    {
        {
            0, 0, 0,
        },
        "\04Cool",
    },
    {
        {
            0, 0, 0,
        },
        "\010Personal",
    },
    {
        {
            0, 0, 0,
        },
        "\011Project 1",
    },
    {
        {
            0, 0, 0,
        },
        "\011Project 2",
    },
};

OSErr Executor::C_GetLabel(short label, RGBColor *label_color,
                           Str255 label_string)
{
    unsigned int index;
    OSErr retval;
    static bool been_here = false;

    if(!been_here)
    {
        /* icky */
        labels[0].rgb_color = ROMlib_QDColors[1].rgb; /* orange->yellow */
        labels[1].rgb_color = ROMlib_QDColors[3].rgb; /* red */
        labels[2].rgb_color = ROMlib_QDColors[2].rgb; /* magenta */
        labels[3].rgb_color = ROMlib_QDColors[4].rgb; /* cyan */
        labels[4].rgb_color = ROMlib_QDColors[6].rgb; /* blue */
        labels[5].rgb_color = ROMlib_QDColors[5].rgb; /* green */
        labels[6].rgb_color = ROMlib_QDColors[0].rgb; /* brown->black */
        been_here = true;
    }

    index = label - 1;
    if(label == 0)
    {
        /* No label: 7.5.5 accepts it (Finder asks at start-up); black and
           an empty name are a guess. */
        if(label_color)
            *label_color = ROMlib_QDColors[0].rgb;
        if(label_string)
            label_string[0] = 0;
        retval = noErr;
    }
    else if(index > 6)
        retval = paramErr;
    else
    {
        // Either output may be nil (Finder asks for colours only).
        if(label_color)
            *label_color = labels[index].rgb_color;
        if(label_string)
            str255assign((StringPtr)label_string,
                         (StringPtr)labels[index].string);
        retval = noErr;
    }

    ICON_RETURN_ERROR(retval);
}

/* Icon families (IM More Macintosh Toolbox ch. 5, Icon Utilities).
 *
 * MacPhoenix rewrite of Executor's suite code. Suites and icon caches share
 * one layout (rsys/icon.h); a cache is a suite whose missing members come
 * from its getter. Every drawing, region and hit-test call works on an
 * IconSource: a suite, a cache, or a PlotIconMethod-style getter. */

namespace
{
const ResType restype_for_icon[N_SUITE_ICONS] = {
    large1BitMask, large4BitData, large8BitData,
    small1BitMask, small4BitData, small8BitData,
};

const IconSelectorValue mask_for_icon[N_SUITE_ICONS] = {
    svLarge1Bit, svLarge4Bit, svLarge8Bit,
    svSmall1Bit, svSmall4Bit, svSmall8Bit,
};

enum
{
    kLargeBase = 0, /* ICN#, icl4, icl8 */
    kSmallBase = 3, /* ics#, ics4, ics8 */
    kSuiteIsCache = 1,
};

int restype_to_index(ResType type)
{
    for(int i = 0; i < N_SUITE_ICONS; i++)
        if(type == restype_for_icon[i])
            return i;
    return -1;
}

suite_layout_t *layout(Handle suite)
{
    return (suite_layout_t *)*suite;
}

/* A purged resource member is reloaded; a purged non-resource is gone. */
Handle usable(Handle h)
{
    if(!h)
        return nullptr;
    if(!*h && (HGetState(h) & RSRCBIT))
        LoadResource(h);
    return *h ? h : nullptr;
}

struct IconSource
{
    virtual ~IconSource() = default;
    virtual Handle get(int index) = 0;
    virtual int label() { return 0; }
};

struct SuiteSource : IconSource
{
    Handle suite;
    explicit SuiteSource(Handle s) : suite(s) {}

    Handle get(int index) override
    {
        Handle h = usable(layout(suite)->icons[index]);
        if(!h && (layout(suite)->flags & kSuiteIsCache) && layout(suite)->cacheProc)
        {
            IconGetterUPP proc = layout(suite)->cacheProc;
            h = proc(restype_for_icon[index], layout(suite)->cacheData);
            /* the getter may move memory */
            layout(suite)->icons[index] = h;
            h = usable(h);
        }
        return h;
    }
    int label() override { return layout(suite)->label; }
};

struct MethodSource : IconSource
{
    IconGetterUPP method;
    void *data;
    Handle got[N_SUITE_ICONS] = {};
    bool asked[N_SUITE_ICONS] = {};
    MethodSource(IconGetterUPP m, void *d) : method(m), data(d) {}

    Handle get(int index) override
    {
        if(!asked[index])
        {
            asked[index] = true;
            got[index] = method(restype_for_icon[index], data);
        }
        return usable(got[index]);
    }
};

/* Which member to draw for a rectangle: small icons below 32 pixels; the
   deepest colour data the port can show; the mask is always the second
   half of the black-and-white member. */
struct Chosen
{
    Handle mask = nullptr; /* ICN# / ics# */
    Handle data = nullptr; /* mask, icl4/8 or ics4/8 */
    int size = 32;
    int bpp = 1;
};

int port_depth()
{
    GrafPtr port = qdGlobals().thePort;
    return CGrafPort_p(port) ? (int)toHost(PIXMAP_PIXEL_SIZE(CPORT_PIXMAP(port))) : 1;
}

OSErr choose(IconSource& src, const Rect *rect, Chosen& c, bool needData = true)
{
    bool small = RECT_WIDTH(rect) < 32 || RECT_HEIGHT(rect) < 32;
    int base = small ? kSmallBase : kLargeBase;
    c.mask = src.get(base);
    if(!c.mask)
    {
        base = small ? kLargeBase : kSmallBase;
        c.mask = src.get(base);
        if(!c.mask)
            return noMaskFoundErr;
    }
    c.size = base == kSmallBase ? 16 : 32;
    c.data = c.mask;
    c.bpp = 1;
    if(!needData)
        return noErr;
    int depth = port_depth();
    if(depth >= 8 && (c.data = src.get(base + 2)))
        c.bpp = 8;
    else if(depth >= 4 && (c.data = src.get(base + 1)))
        c.bpp = 4;
    else
    {
        c.data = c.mask;
        c.bpp = 1;
    }
    /* a member too short for its type is ignored */
    if(GetHandleSize(c.data) < c.size * c.size * c.bpp / 8)
    {
        c.data = c.mask;
        c.bpp = 1;
    }
    if(GetHandleSize(c.mask) < c.size * c.size / 4)
        return noMaskFoundErr;
    return noErr;
}

/* Where the icon goes inside rect: atNone fills the rectangle; otherwise
   the icon keeps its size (shrunk proportionally if it doesn't fit) and is
   placed by the alignment. */
Rect place(const Rect *rect, IconAlignmentType align, int size)
{
    Rect r = *rect;
    if(align == atNone)
        return r;
    int w = RECT_WIDTH(rect), h = RECT_HEIGHT(rect);
    int s = std::min({ size, w, h });
    int dx = 0, dy = 0;
    switch(align & 3)
    {
        case atTop: dy = 0; break;
        case atBottom: dy = h - s; break;
        case atVerticalCenter: dy = (h - s) / 2; break;
        default: dy = (h - s) / 2; break; /* no vertical alignment: centre */
    }
    switch(align & 12)
    {
        case atLeft: dx = 0; break;
        case atRight: dx = w - s; break;
        default: dx = (w - s) / 2; break;
    }
    r.left = rect->left + dx;
    r.top = rect->top + dy;
    r.right = r.left + s;
    r.bottom = r.top + s;
    return r;
}

/* A rows x rowBytes copy of bits in the current heap (BitMap needs a guest
   address). */
Ptr copy_bits(const void *bits, int bytes)
{
    Ptr p = NewPtr(bytes);
    if(p)
        memcpy(p, bits, bytes);
    return p;
}

const uint8_t gray_rows[2] = { 0xAA, 0x55 };     /* 50% */
const uint8_t ltgray_rows[4] = { 0x88, 0x00, 0x22, 0x00 }; /* 12.5% */

void tint(RGBColor& c, const RGBColor& label, bool selected, bool disabled)
{
    auto ch = [&](uint16_t v, uint16_t l) {
        uint32_t x = v;
        if(label.red | label.green | label.blue | 1)
            x = x * l / 0xFFFF;
        if(selected)
            x /= 2;
        if(disabled)
            x = (x + 0xFFFF) / 2;
        return (uint16_t)x;
    };
    c.red = ch(c.red, label.red);
    c.green = ch(c.green, label.green);
    c.blue = ch(c.blue, label.blue);
}

OSErr plot(IconSource& src, const Rect *rect, IconAlignmentType align,
           IconTransformType transform)
{
    Chosen c;
    OSErr err = choose(src, rect, c);
    if(err != noErr)
        return err;

    int label = (transform >> 8) & 0xF;
    if(!label)
        label = src.label() & 0xF;
    int state = transform & 0xFF;
    bool selected = (transform & ttSelected) != 0;
    bool disabled = state == ttDisabled;
    bool patterned = state == ttOpen || state == ttOffline;

    RGBColor label_rgb = ROMlib_black_rgb_color;
    bool labelled = label >= 1 && label <= 7 && port_depth() > 1;
    if(labelled)
        GetLabel(label, &label_rgb, nullptr);

    int n = c.size;
    int mask_bytes = n * n / 8;
    HLockGuard g1(c.mask), g2(c.data);
    const uint8_t *mask_src = (const uint8_t *)*c.mask + mask_bytes;

    Rect icon_rect = { 0, 0, (int16_t)n, (int16_t)n };
    Rect dst = place(rect, align, n);
    GrafPtr port = qdGlobals().thePort;

    Ptr mask_p = copy_bits(mask_src, mask_bytes);
    if(!mask_p)
        return memFullErr;
    BitMap mask_bm;
    mask_bm.baseAddr = mask_p;
    mask_bm.rowBytes = n / 8;
    mask_bm.bounds = icon_rect;

    if(c.bpp == 1 || patterned)
    {
        /* black and white: transforms work on the bits themselves */
        Ptr data_p = copy_bits(*c.mask, mask_bytes);
        if(!data_p)
        {
            DisposePtr(mask_p);
            return memFullErr;
        }
        uint8_t *d = (uint8_t *)data_p;
        const uint8_t *m = (const uint8_t *)mask_p;
        for(int y = 0; y < n; y++)
            for(int x = 0; x < n / 8; x++)
            {
                int i = y * (n / 8) + x;
                if(state == ttOpen)
                    d[i] = m[i] & gray_rows[y & 1];
                else if(state == ttOffline)
                    d[i] = (d[i] & ~m[i]) | (m[i] & ltgray_rows[y & 3]);
                if(disabled)
                    d[i] &= gray_rows[y & 1];
                if(selected)
                    d[i] = m[i] & ~d[i];
            }
        BitMap data_bm = mask_bm;
        data_bm.baseAddr = data_p;

        GUEST<int32_t> fg = PORT_FG_COLOR(port), bk = PORT_BK_COLOR(port);
        RGBColor fg_rgb, bk_rgb;
        bool color_port = CGrafPort_p(port);
        if(color_port)
        {
            fg_rgb = CPORT_RGB_FG_COLOR(port);
            bk_rgb = CPORT_RGB_BK_COLOR(port);
            RGBForeColor(labelled ? &label_rgb : &ROMlib_black_rgb_color);
            RGBBackColor(&ROMlib_white_rgb_color);
        }
        CopyMask(&data_bm, &mask_bm, PORT_BITS_FOR_COPY(port), &icon_rect,
                 &icon_rect, &dst);
        if(color_port)
        {
            RGBForeColor(&fg_rgb);
            RGBBackColor(&bk_rgb);
        }
        PORT_FG_COLOR(port) = fg;
        PORT_BK_COLOR(port) = bk;
        DisposePtr(data_p);
    }
    else
    {
        /* colour: label, selection and dimming change the colour table */
        CTabHandle ctab = GetCTable(c.bpp);
        if(!ctab)
        {
            DisposePtr(mask_p);
            return memFullErr;
        }
        if(labelled || selected || disabled)
        {
            HLockGuard g3(ctab);
            int count = CTAB_SIZE(ctab) + 1;
            for(int i = 0; i < count; i++)
            {
                RGBColor rgb = CTAB_TABLE(ctab)[i].rgb;
                tint(rgb, labelled ? label_rgb : ROMlib_white_rgb_color, selected, disabled);
                CTAB_TABLE(ctab)[i].rgb = rgb;
            }
            CTAB_SEED(ctab) = GetCTSeed();
        }
        PixMap pm;
        memset(&pm, 0, sizeof pm);
        pm.baseAddr = *c.data;
        pm.rowBytes = (n * c.bpp / 8) | PIXMAP_DEFAULT_ROW_BYTES;
        pm.bounds = icon_rect;
        pm.pixelSize = pm.cmpSize = c.bpp;
        pm.cmpCount = 1;
        pm.pmTable = ctab;

        GUEST<int32_t> fg = PORT_FG_COLOR(port), bk = PORT_BK_COLOR(port);
        RGBColor fg_rgb = CPORT_RGB_FG_COLOR(port), bk_rgb = CPORT_RGB_BK_COLOR(port);
        RGBForeColor(&ROMlib_black_rgb_color);
        RGBBackColor(&ROMlib_white_rgb_color);
        CopyMask((BitMap *)&pm, &mask_bm, PORT_BITS_FOR_COPY(port), &icon_rect,
                 &icon_rect, &dst);
        RGBForeColor(&fg_rgb);
        RGBBackColor(&bk_rgb);
        PORT_FG_COLOR(port) = fg;
        PORT_BK_COLOR(port) = bk;
        DisposeCTable(ctab);
    }
    DisposePtr(mask_p);
    return noErr;
}

/* The mask as a region over the placed icon. */
OSErr to_rgn(IconSource& src, RgnHandle rgn, const Rect *rect, IconAlignmentType align)
{
    Chosen c;
    OSErr err = choose(src, rect, c, false);
    if(err != noErr)
        return err;
    int n = c.size;
    HLockGuard g(c.mask);
    Ptr mask_p = copy_bits((const uint8_t *)*c.mask + n * n / 8, n * n / 8);
    if(!mask_p)
        return memFullErr;
    BitMap bm;
    bm.baseAddr = mask_p;
    bm.rowBytes = n / 8;
    bm.bounds = { 0, 0, (int16_t)n, (int16_t)n };
    err = BitMapToRegion(rgn, &bm);
    DisposePtr(mask_p);
    if(err != noErr)
        return err;
    Rect icon_rect = bm.bounds;
    Rect dst = place(rect, align, n);
    MapRgn(rgn, &icon_rect, &dst);
    return noErr;
}

bool pt_in(IconSource& src, Point pt, const Rect *rect, IconAlignmentType align)
{
    Chosen c;
    if(choose(src, rect, c, false) != noErr)
        return false;
    Rect dst = place(rect, align, c.size);
    if(!PtInRect(pt, &dst))
        return false;
    int n = c.size;
    int x = (pt.h - dst.left) * n / RECT_WIDTH(&dst);
    int y = (pt.v - dst.top) * n / RECT_HEIGHT(&dst);
    const uint8_t *m = (const uint8_t *)*c.mask + n * n / 8;
    return (m[y * (n / 8) + x / 8] >> (7 - (x & 7))) & 1;
}

bool rect_in(IconSource& src, const Rect *test, const Rect *rect, IconAlignmentType align)
{
    RgnHandle rgn = NewRgn();
    bool in = to_rgn(src, rgn, rect, align) == noErr && RectInRgn(test, rgn);
    DisposeRgn(rgn);
    return in;
}

Handle new_suite()
{
    return NewHandleClear(sizeof(suite_layout_t));
}
}

OSErr Executor::C_GetIconSuite(GUEST<Handle> *icon_suite_return, short res_id,
                               IconSelectorValue selector)
{
    *icon_suite_return = nullptr;
    Handle suite = new_suite();
    if(!suite)
        ICON_RETURN_ERROR(memFullErr);
    for(int i = 0; i < N_SUITE_ICONS; i++)
        if(selector & mask_for_icon[i])
        {
            Handle icon = GetResource(restype_for_icon[i], res_id);
            layout(suite)->icons[i] = icon;
        }
    *icon_suite_return = suite;
    ICON_RETURN_ERROR(noErr);
}

OSErr Executor::C_NewIconSuite(GUEST<Handle> *icon_suite_return)
{
    Handle suite = new_suite();
    *icon_suite_return = suite;
    if(!suite)
        ICON_RETURN_ERROR(memFullErr);
    ICON_RETURN_ERROR(noErr);
}

OSErr Executor::C_AddIconToSuite(Handle icon_data, Handle icon_suite,
                                 ResType type)
{
    int i = restype_to_index(type);
    if(i < 0)
        ICON_RETURN_ERROR(paramErr);
    layout(icon_suite)->icons[i] = icon_data;
    ICON_RETURN_ERROR(noErr);
}

OSErr Executor::C_GetIconFromSuite(GUEST<Handle> *icon_data_return,
                                   Handle icon_suite, ResType type)
{
    int i = restype_to_index(type);
    Handle h = i < 0 ? nullptr : (Handle)layout(icon_suite)->icons[i];
    if(!h)
        ICON_RETURN_ERROR(paramErr);
    *icon_data_return = h;
    ICON_RETURN_ERROR(noErr);
}

OSErr Executor::C_PlotIconSuite(const Rect *rect, IconAlignmentType align,
                                IconTransformType transform, Handle icon_suite)
{
    SuiteSource src(icon_suite);
    HLockGuard guard(icon_suite);
    ICON_RETURN_ERROR(plot(src, rect, align, transform));
}

OSErr Executor::C_PlotIconMethod(const Rect *rect, IconAlignmentType align,
                                 IconTransformType transform,
                                 IconGetterUPP method, void *data)
{
    MethodSource src(method, data);
    ICON_RETURN_ERROR(plot(src, rect, align, transform));
}

OSErr Executor::C_ForEachIconDo(Handle suite, IconSelectorValue selector,
                                IconActionUPP action, void *data)
{
    for(int i = 0; i < N_SUITE_ICONS; i++)
    {
        if(!(selector & mask_for_icon[i]))
            continue;
        GUEST<Handle> h = layout(suite)->icons[i];
        OSErr err = action(restype_for_icon[i], &h, data);
        layout(suite)->icons[i] = h;
        if(err != noErr)
            return err;
    }
    return noErr;
}

short Executor::C_GetSuiteLabel(Handle suite)
{
    return layout(suite)->label;
}

OSErr Executor::C_SetSuiteLabel(Handle suite, short label)
{
    if(label < 0 || label > 7)
        ICON_RETURN_ERROR(paramErr);
    layout(suite)->label = label;
    return noErr;
}

OSErr Executor::C_DisposeIconSuite(Handle suite, Boolean dispose_data_p)
{
    if(dispose_data_p)
        for(int i = 0; i < N_SUITE_ICONS; i++)
        {
            Handle icon = layout(suite)->icons[i];
            if(icon && !(HGetState(icon) & RSRCBIT))
                DisposeHandle(icon);
        }
    DisposeHandle(suite);
    ICON_RETURN_ERROR(noErr);
}

OSErr Executor::C_IconSuiteToRgn(RgnHandle rgn, const Rect *rect,
                                 IconAlignmentType align, Handle suite)
{
    SuiteSource src(suite);
    ICON_RETURN_ERROR(to_rgn(src, rgn, rect, align));
}

OSErr Executor::C_IconIDToRgn(RgnHandle rgn, const Rect *rect,
                              IconAlignmentType align, short icon_id)
{
    Handle suite;
    OSErr err = GetIconSuite(out(suite), icon_id, svAllAvailableData);
    if(err != noErr)
        ICON_RETURN_ERROR(err);
    err = IconSuiteToRgn(rgn, rect, align, suite);
    DisposeIconSuite(suite, false);
    ICON_RETURN_ERROR(err);
}

OSErr Executor::C_IconMethodToRgn(RgnHandle rgn, const Rect *rect,
                                  IconAlignmentType align,
                                  IconGetterUPP method, void *data)
{
    MethodSource src(method, data);
    ICON_RETURN_ERROR(to_rgn(src, rgn, rect, align));
}

Boolean Executor::C_PtInIconSuite(Point test_pt, const Rect *rect,
                                  IconAlignmentType align, Handle suite)
{
    SuiteSource src(suite);
    return pt_in(src, test_pt, rect, align);
}

Boolean Executor::C_PtInIconID(Point test_pt, const Rect *rect,
                               IconAlignmentType align, short icon_id)
{
    Handle suite;
    if(GetIconSuite(out(suite), icon_id, svAllAvailableData) != noErr)
        return false;
    Boolean in = PtInIconSuite(test_pt, rect, align, suite);
    DisposeIconSuite(suite, false);
    return in;
}

Boolean Executor::C_PtInIconMethod(Point test_pt, const Rect *rect,
                                   IconAlignmentType align,
                                   IconGetterUPP method, void *data)
{
    MethodSource src(method, data);
    return pt_in(src, test_pt, rect, align);
}

Boolean Executor::C_RectInIconSuite(const Rect *test_rect, const Rect *rect,
                                    IconAlignmentType align, Handle suite)
{
    SuiteSource src(suite);
    return rect_in(src, test_rect, rect, align);
}

Boolean Executor::C_RectInIconID(const Rect *test_rect, const Rect *rect,
                                 IconAlignmentType align, short icon_id)
{
    Handle suite;
    if(GetIconSuite(out(suite), icon_id, svAllAvailableData) != noErr)
        return false;
    Boolean in = RectInIconSuite(test_rect, rect, align, suite);
    DisposeIconSuite(suite, false);
    return in;
}

Boolean Executor::C_RectInIconMethod(const Rect *test_rect, const Rect *rect,
                                     IconAlignmentType align,
                                     IconGetterUPP method, void *data)
{
    MethodSource src(method, data);
    return rect_in(src, test_rect, rect, align);
}

OSErr Executor::C_MakeIconCache(GUEST<Handle> *cache, IconGetterUPP make_icon,
                                void *data)
{
    Handle suite = new_suite();
    *cache = suite;
    if(!suite)
        ICON_RETURN_ERROR(memFullErr);
    layout(suite)->flags = kSuiteIsCache;
    layout(suite)->cacheProc = make_icon;
    layout(suite)->cacheData = data;
    ICON_RETURN_ERROR(noErr);
}

/* Fetch the members a plot of rect would use, without drawing. */
OSErr Executor::C_LoadIconCache(const Rect *rect, IconAlignmentType align,
                                IconTransformType transform, Handle cache)
{
    SuiteSource src(cache);
    Chosen c;
    ICON_RETURN_ERROR(choose(src, rect, c));
}

OSErr Executor::C_GetIconCacheData(Handle cache, GUEST<void *> *data)
{
    *data = layout(cache)->cacheData;
    return noErr;
}

OSErr Executor::C_SetIconCacheData(Handle cache, void *data)
{
    layout(cache)->cacheData = data;
    return noErr;
}

OSErr Executor::C_GetIconCacheProc(Handle cache, GUEST<IconGetterUPP> *proc)
{
    *proc = layout(cache)->cacheProc;
    return noErr;
}

OSErr Executor::C_SetIconCacheProc(Handle cache, IconGetterUPP proc)
{
    layout(cache)->cacheProc = proc;
    return noErr;
}

/* MacPhoenix: see SetIconDrawContext in Iconutil.yaml. Executor's icon
   drawing has no use for the value; it is kept for GetIconDrawContext. */
static int32_t icon_draw_context;

void Executor::C_SetIconDrawContext(int32_t context)
{
    icon_draw_context = context;
}

void Executor::C_GetIconDrawContext(GUEST<int32_t> *context)
{
    *context = icon_draw_context;
}
