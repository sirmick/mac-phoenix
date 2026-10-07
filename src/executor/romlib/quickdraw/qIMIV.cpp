/* Copyright 1989, 1990, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>

#include <quickdraw/quick.h>
#include <quickdraw/cquick.h>
#include <quickdraw/stdbits.h>
#include <rsys/safe_alloca.h>
#include <mman/tempalloc.h>
#include <vdriver/dirtyrect.h>

#undef ALLOCABEGIN
#define ALLOCABEGIN SAFE_DECL();
#undef ALLOCA
#define ALLOCA(n) SAFE_alloca(n)

namespace Executor
{
#include "seedtables.ctable"
}

using namespace Executor;

#define DOLEFT 1
#define DORIGHT 2
#define DOMASK (DOLEFT | DORIGHT)

typedef struct
{
    unsigned char *p;
    unsigned char seed;
    unsigned char flags;
} stackentry;

typedef enum { Copy,
               Xor,
               Negate } transferop;

static void transfer(INTEGER *, INTEGER *, INTEGER, INTEGER, INTEGER, INTEGER,
                     transferop);
static void xSeedFill(unsigned char *, unsigned char *, INTEGER, INTEGER,
                      INTEGER, INTEGER, Boolean, INTEGER, INTEGER);

static void transfer(INTEGER *srcp, INTEGER *dstp, INTEGER srcr, INTEGER dstr,
                     INTEGER height, INTEGER widthw, transferop op)
{
    INTEGER sbump, dbump;
    INTEGER *ep0, *ep1;

    sbump = srcr / 2 - widthw;
    dbump = dstr / 2 - widthw;
    ep0 = dstp + (LONGINT)height * dstr / 2;
    switch(op)
    {
        case Copy:
            while(dstp != ep0)
            {
                for(ep1 = srcp + widthw; srcp != ep1; *dstp++ = *srcp++)
                    ;
                srcp += sbump;
                dstp += dbump;
            }
            break;
        case Xor:
            while(dstp != ep0)
            {
                for(ep1 = srcp + widthw; srcp != ep1; *dstp++ ^= *srcp++)
                    ;
                srcp += sbump;
                dstp += dbump;
            }
            break;
        case Negate:
            while(dstp != ep0)
            {
                for(ep1 = dstp + widthw; dstp != ep1; *dstp++ ^= ~0)
                    ;
                dstp += dbump;
            }
            break;
    }
}
#define LEFTBIT (0x80)
#define RIGHTBIT (1)

#define ADDTOSTACK(ptr, s, f) (sp->p = (ptr), sp->seed = (s), \
                               sp->flags = (f), sp++)

#define EXPAND(p, s) (tempuc = ~*(p), expandtable[binarytotrinary[tempuc] + binarytotrinary[tempuc & (s)]])

#define CHECKLEFT(lp, op)                                         \
    {                                                             \
        --cur;                                                    \
        if((expanded & LEFTBIT) && !atleft && !(*cur & RIGHTBIT)) \
        {                                                         \
            if(lp->p op dstr == cur)                              \
                lp->p = cur;                                      \
            else                                                  \
                lp = ADDTOSTACK(cur, RIGHTBIT, DOLEFT);           \
        }                                                         \
        ++cur;                                                    \
    }

#define CHECKRIGHT(lp, op)                                         \
    {                                                              \
        ++cur;                                                     \
        if((expanded & RIGHTBIT) && !atright && !(*cur & LEFTBIT)) \
        {                                                          \
            if(lp->p op dstr == cur)                               \
                lp->p = cur;                                       \
            else                                                   \
                lp = ADDTOSTACK(cur, LEFTBIT, DORIGHT);            \
        }                                                          \
        --cur;                                                     \
    }

static void xSeedFill(unsigned char *srcp, unsigned char *dstp, INTEGER srcr,
                      INTEGER dstr, INTEGER height, INTEGER width,
                      Boolean useseeds, INTEGER seedh, INTEGER seedv)
{
    unsigned char *cur, *savecur, expanded, saveexpanded, seed, *edstp;
    stackentry bogusentry, *topleftp, *toprightp, *bottomleftp, *bottomrightp,
        *stackp, stacke;
    Boolean atleft, atright;
    unsigned char tempuc;
    stackentry stack[4000], *sp;
    unsigned char *ecur;
    Rect temprect;
    LONGINT byteoff, voff;

    /* should be a sanity check of seedh and seedv here with just a zering
       of the destrect and early return if they are bad */

    transfer((INTEGER *)srcp, (INTEGER *)dstp, srcr, dstr, height, width,
             Copy);
    sp = stack;
    bogusentry.p = 0;
    edstp = dstp + (LONGINT)height * dstr;

    if(useseeds)
    {
        cur = dstp + (LONGINT)seedv * dstr + seedh / 8;
        seed = LEFTBIT >> (seedh % 8);
        ADDTOSTACK(cur, seed, DOLEFT | DORIGHT);
    }
    else
    {
        /* this is setting things up for CalcMask */
        for(cur = dstp, ecur = cur + width * 2; cur != ecur; cur++)
            if(*cur != 0xFF)
                ADDTOSTACK(cur, 0xFF, DOLEFT | DORIGHT);
        for(cur = dstp + (height - 1) * (LONGINT)dstr, ecur = cur + width * 2;
            cur != ecur; cur++)
            if(*cur != 0xFF)
                ADDTOSTACK(cur, 0xFF, DOLEFT | DORIGHT);
        bottomleftp = &bogusentry;
        for(cur = dstp; cur < edstp; cur += dstr)
        {
            if(!(*cur & LEFTBIT))
            {
                if(bottomleftp->p + dstr == cur)
                    bottomleftp->p = cur;
                else
                    bottomleftp = ADDTOSTACK(cur, LEFTBIT, DORIGHT);
            }
        }
        bottomrightp = &bogusentry;
        for(cur = dstp + width * 2 - 1; cur < edstp; cur += dstr)
        {
            if(!(*cur & RIGHTBIT))
            {
                if(bottomrightp->p + dstr == cur)
                    bottomrightp->p = cur;
                else
                    bottomrightp = ADDTOSTACK(cur, RIGHTBIT, DOLEFT);
            }
        }
    }
    while((stackp = --sp) >= stack)
    {
        stacke = *stackp;
        topleftp = toprightp = bottomleftp = bottomrightp = &bogusentry;
        cur = stacke.p;
        atleft = !((cur - dstp) % dstr);
        atright = (cur - dstp) % dstr == width * 2 - 1;

        saveexpanded = expanded = EXPAND(cur, stacke.seed);
        savecur = cur;
        cur[0] |= expanded;
        if(stacke.flags & DOLEFT)
            CHECKLEFT(topleftp, +); /* op doesn't really matter */
        bottomleftp = topleftp;
        if(stacke.flags & DORIGHT)
            CHECKRIGHT(toprightp, +); /* op doesn't really matter */
        bottomrightp = toprightp;

        /* go flying up the top */
        while((cur -= dstr, cur >= dstp) && (expanded = EXPAND(cur, expanded)))
        {
            cur[0] |= expanded;
            CHECKLEFT(topleftp, -);
            CHECKRIGHT(toprightp, -);
            if((cur[dstr] & expanded) != expanded)
                ADDTOSTACK(cur + dstr, expanded, DOLEFT | DORIGHT);
        }

        /* go flying down the bottom */
        cur = savecur;
        expanded = saveexpanded;
        while((cur += dstr, cur < edstp) && (expanded = EXPAND(cur, expanded)))
        {
            cur[0] |= expanded;
            CHECKLEFT(bottomleftp, +);
            CHECKRIGHT(bottomrightp, +);
            if((cur[-dstr] & expanded) != expanded)
                ADDTOSTACK(cur - dstr, expanded, DOLEFT | DORIGHT);
        }
    }
    transfer((INTEGER *)srcp, (INTEGER *)dstp, srcr, dstr, height, width,
             Xor);
    if(!useseeds)
        transfer((INTEGER *)0, (INTEGER *)dstp, 0, dstr, height,
                 width, Negate);
    if(dstp >= (unsigned char *)qdGlobals().screenBits.baseAddr)
    {
        byteoff = dstp - (unsigned char *)qdGlobals().screenBits.baseAddr;
        voff = byteoff / qdGlobals().screenBits.rowBytes;
        if(voff < qdGlobals().screenBits.bounds.bottom - qdGlobals().screenBits.bounds.top)
        {
            dirty_rect_accrue(qdGlobals().screenBits.bounds.top + voff,
                              (qdGlobals().screenBits.bounds.left
                               + (byteoff % qdGlobals().screenBits.rowBytes * 8L)),
                              temprect.top + height,
                              temprect.left + (LONGINT)width * 16);
        }
    }
}


