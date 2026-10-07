#if !defined(_CQUICK_H_)
#define _CQUICK_H_

#include <CQuickDraw.h>
#include <Iconutil.h>
#include <MemoryMgr.h>

#include <quickdraw/rgbutil.h>
#include <error/error.h>

namespace Executor
{
#define SET_HILITE_BIT() \
    (LM(HiliteMode) |= 1 << hiliteBit)

#define CLEAR_HILITE_BIT() \
    (LM(HiliteMode) &= ~(1 << hiliteBit))

typedef struct GrafVars
{
    GUEST_STRUCT;
    GUEST<RGBColor> rgbOpColor;
    GUEST<RGBColor> rgbHiliteColor;
    GUEST<Handle> pmFgColor;
    GUEST<INTEGER> pmFgIndex;
    GUEST<Handle> pmBkColor;
    GUEST<INTEGER> pmBkIndex;
    GUEST<INTEGER> pmFlags;
} * GrafVarsPtr;

typedef GUEST<GrafVarsPtr> *GrafVarsHandle;

#define SAFE_PTR(ptr) (gui_assert(ptr))
#define SAFE_HANDLE(handle) (gui_assert(handle), gui_assert((handle)->p))

/* CGrafPort accessors

   these macros provide accessors for both GrafPort and CGrafPort
   types.  for those members which exist in both, PORT_... accessors
   are provided.  when the member exists in both types but reside in
   different locations in the structures, the PORT_FIELD accessor is
   used, otherwise a strait `->' reference is used.  for GrafPort only
   members PORT_... accessors are provided, for CGrafPort only
   members, CPORT_... accessors are provided.

   FIXME: all accessors explictly cast thier arguments to the
   appropriate type; this allows the use of these macros with
   WindowPeek and WindowPtr arguments.  _i'm not sure if this will
   lead to type errors_. */

/* determine if the given port is a CGrafPort by checking the high two
   bits of the CGrafPort::portVersion/GrafPort::portBits::rowBytes */
#define CGrafPort_p(port) ((((char *)(port))[6] & 0xC0) == 0xC0)

static inline CGrafPtr ASSERT_CPORT(void *port)
{
#if !defined(NDEBUG)
    gui_assert(CGrafPort_p(port));
#endif
    return (CGrafPtr)port;
}

static inline GrafPtr ASSERT_NOT_CPORT(void *port)
{
#if !defined(NDEBUG)
    gui_assert(!CGrafPort_p(port));
#endif
    return (GrafPtr)port;
}

/* field accessors that exist in both types but at different locations */
/* return the offset of the given field for the given port
   determines the type of the port using CGrafPort_p () */
#define PORT_FIELD_OFFSET(field, port) (CGrafPort_p(port)                \
                                            ? offsetof(CGrafPort, field) \
                                            : offsetof(GrafPort, field))

/* return the field of the port */
#define PORT_FIELD(port, field)                             \
    (*(decltype(((GrafPtr)(port))->field) *)((char *)(port) \
                                             + PORT_FIELD_OFFSET(field, port)))

/* aggregate type fields do not require byte swapping */
#define PORT_RECT(port) PORT_FIELD(port, portRect)
#define PORT_PEN_LOC(port) PORT_FIELD(port, pnLoc)
#define PORT_PEN_SIZE(port) PORT_FIELD(port, pnSize)

/* big endian byte order */
#define PORT_VIS_REGION(port) PORT_FIELD(port, visRgn)
#define PORT_CLIP_REGION(port) PORT_FIELD(port, clipRgn)
#define PORT_PEN_MODE(port) PORT_FIELD(port, pnMode)
/* native byte order */




/* field accessors for members which exist in both types and are in
   the same location */
