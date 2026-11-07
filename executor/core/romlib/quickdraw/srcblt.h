#if !defined(_SRCBLT_H_)
#define _SRCBLT_H_

#include <QuickDraw.h>
#include <quickdraw/cquick.h>

extern "C" {



extern void srcblt_rgn(Executor::RgnHandle rh, int mode, int log2_bpp,
                       const Executor::blt_bitmap_t *src, const Executor::blt_bitmap_t *dst,
                       Executor::GUEST<Executor::Point> *src_origin, Executor::GUEST<Executor::Point> *dst_origin,
                       uint32_t fg_color, uint32_t bk_color);

extern void srcblt_bitmap(void);

extern char srcblt_nop;

extern const void **srcblt_noshift_stubs[8];
extern const void **srcblt_noshift_fgbk_stubs[8];
extern const void **srcblt_shift_stubs[8];
extern const void **srcblt_shift_fgbk_stubs[8];


extern int srcblt_log2_bpp;

extern const Executor::INTEGER *srcblt_rgn_start;

extern const void **srcblt_stub_table;

extern int32_t srcblt_x_offset;

extern int32_t srcblt_src_row_bytes;
extern int32_t srcblt_dst_row_bytes;

extern uint32_t srcblt_fg_color;
extern uint32_t srcblt_bk_color;

extern char *srcblt_src_baseaddr;
extern char *srcblt_dst_baseaddr;

extern int srcblt_shift_offset;

extern bool srcblt_reverse_scanlines_p;
}
#endif /* !_SRCBLT_H_ */