void Executor::C_SeedFill(Ptr srcp, Ptr dstp, INTEGER srcr, INTEGER dstr,
                          INTEGER height, INTEGER width, INTEGER seedh,
                          INTEGER seedv) /* IMIV-24 */
{
    xSeedFill((uint8_t *)srcp, (uint8_t *)dstp,
              srcr, dstr,
              height, width, true, seedh, seedv);
}

void Executor::C_CalcMask(Ptr srcp, Ptr dstp, INTEGER srcr, INTEGER dstr,
                          INTEGER height, INTEGER width) /* IMIV-24 */
{
    xSeedFill((uint8_t *)srcp, (uint8_t *)dstp,
              srcr, dstr,
              height, width, false, 0, 0);
}

static void
copy_mask_1(BitMap *src_bm, BitMap *mask_bm, BitMap *dst_bm,
            const Rect *src_rect, const Rect *mask_rect, const Rect *dst_rect)
{
    RgnHandle mask_rgn;

    mask_rgn = NewRgn();
    if(BitMapToRegion(mask_rgn, mask_bm) == noErr)
    {
        GUEST<Handle> save_pic_handle;
        GUEST<QDProcsPtr> save_graf_procs;
        RgnHandle mask_rect_rgn;

        mask_rect_rgn = NewRgn();
        RectRgn(mask_rect_rgn, mask_rect);
        SectRgn(mask_rgn, mask_rect_rgn, mask_rgn);

        MapRgn(mask_rgn, mask_rect, dst_rect);

        save_pic_handle = PORT_PIC_SAVE(qdGlobals().thePort);
        save_graf_procs = PORT_GRAF_PROCS(qdGlobals().thePort);

        PORT_PIC_SAVE(qdGlobals().thePort) = nullptr;
        PORT_GRAF_PROCS(qdGlobals().thePort) = nullptr;

        CopyBits(src_bm, dst_bm, src_rect, dst_rect, srcCopy, mask_rgn);

        PORT_PIC_SAVE(qdGlobals().thePort) = save_pic_handle;
        PORT_GRAF_PROCS(qdGlobals().thePort) = save_graf_procs;

        DisposeRgn(mask_rect_rgn);
        DisposeRgn(mask_rgn);
    }
    else
    {
        Rect src_top, src_bottom, mask_top, mask_bottom, dst_top, dst_bottom;
        int16_t src_half, mask_half, dst_half;
        BitMap mask_top_bm, mask_bottom_bm;

        DisposeRgn(mask_rgn);

        src_half = RECT_HEIGHT(src_rect) / 2;
        mask_half = RECT_HEIGHT(mask_rect) / 2;
        dst_half = RECT_HEIGHT(dst_rect) / 2;

        src_top = src_bottom = *src_rect;
        mask_top = mask_bottom = *mask_rect;
        dst_top = dst_bottom = *dst_rect;

        src_bottom.top = src_top.bottom
            = src_top.bottom - src_half;
        mask_bottom.top = mask_top.bottom
            = mask_top.bottom - mask_half;
        dst_bottom.top = dst_top.bottom
            = dst_top.bottom - dst_half;

        mask_top_bm = *mask_bm;
        mask_bottom_bm = *mask_bm;
        mask_bottom_bm.bounds.top = mask_top_bm.bounds.bottom
            = mask_top_bm.bounds.bottom - mask_half;

        copy_mask_1(src_bm, &mask_top_bm, dst_bm,
                    &src_top, &mask_top, &dst_top);
        copy_mask_1(src_bm, &mask_bottom_bm, dst_bm,
                    &src_bottom, &mask_bottom, &dst_bottom);
    }
}