/* big endian byte order */
#define PORT_DEVICE(port) (((GrafPtr)(port))->device)
#define PORT_PEN_VIS(port) (((GrafPtr)(port))->pnVis)
#define PORT_TX_FONT(port) (((GrafPtr)(port))->txFont)
#define PORT_TX_FACE(port) (((GrafPtr)(port))->txFace)
#define PORT_TX_MODE(port) (((GrafPtr)(port))->txMode)
#define PORT_TX_SIZE(port) (((GrafPtr)(port))->txSize)
#define PORT_SP_EXTRA(port) (((GrafPtr)(port))->spExtra)
#define PORT_FG_COLOR(port) (((GrafPtr)(port))->fgColor)
#define PORT_BK_COLOR(port) (((GrafPtr)(port))->bkColor)
#define PORT_COLR_BIT(port) (((GrafPtr)(port))->colrBit)
#define PORT_PAT_STRETCH(port) (((GrafPtr)(port))->patStretch)
#define PORT_PIC_SAVE(port) (((GrafPtr)(port))->picSave)
#define PORT_REGION_SAVE(port) (((GrafPtr)(port))->rgnSave)
#define PORT_POLY_SAVE(port) (((GrafPtr)(port))->polySave)
/* NOTE: this returns a different type depending if
   the argument is a CGrafPort or not */
#define PORT_GRAF_PROCS(port) (((GrafPtr)(port))->grafProcs)
/* native byte order */














/* NOTE: this returns a different type depending if
   the argument is a CGrafPort or not */


/* accessors for fields which exist only in basic quickdraw graphics
   ports */
#define PORT_BITS(port) (ASSERT_NOT_CPORT(port)->portBits)

#define PORT_BK_PAT(port) (ASSERT_NOT_CPORT(port)->bkPat)

#define PORT_FILL_PAT(port) (ASSERT_NOT_CPORT(port)->fillPat)

#define PORT_PEN_PAT(port) (ASSERT_NOT_CPORT(port)->pnPat)

/* accessors for fields which exist only in color quick draw graphics
   ports */
/* aggregate type fields do not require byte swapping */

#define CPORT_RGB_FG_COLOR(cport) (ASSERT_CPORT(cport)->rgbFgColor)

#define CPORT_RGB_BK_COLOR(cport) (ASSERT_CPORT(cport)->rgbBkColor)
/* big endian byte order */
#define CPORT_PIXMAP(cport) (ASSERT_CPORT(cport)->portPixMap)

#define CPORT_PIXMAP_X_NO_ASSERT(cport) (((CGrafPtr)(cport))->portPixMap)

#define CPORT_VERSION(cport) (ASSERT_CPORT(cport)->portVersion)

#define CPORT_VERSION_X_NO_ASSERT(cport) (((CGrafPtr)(cport))->portVersion)

#define CPORT_CH_EXTRA(cport) (ASSERT_CPORT(cport)->chExtra)

#define CPORT_PENLOC_HFRAC(cport) (ASSERT_CPORT(cport)->pnLocHFrac)

#define CPORT_BK_PIXPAT(cport) (ASSERT_CPORT(cport)->bkPixPat)

#define CPORT_FILL_PIXPAT(cport) (ASSERT_CPORT(cport)->fillPixPat)

#define CPORT_PEN_PIXPAT(cport) (ASSERT_CPORT(cport)->pnPixPat)

#define CPORT_GRAFVARS(cport) (ASSERT_CPORT(cport)->grafVars)

#define CPORT_OP_COLOR(cport) \
    ((*(GrafVarsHandle)CPORT_GRAFVARS(cport))->rgbOpColor)
#define CPORT_HILITE_COLOR(cport) \
    ((*(GrafVarsHandle)CPORT_GRAFVARS(cport))->rgbHiliteColor)

/* general purpose accessor functions */
/* return the bounds of a port, whether it is a GrafPort or CGrafPort */
#define PORT_BOUNDS(port)                                         \
    (*(Rect *)(CGrafPort_p(port)                                  \
                   ? &PIXMAP_BOUNDS(CPORT_PIXMAP((CGrafPtr)port)) \
                   : &(PORT_BITS((GrafPtr)port).bounds)))

#define PORT_BASEADDR(port)                              \
    (CGrafPort_p(port)                                     \
         ? PIXMAP_BASEADDR(CPORT_PIXMAP((CGrafPtr)port)) \
         : PORT_BITS((GrafPtr)port).baseAddr)


/* return true if the given bitmap has the same baseAddr, rowBytes and
   bounds as thePort's bits (portBits or portPixMap) */
#define BITMAP_IS_THEPORT_P(bitmap)                         \
    (!memcmp(bitmap,                                        \
             (CGrafPort_p(qdGlobals().thePort)                          \
                  ? (BitMap *)*CPORT_PIXMAP(theCPort) \
                  : &PORT_BITS(qdGlobals().thePort)),                   \
             sizeof(BitMap)))

/* Return the port bits of the given port suitable for passing
   too StdBits () or CopyBits ();
   offsetof (GrafPort, portBits) == offsetof (CGrafPort, portPixMap) */
#define PORT_BITS_FOR_COPY(port) \
    ((BitMap *)((char *)(port) + offsetof(GrafPort, portBits)))

#define IS_PIXMAP_PTR_P(ptr)                     \
    ((((BitMap *)ptr)->rowBytes & (1 << 15)) \
     && !(((BitMap *)ptr)->rowBytes & (1 << 14)))

/* PixMap accessors */
#define PIXMAP_BOUNDS(pixmap) ((*pixmap)->bounds)

/* big endian byte order */
#define PIXMAP_BASEADDR(pixmap) ((*pixmap)->baseAddr)

const short ROWBYTES_VALUE_BITS = 0x3FFF;

const short ROWBYTES_FLAG_BITS = short(3 << 14);
#define PIXMAP_FLAGS(pixmap) \
    ((*pixmap)->rowBytes & ROWBYTES_FLAG_BITS)


const short PIXMAP_DEFAULT_ROW_BYTES = short(1 << 15);

/* the high two bits of the rowbytes field of a {c}grafport/pixmap
   is reserved; and has the following meaning(s): */
const short PIXMAP_FLAG_BITS = short(2 << 14);
const short CPORT_FLAG_BITS = short(3 << 14);

/* this bit of the port version field of a cgrafport indicates
   the cgrafptr is really a gworldptr */
#define GW_FLAG_BIT (1)

#define GWorld_p(port) \
    (CGrafPort_p(port) \
     && (CPORT_VERSION(port) & GW_FLAG_BIT))

#define PIXMAP_ROWBYTES(pixmap) \
    ((*pixmap)->rowBytes & ROWBYTES_VALUE_BITS)


#define PIXMAP_SET_ROWBYTES(pixmap, value) \
    ((*pixmap)->rowBytes = ((value) & ROWBYTES_VALUE_BITS) | PIXMAP_FLAGS(pixmap))

#define PIXMAP_VERSION(pixmap) ((*pixmap)->pmVersion)
#define PIXMAP_PACK_TYPE(pixmap) ((*pixmap)->packType)
#define PIXMAP_PACK_SIZE(pixmap) ((*pixmap)->packSize)
#define PIXMAP_HRES(pixmap) ((*pixmap)->hRes)
#define PIXMAP_VRES(pixmap) ((*pixmap)->vRes)
#define PIXMAP_PIXEL_TYPE(pixmap) ((*pixmap)->pixelType)
#define PIXMAP_PIXEL_SIZE(pixmap) ((*pixmap)->pixelSize)
#define PIXMAP_CMP_COUNT(pixmap) ((*pixmap)->cmpCount)
#define PIXMAP_CMP_SIZE(pixmap) ((*pixmap)->cmpSize)
#define PIXMAP_PLANE_BYTES(pixmap) ((*pixmap)->planeBytes)
#define PIXMAP_TABLE(pixmap) ((*pixmap)->pmTable)

#define PIXMAP_ASSERT_NOT_SCREEN(pixmap) \
    gui_assert(!active_screen_addr_p(pixmap))

/* used for initializing this field to zero for future
   compatibility */
#define PIXMAP_RESERVED(pixmap) ((*pixmap)->pmReserved)

#define PIXMAP_TABLE_AS_OFFSET(pixmap) (guest_cast<int32_t>(PIXMAP_TABLE(pixmap)))

#define WRAPPER_PIXMAP_FOR_COPY(wrapper_decl_name) \
    BitMap *wrapper_decl_name = (BitMap *)alloca(sizeof(BitMap))

#define WRAPPER_SET_PIXMAP(wrapper, pixmap_h)            \
    do                                                   \
    {                                                    \
        (wrapper)->rowBytes = (short)(3 << 14);          \
        (wrapper)->baseAddr = guest_cast<Ptr>(pixmap_h); \
    } while(0)

/* PixPat accessors */
enum pixpat_pattern_types
{
    pixpat_type_orig = 0,
    pixpat_type_color = 1,
    pixpat_type_rgb = 2,