void Executor::C_CopyMask(BitMap *src_bogo_map, BitMap *mask_bogo_map,
                          BitMap *dst_bogo_map, const Rect *src_rect,
                          const Rect *mask_rect, const Rect *dst_rect) /* IMIV-24 */
{
    BitMap mask_bm;
    void *mask_bits;
    int row_bytes;
    TEMP_ALLOC_DECL(temp_mask_bits);

    if(ROMlib_text_output_disabled_p)
        /*-->*/ return;

    if(EmptyRect(mask_rect))
        return;

    row_bytes = (RECT_WIDTH(mask_rect) + 31) / 32 * 4;
    TEMP_ALLOC_ALLOCATE(mask_bits, temp_mask_bits,
                        row_bytes * RECT_HEIGHT(mask_rect));
    mask_bm.baseAddr = (Ptr)mask_bits;
    mask_bm.rowBytes = row_bytes;
    mask_bm.bounds = *mask_rect;

    CopyBits(mask_bogo_map, &mask_bm,
             mask_rect, mask_rect, srcCopy, nullptr);

    copy_mask_1(src_bogo_map, &mask_bm, dst_bogo_map,
                src_rect, mask_rect, dst_rect);

    TEMP_ALLOC_FREE(temp_mask_bits);
}

void Executor::C_CopyDeepMask(BitMap *srcBits, BitMap *maskBits,
                                   BitMap *dstBits, const Rect *srcRect,
                                   const Rect *maskRect, const Rect *dstRect, INTEGER mode,
                                   RgnHandle maskRgn)
{
    warning_unimplemented("poorly implemented");

    if(ROMlib_text_output_disabled_p)
        /*-->*/ return;

    C_CopyMask(srcBits, maskBits, dstBits, srcRect, maskRect, dstRect);
}