    pixpat_old_style_pattern = 0,
    pixpat_color_pattern = 1,
    pixpat_rgb_pattern = 2,
};
#define PIXPAT_1DATA(pixpat) ((*pixpat)->pat1Data)

#define PIXPAT_TYPE(pixpat) ((*pixpat)->patType)
#define PIXPAT_MAP(pixpat) ((*pixpat)->patMap)
#define PIXPAT_DATA(pixpat) ((*pixpat)->patData)
#define PIXPAT_XDATA(pixpat) ((*pixpat)->patXData)
#define PIXPAT_XVALID(pixpat) ((*pixpat)->patXValid)
#define PIXPAT_XMAP(pixpat) ((*pixpat)->patXMap)


#define PIXPAT_DATA_AS_OFFSET(pixpat) (guest_cast<int32_t>(PIXPAT_DATA(pixpat)))

/* BitMap accessors
   NOTE: these take `BitMap *'s, not BitMap handles */
#define BITMAP_BOUNDS(bitmap) ((bitmap)->bounds)

#define BITMAP_FLAGS(bitmap) \
    (((bitmap)->rowBytes & ROWBYTES_FLAG_BITS))

#define BITMAP_DEFAULT_ROWBYTES (0)

#define BITMAP_ROWBYTES(bitmap) \
    ((bitmap)->rowBytes & ROWBYTES_VALUE_BITS)

#define BITMAP_SET_ROWBYTES(bitmap, value)                               \
    ((bitmap)->rowBytes = (((value) & ROWBYTES_VALUE_BITS) \
                             | BITMAP_FLAGS(bitmap)))

#define BITMAP_BASEADDR(bitmap) ((bitmap)->baseAddr)


#define BITMAP_P(bitmap) (!((bitmap)->rowBytes & PIXMAP_FLAG_BITS))

typedef BitMap blt_bitmap_t;

/* color table accessors */
/* number of bytes of storage needed for a color table whose max
   element (number of elements - 1) is `max_elt' */
#define CTAB_STORAGE_FOR_SIZE(max_elt) \
    (sizeof(ColorTable) + ((max_elt) * sizeof(ColorSpec)))

/* color table flags */
#define CTAB_GDEVICE_BIT (1 << 15)

#define CTAB_TABLE(ctab) ((*(ctab))->ctTable)

#define CTAB_SEED(ctab) ((*(ctab))->ctSeed)
#define CTAB_FLAGS(ctab) ((*(ctab))->ctFlags)
#define CTAB_SIZE(ctab) ((*(ctab))->ctSize)





/* flags for the value bits of device-type color
   tables */
#define CTAB_VALUE_ID_BITS 0xFF
#define CTAB_RESERVED_BIT (0x4000)
#define CTAB_PROTECTED_BIT (0x8000)
#define CTAB_TOLERANT_BIT (0x2000)
#define CTAB_PENDING_BIT (0x1000)

/* accessor macro to avoid byte swap. */
#define COLORSPEC_VALUE_LOW_BYTE(cspec) (((uint8_t *)&((cspec)->value))[1])

/* inverse color table accessors */
#define ITAB_TABLE(itab) ((*(itab))->iTTable)

#define ITAB_SEED(itab) ((*(itab))->iTabSeed)
#define ITAB_RES(itab) ((*(itab))->iTabRes)




/* graphics device accessors */
#define GD_RECT(gdhandle) ((*gdhandle)->gdRect)
#define GD_BOUNDS(gdhandle) PIXMAP_BOUNDS(GD_PMAP(gdhandle))

#define GD_REF_NUM(gdhandle) ((*gdhandle)->gdRefNum)
#define GD_ID(gdhandle) ((*gdhandle)->gdID)
#define GD_TYPE(gdhandle) ((*gdhandle)->gdType)
#define GD_ITABLE(gdhandle) ((*gdhandle)->gdITable)
#define GD_RES_PREF(gdhandle) ((*gdhandle)->gdResPref)
#define GD_SEARCH_PROC(gdhandle) ((*gdhandle)->gdSearchProc)
#define GD_COMP_PROC(gdhandle) ((*gdhandle)->gdCompProc)
#define GD_FLAGS(gdhandle) ((*gdhandle)->gdFlags)
#define GD_PMAP(gdhandle) ((*gdhandle)->gdPMap)
#define GD_REF_CON(gdhandle) ((*gdhandle)->gdRefCon)
#define GD_NEXT_GD(gdhandle) ((*gdhandle)->gdNextGD)
#define GD_MODE(gdhandle) ((*gdhandle)->gdMode)

#define GD_CCBYTES(gdhandle) ((*gdhandle)->gdCCBytes)
#define GD_CCDEPTH(gdhandle) ((*gdhandle)->gdCCDepth)
#define GD_CCXDATA(gdhandle) ((*gdhandle)->gdCCXData)
#define GD_CCXMASK(gdhandle) ((*gdhandle)->gdCCXMask)

#define GD_RESERVED(gdhandle) ((*gdhandle)->gdReserved)



















/* color icon accessors */
#define CICON_PMAP(cicon) ((*cicon)->iconPMap)
#define CICON_MASK(cicon) ((*cicon)->iconMask)
#define CICON_BMAP(cicon) ((*cicon)->iconBMap)
#define CICON_MASK_DATA(cicon) ((*cicon)->iconMaskData)

#define CICON_DATA(cicon) ((*cicon)->iconData)


inline bool CICON_P(Handle _icon)
{
    CIconHandle _cicon;                                                      
    uint32_t icon_size;                                                      
                                                                                 
    icon_size = GetHandleSize(_icon);                                        
    _cicon = (CIconHandle)_icon;                                             
                                                                                 
    return ((icon_size < sizeof(CIcon))                                             
            ? false                                                             
            : (icon_size == (sizeof(CIcon)                                      
                            - sizeof(int16_t)                                  
                            + (RECT_HEIGHT(&BITMAP_BOUNDS(&CICON_PMAP(_cicon)))
                                * (BITMAP_ROWBYTES(&CICON_BMAP(_cicon))         
                                + BITMAP_ROWBYTES(&CICON_MASK(_cicon))))))); 
}

/* palette accessors */
/* number of bytes of storage needed for a palette which
   contains `n_entries' entries */
#define PALETTE_STORAGE_FOR_ENTRIES(n_entries) \
    (sizeof(Palette) + ((n_entries - 1) * sizeof(ColorInfo)))

#define PALETTE_DATA_FIELDS(palette) ((*palette)->pmDataFields)
#define PALETTE_INFO(palette) ((*palette)->pmInfo)

#define PALETTE_ENTRIES(palette) ((*palette)->pmEntries)
#define PALETTE_WINDOW(palette) ((*palette)->pmWindow)
#define PALETTE_PRIVATE(palette) ((*palette)->pmPrivate)
#define PALETTE_DEVICES(palette) ((*palette)->pmDevices)
#define PALETTE_SEEDS(palette) ((*palette)->pmSeeds)







#define PALETTE_UPDATE_FLAG_BITS (0xE000)
#define PALETTE_UPDATE_FLAG_BITS (0xE000)

#define CINFO_RESERVED_INDEX_BIT (0x8000)
#define CINFO_RESERVED_INDEX_BIT (0x8000)

/* color cursor accessors */
#define CCRSR_1DATA(ccrsr) ((*ccrsr)->crsr1Data)
#define CCRSR_MASK(ccrsr) ((*ccrsr)->crsrMask)
#define CCRSR_HOT_SPOT(ccrsr) ((*ccrsr)->crsrHotSpot)

#define CCRSR_TYPE(ccrsr) ((*ccrsr)->crsrType)
#define CCRSR_MAP(ccrsr) ((*ccrsr)->crsrMap)
#define CCRSR_DATA(ccrsr) ((*ccrsr)->crsrData)
#define CCRSR_XDATA(ccrsr) ((*ccrsr)->crsrXData)
#define CCRSR_XVALID(ccrsr) ((*ccrsr)->crsrXValid)
#define CCRSR_XHANDLE(ccrsr) ((*ccrsr)->crsrXHandle)
#define CCRSR_XTABLE(ccrsr) ((*ccrsr)->crsrXTable)
#define CCRSR_ID(ccrsr) ((*ccrsr)->crsrID)










extern void cursor_reset_current_cursor(void);