/* MeasureText is in qd/qStdText.c */

INTEGER *Executor::C_GetMaskTable() /* IMIV-25 */
{
    static unsigned char table[] __attribute__((aligned(2))) = {
        0x00, 0x00, 0x80, 0x00, 0xC0, 0x00, 0xE0, 0x00,
        0xF0, 0x00, 0xF8, 0x00, 0xFC, 0x00, 0xFE, 0x00,
        0xFF, 0x00, 0xFF, 0x80, 0xFF, 0xC0, 0xFF, 0xE0,
        0xFF, 0xF0, 0xFF, 0xF8, 0xFF, 0xFC, 0xFF, 0xFE,

        0xFF, 0xFF, 0x7F, 0xFF, 0x3F, 0xFF, 0x1F, 0xFF,
        0x0F, 0xFF, 0x07, 0xFF, 0x03, 0xFF, 0x01, 0xFF,
        0x00, 0xFF, 0x00, 0x7F, 0x00, 0x3F, 0x00, 0x1F,
        0x00, 0x0F, 0x00, 0x07, 0x00, 0x03, 0x00, 0x01,

        0x80, 0x00, 0x40, 0x00, 0x20, 0x00, 0x10, 0x00,
        0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x01, 0x00,
        0x00, 0x80, 0x00, 0x40, 0x00, 0x20, 0x00, 0x10,
        0x00, 0x08, 0x00, 0x04, 0x00, 0x02, 0x00, 0x01,
    };

    EM_A0 = US_TO_SYN68K(table);
    return (INTEGER *)table;
}