#define IMV_XFER_MODE_P(mode) ((mode) >= blend && (mode) <= adMin)
#define active_screen_addr_p(bitmap) \
    ((bitmap)->baseAddr == PIXMAP_BASEADDR(GD_PMAP(LM(MainDevice))))

/* gd flags */
#define gdDevType 0
#define burstDevice 7
#define ext32Device 8
#define ramInit 10
#define mainScreen 11
#define allInit 12
#define screenDevice 13
#define noDriver 14
#define screenActive 15

/* gd modes */
#define GD_8BIT_INDIRECT_MODE 1

/* graphics device types */
#define clutType 0
#define fixedType 1
#define directType 2

/* utilities for saving and restoring the drawing state (traditional
   pen state, color, and text state */
typedef struct draw_state
{
    GUEST<PenState> pen_state;

    /* used only when the control owner is a cgrafport */
    GUEST<RGBColor> fg_color;
    GUEST<RGBColor> bk_color;

    GUEST<int32_t> fg;
    GUEST<int32_t> bk;

    /* text draw state */
    GUEST<Style> tx_face;
    GUEST<int16_t> tx_font;
    GUEST<int16_t> tx_mode;
    GUEST<int16_t> tx_size;
} draw_state_t;

extern void draw_state_save(draw_state_t *draw_state);
extern void draw_state_restore(draw_state_t *draw_state);

extern struct qd_color_elt
{
    RGBColor rgb;
    LONGINT value;
} ROMlib_QDColors[];

struct pixpat_res
{
    PixPat pixpat;
    PixMap patmap;
};

void pixmap_free_copy(PixMap *pm);
void pixmap_copy(const PixMap *src_pm, const Rect *src_rect,
                 PixMap *return_pm, Rect *return_rect);

extern int ROMlib_Cursor_color_p;
extern Cursor ROMlib_Cursor;
extern CCrsrHandle ROMlib_CCursor;

extern ColorSpec ctab_1bpp_values[];
extern ColorSpec ctab_2bpp_values[];
extern ColorSpec ctab_4bpp_values[];
extern ColorSpec ctab_8bpp_values[];

extern ColorSpec ROMlib_white_cspec;
extern ColorSpec ROMlib_black_cspec;
extern ColorSpec ROMlib_gray_cspec;
#define ROMlib_white_rgb_color (ROMlib_white_cspec.rgb)
#define ROMlib_black_rgb_color (ROMlib_black_cspec.rgb)
#define ROMlib_gray_rgb_color (ROMlib_gray_cspec.rgb)

extern Rect ROMlib_pattern_bounds;

extern CTabHandle ROMlib_dont_depthconv_ctab;
extern CTabHandle ROMlib_bw_ctab;
extern CTabHandle no_stdbits_color_conversion_color_table;
extern CTabHandle validate_relative_bw_ctab();
extern CTabHandle validate_fg_bk_ctab();

extern const int ROMlib_log2[];
extern const uint32_t ROMlib_pixel_tile_scale[];
extern const uint32_t ROMlib_pixel_size_mask[];

extern CTabHandle default_w_ctab;
extern AuxWinHandle default_aux_win;
extern GUEST<AuxWinHandle> *lookup_aux_win(WindowPtr w);

extern void ROMlib_color_init();

extern Rect ROMlib_cursor_rect;

extern void ROMlib_blt_rgn_update_dirty_rect(RgnHandle, int16_t, bool, int,
                                             const PixMap *, PixMap *,
                                             const Rect *, const Rect *,
                                             uint32_t, uint32_t);

extern void convert_pixmap(const PixMap *old_pixmap, PixMap *new_pixmap,
                           const Rect *rect, CTabPtr conv_table);

#define ROMlib_invalidate_conversion_tables()
/* extern void ROMlib_invalidate_conversion_tables (void); */

extern void scale_blt_bitmap(const blt_bitmap_t *src_bitmap,
                             blt_bitmap_t *dst_bitmap,
                             const Rect *old_rect, const Rect *new_rect,
                             int log2_bits_per_pixel);

void convert_pixmap_with_IMV_mode(const PixMap *src1, const PixMap *src2,
                                  PixMap *dst,
                                  CTabHandle src1_ctabh, CTabHandle src2_ctabh,
                                  ITabHandle itabh,
                                  const Rect *r1, const Rect *r2,
                                  int16_t mode, const RGBColor *op_color,
                                  bool tile_src1_p,
                                  int pat_x_offset, int pat_y_offset);

void convert_transparent(const PixMap *src1, const PixMap *src2,
                         PixMap *dst,
                         const Rect *r1, const Rect *r2,
                         int16_t mode,
                         bool tile_src1_p,
                         int pat_x_offset, int pat_y_offset);

extern void pm_front_window_maybe_changed_hook(void);
extern void pm_window_closed(WindowPtr w);

extern void ctl_color_init(void);

extern CTabHandle ROMlib_empty_ctab(int size);
extern void ROMlib_copy_ctab(CTabHandle src, CTabHandle dst);

extern PixPatHandle ROMlib_pattern_to_pixpat(const Pattern& p);

extern void ROMlib_blt_pn(RgnHandle, INTEGER);
extern void ROMlib_blt_fill(RgnHandle, INTEGER);
extern void ROMlib_blt_bk(RgnHandle, INTEGER);

extern void ROMlib_fill_pat(const Pattern&);
extern void ROMlib_fill_pixpat(PixPatHandle);

extern RGBColor *ROMlib_qd_color_to_rgb(LONGINT);

extern void gd_vdriver_mode_changed();
extern void gd_update_colors();
extern void ROMlib_InitGWorlds(void);
extern void ROMlib_InitGDevices(int width, int height, int bpp, bool grayscale);
extern void ResetToInitialDepth();

extern void pixmap_set_pixel_fields(PixMap *pixmap, int bpp);
extern const rgb_spec_t *pixmap_rgb_spec(const PixMap *pixmap);

extern void pixmap_black_white(const PixMap *pixmap,
                               uint32_t *black_return, uint32_t *white_return);
extern void gd_black_white(GDHandle gdh,
                           uint32_t *black_return, uint32_t *white_return);

extern rgb_spec_t mac_16bpp_rgb_spec, mac_32bpp_rgb_spec;

extern int average_color(GDHandle gdev,
                         RGBColor *c1, RGBColor *c2, int ratio,
                         RGBColor *out);

extern int16_t xStdTxMeas(int16_t n, uint8_t *p, GUEST<Point> *nump, GUEST<Point> *denp,
                          FontInfo *finfop, GUEST<int16_t> *charlocp);

extern void ROMlib_fg_bk(uint32_t *fg_pixel_out, uint32_t *bk_pixel_out,
                         RGBColor *fg_rgb_out, RGBColor *bk_rgb_out,
                         const rgb_spec_t *rgb_spec,
                         bool active_screen_addr_p,
                         bool src_blt_p);

extern void canonical_from_bogo_color(uint32_t index,
                                      const rgb_spec_t *rgb_spec,
                                      uint32_t *pixel_out,
                                      RGBColor *rgb_out);

#define AVERAGE_COLOR(c1, c2, ratio, out) \
    (average_color(LM(TheGDevice), (c1), (c2), (ratio), (out)))

#define DEFAULT_ITABLE_RESOLUTION 4

/* this probably belongs elsewhere as well */
class ThePortGuard
{
    GUEST<GrafPtr> savePort;

public:
    explicit ThePortGuard(GrafPtr port)
        : savePort(qdGlobals().thePort)
    {
        SetPort(port);
    }
    explicit ThePortGuard(GUEST<GrafPtr> port)
        : ThePortGuard(port.get())
    {
    }
    ThePortGuard()
        : savePort(qdGlobals().thePort)
    {
    }
    ThePortGuard(const ThePortGuard &) = delete;
    ~ThePortGuard()
    {
        qdGlobals().thePort = savePort;
    }
};

class TheGDeviceGuard
{
    GDHandle saveDevice;

public:
    explicit TheGDeviceGuard(GDHandle device)
        : saveDevice(LM(TheGDevice))
    {
        SetGDevice(device);
    }
    explicit TheGDeviceGuard(GUEST<GDHandle> device)
        : TheGDeviceGuard(device.get())
    {
    }
    ~TheGDeviceGuard()
    {
        SetGDevice(saveDevice);
    }
};

static_assert(sizeof(GrafVars) == 26);
}
#endif /* _CQUICK_H_ */
