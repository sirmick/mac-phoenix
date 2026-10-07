/* Copyright 1986-1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <FontMgr.h>
#include <MemoryMgr.h>
#include <ToolboxUtil.h>

#include <quickdraw/cquick.h>
#include <quickdraw/picture.h>
#include <quickdraw/font.h>
#include <quickdraw/text.h>

#include <rsys/safe_alloca.h>
#include <algorithm>

using namespace Executor;

#undef ALLOCABEGIN

#if !defined(NDEBUG)
#define ALLOCABEGIN SAFE_DECL();
#else
#define ALLOCABEGIN
#endif

#undef ALLOCA
#define ALLOCA(n) SAFE_alloca(n)

namespace Executor
{
#include "ultable.ctable"
}

static void charblit(BitMap *fbmp, BitMap *tbmp, Rect *srect, Rect *drect,
                     INTEGER firsttime) /* INTERNAL */
{
    INTEGER firstfrom, lastfrom, firstto, lastto;
    INTEGER firstmaskend, lastmaskend, wordstodo, wordsfromdo, shiftsize;
    INTEGER numfromwords, numtowords;
    INTEGER i, j, numrows;
    GUEST<unsigned short> *firstfromword, *firsttoword, *nextfromword, *nexttoword;
    ULONGINT nextlong, firstmask, lastmask;
    INTEGER firsttomod16;

    if(srect->left == srect->right)
        /*-->*/ return;
    /* Find the first column of pixels to be copied. */

    firstfrom = srect->left - fbmp->bounds.left;

    /* Find the last column of pixels to be copied. */

    lastfrom = srect->right - fbmp->bounds.left;

    /* Find the first column pixels are to be copied to. */

    firstto = drect->left - tbmp->bounds.left;

    /* Find the last column pixels are to be copied to. */

    lastto = drect->right - tbmp->bounds.left;
    numrows = srect->bottom - srect->top;
    /* 
 * If it is to be put into italics, start with the characters
 * shifted over and shift them back one pixel at a time later.
 * Only do this once (the firsttime)
 */
    if((PORT_TX_FACE(qdGlobals().thePort) & (int)italic) && firsttime)
    {
        INTEGER temp;
        temp = (numrows) / 2;
        firstto += temp;
        lastto += temp;
    }
    /*
 * Find where in the bitmaps to copy from and to, shifted up one row
 * because the BLITROW macro increments the row number at the beginning.
 * I had a reason for this, but I don't currently remember it.
 */
    firstfromword = (GUEST<uint16_t> *)(fbmp->baseAddr + BITMAP_ROWBYTES(fbmp) * (srect->top - fbmp->bounds.top - 1)) + firstfrom / 16;

    firsttoword = (GUEST<uint16_t> *)(tbmp->baseAddr + BITMAP_ROWBYTES(tbmp) * (drect->top - tbmp->bounds.top - 1)) + firstto / 16;
    /*
 * Calculate the number of words to be taken from one bitmap and the
 * number to be put to the other minus one.  The minus one is because
 * the loop that needs wordstodo has an extra write at the end.
 * wordsfromdo is only used in comparisons.  They can be looked at
 * as the number of word boundaries crossed.
 */
    wordstodo = (lastto >> 4) - (firstto >> 4);
    wordsfromdo = (lastfrom >> 4) - (firstfrom >> 4);
    /*
 * Find the number of bits from the first and last words that are to
 * be used and make the appropriate mask.
 */
    lastmaskend = lastfrom & 0x000F;
    firstmaskend = firstfrom & 0x000F;
    firsttomod16 = firstto & 0x000F;
    /*
 * Calculate how far each bit needs to be right shifted so it comes out
 * correct in the destination bitmap.
 */
    shiftsize = firsttomod16 - firstmaskend;
    /*
 * If more bits need to be put in the first word written than are taken
 * from the first word read, two words should be read in and the amount
 * to be shifted should be adjusted accordingly.
 */
    if(shiftsize < 0)
    {
        shiftsize += 16;
        firstmask = (ULONGINT)0xFFFFFFFF >> firstmaskend;
    }
    else
    {
        /*
 * If there are enough bits, start reading one word earlier and adjust
 * the mask so the state will be the same as the other case.
 */
        firstfromword--;
        firstmask = 0x0000FFFF >> firstmaskend;
    }
    /* Calculate the number of bits to be used from the last from word.
 * TODO: elaborate
 */
    if((wordstodo > wordsfromdo) || ((lastto & 0x000F) < lastmaskend))
    {
        lastmask = 0xFFFFFFFF << (32 - lastmaskend);
    }
    else
    {
        lastmask = 0xFFFFFFFF << (16 - lastmaskend);
    }

    /* Variables used for speed. */

    numfromwords = BITMAP_ROWBYTES(fbmp) / 2;
    numtowords = BITMAP_ROWBYTES(tbmp) / 2;

#define BLITROW                                                                                 \
    {                                                                                           \
        /* Start reading and writing the next line */                                           \
        nextfromword = firstfromword += numfromwords;                                           \
        nexttoword = firsttoword += numtowords;                                                 \
        /* Read the first two words and mask off the unused bits */                             \
        nextlong = (((ULONGINT)*nextfromword << 16) + *(nextfromword + 1)) & firstmask; \
        nextfromword += 2;                                                                      \
        for(j = wordstodo; --j >= 0;)                                                           \
        {                                                                                       \
            /* Write the next word, calculated by taking the appropriate                        \
             * 16 bits from a LONGINT. */                                                       \
            *nexttoword++ |= nextlong >> shiftsize;                                         \
            /* Prepare the LONGINT for the next pass. */                                        \
            nextlong = (nextlong << 16) + *nextfromword++;                                  \
        }                                                                                       \
        /* Write the last word with the appropriate bits masked out. */                         \
        *nexttoword |= (nextlong & lastmask) >> shiftsize;                                  \
    }

/*
 * SHIFTROWONELEFT recalculates all the values BLITROW uses.  It
 * saves some time because not all the values change and some
 * change in a simple way.
 */

#define SHIFTROWONELEFT                                                       \
    {                                                                         \
        /* All the bits are going to shift one left */                        \
        firstto--;                                                            \
        lastto--;                                                             \
        /* The next three lines are the same as the main code. */             \
        firsttomod16 = firstto & 0x000F;                                      \
        wordstodo = (lastto >> 4) - (firstto >> 4);                           \
        shiftsize = firsttomod16 - firstmaskend;                              \
        /*                                                                    \
         * The calculation of firsttoword ends with + firstfrom / 16.         \
         * If firsttomod16 == 0xF after firstto has been decremented,         \
         * it needs to be corrected.                                          \
         */                                                                   \
        if(firsttomod16 == 0x000F)                                            \
            firsttoword--;                                                    \
        if(shiftsize < 0)                                                     \
        {                                                                     \
            /*                                                                \
             * The first time shiftsize goes negative is the only time        \
             * firstfromword should be corrected from the change made         \
             * in the other part of the if.                                   \
             */                                                               \
            if(shiftsize == -1)                                               \
                firstfromword++;                                              \
            /* From the main code */                                          \
            shiftsize += 16;                                                  \
            firstmask = (ULONGINT)0xFFFFFFFF >> firstmaskend;                 \
        }                                                                     \
        else                                                                  \
        {                                                                     \
            if(firsttomod16 == 0x000F)                                        \
            {                                                                 \
                /* The first time shiftsize >= 0 is when firsttomod16 changes \
                 * from 0 to 0xF.  This line corresponds to the line in the   \
                 * main code which only happens once.                         \
                 */                                                           \
                if(firstmaskend != 0)                                         \
                    firstfromword--;                                          \
            }                                                                 \
            /* From the main code */                                          \
            firstmask = 0x0000FFFF >> firstmaskend;                           \
        }                                                                     \
        if((wordstodo > wordsfromdo) || ((lastto & 0x000F) < lastmaskend))    \
            lastmask = 0xFFFFFFFF << (32 - lastmaskend);                      \
        else                                                                  \
            lastmask = 0xFFFFFFFF << (16 - lastmaskend);                      \
    }

    if((PORT_TX_FACE(qdGlobals().thePort) & (int)italic) && firsttime)
    {
        if(!((i = numrows) & 1))
        {
            BLITROW;
            SHIFTROWONELEFT;
        }
        while((i -= 2) >= 1)
        {
            BLITROW;
            SHIFTROWONELEFT;
            BLITROW;
        }
        BLITROW;
    }
    else
    {
        for(i = numrows; --i >= 0;)
        {
            BLITROW;
        }
    }
}

#define FIXEDONEHALF (1L << 15)
#define FIXED(n) ((LONGINT)(n) << 16)

/*
 * This is a mongrel routine.  Right now it's hand-crafted to do the work
 * of StdText and a xStdTxMeas, which is a helper routine that other
 * measurement routines call.
 */

LONGINT
Executor::text_helper(LONGINT n, Ptr textbufp, GUEST<Point> *nump, GUEST<Point> *denp,
                      FontInfo *finfop, GUEST<INTEGER> *charlocp, text_helper_action_t action)
{
    Point num, den;
    unsigned char *p;
    FMInput fmi;
    FMOutput *fmop;
    unsigned char *ep;
    INTEGER wid, offset;
    GUEST<INTEGER> *widp, *locp;
    unsigned out;
    INTEGER c;
    FontRec *fp;
    BitMap fmap;
    Rect srect, drect, misrect;
    unsigned char count;
    INTEGER extra;
    Fixed fixed_extra;
    INTEGER strwidth;
    BitMap stylemap, stylemap2, stylemap3, *bmp;
    int nbytes;
    INTEGER i;
    INTEGER carryforward;
    INTEGER first, max, misintwidth, kernmax, hOutput, vOutput;
    GUEST<Fixed> *widths;
    Fixed hOutputInverse, width, left, misfixwidth, spacewidth;
    Fixed left_begin;
    Fixed space_extra;
    INTEGER lineabove, linebelow, descent, leftmost;
    INTEGER rightitalicoffset, leftitalicoffset;
    INTEGER fmopstate, widthstate;
    LONGINT retval;
    ALLOCABEGIN
    PAUSEDECL;

    p = (unsigned char *)textbufp;
    num.h = nump->h;
    num.v = nump->v;
    den.h = denp->h;
    den.v = denp->v;
    retval = 0;
    if(action == text_helper_measure)
    {
        if(n < 0)
            n = 0;
    }
    else
    {
        if(n <= 0)
            /*-->*/ return retval;

        if(qdGlobals().thePort->picSave)
        {
            ROMlib_textpicupdate(num, den);
            PICOP(OP_LongText);
            PICWRITE(&PORT_PEN_LOC(qdGlobals().thePort), sizeof(PORT_PEN_LOC(qdGlobals().thePort)));
            count = n;
            PICWRITE(&count, sizeof(count));
            PICWRITE(p, count);
            if(!(count & 1))
                PICWRITE("", 1); /* even things out */
        }

        if(PORT_PEN_VIS(qdGlobals().thePort) < 0)
        {
            GUEST<Point> swapped_num;
            GUEST<Point> swapped_den;

            swapped_num.h = num.h;
            swapped_num.v = num.v;
            swapped_den.h = den.h;
            swapped_den.v = den.v;
            PORT_PEN_LOC(qdGlobals().thePort).h = PORT_PEN_LOC(qdGlobals().thePort).h
                                         + (CALLTXMEAS(n, textbufp,
                                                       &swapped_num,
                                                       &swapped_den, 0));
            /*-->*/ return retval;
        }
    }

    fmi.needBits = true;
    fmi.family = PORT_TX_FONT(qdGlobals().thePort);
    fmi.size = PORT_TX_SIZE(qdGlobals().thePort);
    fmi.face = PORT_TX_FACE(qdGlobals().thePort);
    fmi.device = PORT_DEVICE(qdGlobals().thePort);
    fmi.numer.h = num.h;
    fmi.numer.v = num.v;
    fmi.denom.h = den.h;
    fmi.denom.v = den.v;
    fmop = FMSwapFont(&fmi);

    if(action == text_helper_measure)
    {
        if(fmop->numer.h && fmop->denom.h)
            nump->h = (LONGINT)fmop->numer.h << 8 / fmop->denom.h;
        else
            nump->h = fmop->numer.h;

        if(fmop->numer.v && fmop->denom.h)
            nump->v = (LONGINT)fmop->numer.v << 8 / fmop->denom.h;
        else
            nump->v = fmop->numer.v;
        denp->h = WIDTHPTR->hFactor;
        denp->v = WIDTHPTR->hFactor;
        if(finfop)
        {
            finfop->ascent = (unsigned short)fmop->ascent;
            finfop->descent = (unsigned short)fmop->descent;
            finfop->widMax = (unsigned short)fmop->widMax;
            finfop->leading = (unsigned short)fmop->leading;
        }
    }

    extra = fmop->extra;
    fixed_extra = FIXED(extra);
    widthstate = HGetState((Handle)LM(WidthTabHandle));
    HLock((Handle)LM(WidthTabHandle));
    if((PORT_TX_FACE(qdGlobals().thePort) & (int)underline) && fmop->descent < 2)
        descent = 2;
    else
        descent = fmop->descent;

    PAUSERECORDING;
    fmopstate = HGetState((Handle)fmop->fontHandle);
    HLock(fmop->fontHandle);
    fp = (FontRec *)(*fmop->fontHandle);
    fmap.baseAddr = (Ptr)(&fp->rowWords + 1);
    fmap.rowBytes = fp->rowWords * 2;
    fmap.bounds.left = fmap.bounds.top = 0;
    fmap.bounds.right = fp->rowWords * 16;
    fmap.bounds.bottom = fp->fRectHeight;
    srect.top = misrect.top = 0;
    srect.bottom = misrect.bottom = fp->fRectHeight;
    widp = (GUEST<INTEGER> *)&(fp->owTLoc) + fp->owTLoc;
    locp = ((GUEST<INTEGER> *)&(fp->rowWords) + fp->rowWords * fp->fRectHeight
            + 1);

    // makes no sense and is unused
    // (widp points to big-endian, bitmask is native endian)
    //        INTEGER missing = *(widp + fp->lastChar - fp->firstChar + 1) & 0xff;
    misrect.left = *(locp + fp->lastChar - fp->firstChar + 1);
    misrect.right = *(locp + fp->lastChar - fp->firstChar + 2);
    drect.left = PORT_PEN_LOC(qdGlobals().thePort).h;
    drect.top = PORT_PEN_LOC(qdGlobals().thePort).v - fp->ascent;
    drect.bottom = drect.top + fp->fRectHeight;

    hOutput = WIDTHPTR->hOutput;
    vOutput = WIDTHPTR->vOutput;
    hOutputInverse = FixRatio(1 << 8, hOutput);

    space_extra = PORT_SP_EXTRA(qdGlobals().thePort);
    spacewidth = (WIDTHPTR->tabData[(unsigned)' '] + space_extra
                  - WIDTHPTR->sExtra);

    if(action == text_helper_draw)
    {
        GUEST<Point> swapped_num;
        GUEST<Point> swapped_den;

        swapped_num.h = num.h;
        swapped_num.v = num.v;
        swapped_den.h = den.h;
        swapped_den.v = den.v;

        strwidth = text_helper(n, textbufp, &swapped_num, &swapped_den, 0, 0,
                               text_helper_measure);
    }
#if !defined(LETGCCWAIL)
    else
    {
        strwidth = 0;
    }
#endif

    /* SPEEDUP:  make the stylemap be of the same alignment as
     qdGlobals().thePort.portBits when it matters to the blitter */

    rightitalicoffset = ((PORT_TX_FACE(qdGlobals().thePort) & (int)italic)
                             ? fmop->ascent / 2 - 1
                             : 0);
    leftitalicoffset = ((PORT_TX_FACE(qdGlobals().thePort) & (int)italic)
                            ? descent / 2 + 1
                            : 0);

    if(action == text_helper_draw)
    {
        if(strwidth <= 0)
            /*-->*/ return 0;

        stylemap.rowBytes = (strwidth - fp->kernMax + leftitalicoffset + rightitalicoffset + 31) / 32 * 4;
        stylemap.bounds.top = PORT_PEN_LOC(qdGlobals().thePort).v
                                 - fmop->ascent;
#if 0
      stylemap.bounds.bottom =  ( (PORT_PEN_LOC (qdGlobals().thePort).v)
				   + descent);
#else
        {
            int height;

            height = std::max<int>(fmop->ascent + descent, fp->fRectHeight);
            stylemap.bounds.bottom = PORT_PEN_LOC(qdGlobals().thePort).v
                                        - fmop->ascent + height;
        }
#endif
        stylemap.bounds.left = PORT_PEN_LOC(qdGlobals().thePort).h
                                  + fp->kernMax - leftitalicoffset;
        stylemap.bounds.right = PORT_PEN_LOC(qdGlobals().thePort).h
                                   + strwidth + rightitalicoffset;
        if(fmop->shadow)
            stylemap.bounds.left = stylemap.bounds.left - 1;
        nbytes = ((stylemap.bounds.bottom - stylemap.bounds.top) * stylemap.rowBytes);
        stylemap.baseAddr = (Ptr)ALLOCA(nbytes);
        memset(stylemap.baseAddr, 0, nbytes);
        bmp = &stylemap;
    }
#if !defined(LETGCCWAIL)
    else
    {
        bmp = 0;
        nbytes = 0;
    }
#endif
    /*
 * Note:  We aren't using any image height info
 */

    /*
 * TODO:  don't just check txFace & bold, you need to see whether it
 *	  has already been emboldened... (This is simple to fix)
 */

    first = fp->firstChar;
    max = fp->lastChar - fp->firstChar;
    misintwidth = misrect.right - misrect.left;
    misfixwidth = FIXED(misintwidth);
    left = FIXED(PORT_PEN_LOC(qdGlobals().thePort).h) + FIXEDONEHALF;
    left_begin = left;
    widths = WIDTHPTR->tabData;
    kernmax = fp->kernMax;
    leftmost = left >> 16;
    WIDTHPTR->tabData[(unsigned)' '] = spacewidth;
    WIDTHPTR->sExtra = space_extra;
    if(action == text_helper_draw)
        ASSERT_SAFE(stylemap.baseAddr);
    for(ep = p + n; p != ep; p++)
    {
        if(charlocp)
            *charlocp++ = (left - left_begin + 0xffff) >> 16;
        c = *p;
        width = widths[c];
        if((c -= fp->firstChar) < 0 || c > max
           || (wid = widp[c]) == -1)
        {
            drect.left = left >> 16;
            if(drect.left < leftmost)
                leftmost = drect.left;
            drect.right = drect.left + misintwidth;
            if(action == text_helper_draw)
            {
                charblit(&fmap, bmp, &misrect, &drect, true);
                ASSERT_SAFE(stylemap.baseAddr);
            }
            if(LM(FractEnable))
                left += width;
            else
                left += misfixwidth;
        }
        else
        {
            srect.left = locp[c];
            srect.right = locp[c + 1];
            drect.left = left >> 16;
            drect.left = drect.left + (offset = (wid >> 8) + kernmax);
            if(drect.left < leftmost)
                leftmost = drect.left;
            drect.right = drect.left + srect.right - srect.left;
            if(action == text_helper_draw)
            {
                charblit(&fmap, bmp, &srect, &drect, true);
                ASSERT_SAFE(stylemap.baseAddr);
            }
            if(LM(FractEnable))
                left += width;
            else
            {
                if(c == ' ' - fp->firstChar)
                    left += spacewidth;
                else
                    left += FIXED((wid & 0xFF) + extra);
            }
        }
    }
    if(charlocp)
        *charlocp = (left - left_begin + 0xffff) >> 16;
    if(action == text_helper_measure)
    {
        retval = (left - left_begin + 0xFFFF) >> 16;
    }
    else
    {
        ASSERT_SAFE(stylemap.baseAddr);
        stylemap.bounds.right = (left >> 16) + rightitalicoffset;
        if(PORT_TX_FACE(qdGlobals().thePort) & (int)bold)
        {
            stylemap2 = stylemap;
            stylemap2.baseAddr = (Ptr)ALLOCA(nbytes);
            ASSERT_SAFE(stylemap.baseAddr);
#if 0
	  BlockMoveData(stylemap.baseAddr, stylemap2.baseAddr, (Size) nbytes);
#else
            memcpy(stylemap2.baseAddr, stylemap.baseAddr, (Size)nbytes);
#endif
            srect = stylemap.bounds;
            drect = srect;

            ASSERT_SAFE(stylemap.baseAddr);
            for(i = 0; i++ < fmop->bold;)
            {
                drect.left = drect.left + 1;
                srect.right = srect.right - 1;
                charblit(&stylemap2, bmp, &srect, &drect, false);
                ASSERT_SAFE(stylemap.baseAddr);
            }
            ASSERT_SAFE(stylemap.baseAddr);
        }

        ASSERT_SAFE(stylemap.baseAddr);
        if(PORT_TX_FACE(qdGlobals().thePort) & (int)underline)
        {
            p = ((unsigned char *)bmp->baseAddr + BITMAP_ROWBYTES(bmp) * (fmop->ascent + 1));
            for(i = 0; i++ < fmop->ulThick;)
            {
                carryforward = 0;
                /*
	       * linebelow is zero if we shouldn't be peering into the following line
	       */
                lineabove = -BITMAP_ROWBYTES(bmp);
                linebelow = descent > 2 ? BITMAP_ROWBYTES(bmp) : 0;
                for(ep = p + BITMAP_ROWBYTES(bmp); p != ep;)
                {
                    c = *p | p[lineabove] | p[linebelow];
                    out = ultable[c];
                    if(c & 0x80)
                    {
                        if(!carryforward)
                            p[-1] &= 0xFE;
                    }
                    else if(carryforward)
                        out &= 0x7F;
                    carryforward = *p & 1;
                    *p = out | *p;
                    p++;
                }
            }
        }
        ASSERT_SAFE(stylemap.baseAddr);

        if(PORT_TX_FACE(qdGlobals().thePort) & (int)(outline | shadow))
        {
            stylemap2 = stylemap;
            stylemap3 = stylemap;
            stylemap2.baseAddr = stylemap.baseAddr;
            stylemap3.baseAddr = (Ptr)ALLOCA(nbytes);
#if 0
	  BlockMoveData(stylemap2.baseAddr, stylemap3.baseAddr,	(Size) nbytes);
#else
            memcpy(stylemap3.baseAddr, stylemap2.baseAddr, (Size)nbytes);
#endif
            stylemap.baseAddr = (Ptr)ALLOCA(nbytes);

            srect = stylemap.bounds;
            drect = srect;

            srect.left = srect.left + 1;
            drect.right = drect.right - 1;
            ASSERT_SAFE(stylemap.baseAddr);
            charblit(&stylemap2, &stylemap3, &srect, &drect, false);
            ASSERT_SAFE(stylemap.baseAddr);
            srect.left = srect.left - 1; /* restore */
            drect.right = drect.right + 1;
            for(i = 0; i++ < fmop->shadow;)
            {
                drect.left = drect.left + 1;
                srect.right = srect.right - 1;
                charblit(&stylemap2, &stylemap3, &srect, &drect, false);
            }
            ASSERT_SAFE(stylemap.baseAddr);
            drect.left = drect.left - fmop->shadow; /* restore */
            srect.right = srect.right + fmop->shadow;

#if 0
	  BlockMoveData(stylemap3.baseAddr, bmp->baseAddr, (Size) nbytes);
#else
            memcpy(bmp->baseAddr, stylemap3.baseAddr, (Size)nbytes);
#endif

            srect.top = srect.top + 1;
            drect.bottom = drect.bottom - 1;
            ASSERT_SAFE(stylemap.baseAddr);
            charblit(&stylemap3, bmp, &srect, &drect, false);
            srect.top = srect.top - 1; /* restore */
            drect.bottom = drect.bottom + 1;
            ASSERT_SAFE(stylemap.baseAddr);
            for(i = 0; i++ < fmop->shadow;)
            {
                drect.top = drect.top + 1;
                srect.bottom = srect.bottom - 1;
                charblit(&stylemap3, bmp, &srect, &drect, false);
            }
            drect.top = drect.top - fmop->shadow; /* restore */
            srect.bottom = srect.bottom + fmop->shadow;

            ASSERT_SAFE(stylemap3.baseAddr);
            ASSERT_SAFE(stylemap.baseAddr);
        }

        drect.top = bmp->bounds.top
                       - (FixMul((LONGINT)fmop->ascent << 16,
                                 (Fixed)(vOutput - 256) << 8)
                          >> 16);
        drect.left = leftmost;
        drect.bottom = drect.top
                          + (FixMul((LONGINT)(bmp->bounds.bottom
                                              - bmp->bounds.top)
                                        << 16,
                                    (Fixed)vOutput << 8)
                             >> 16);
        drect.right = leftmost + (FixMul((LONGINT)(bmp->bounds.right
                                                      - leftmost)
                                                << 16,
                                            (Fixed)hOutput << 8)
                                     >> 16);
        srect = bmp->bounds;
        srect.left = leftmost;
        ASSERT_SAFE(stylemap.baseAddr);
        StdBits(bmp, &srect, &drect, PORT_TX_MODE(qdGlobals().thePort) & 0x37, (RgnHandle)0);
        ASSERT_SAFE(stylemap.baseAddr);
        if(PORT_TX_FACE(qdGlobals().thePort) & (int)(outline | shadow))
        {
            ASSERT_SAFE(stylemap.baseAddr);
            StdBits(&stylemap2, &srect, &drect, srcXor, (RgnHandle)0);
            ASSERT_SAFE(stylemap.baseAddr);
            ASSERT_SAFE(stylemap2.baseAddr);
        }

        PORT_PEN_LOC(qdGlobals().thePort).h = drect.right - (FixMul((LONGINT)rightitalicoffset << 16,
                                                               (Fixed)hOutput << 8)
                                                        >> 16);
        ASSERT_SAFE(stylemap.baseAddr);
    }
    HSetState(fmop->fontHandle, fmopstate);
    HSetState((Handle)LM(WidthTabHandle), widthstate);
    RESUMERECORDING;
    ALLOCAEND
    return retval;
}

bool Executor::ROMlib_text_output_disabled_p;

bool
Executor::disable_text_printing(void)
{
    bool retval;

    retval = ROMlib_text_output_disabled_p;
    ROMlib_text_output_disabled_p = true;
    return retval;
}

void
Executor::set_text_printing(bool state)
{
    ROMlib_text_output_disabled_p = state;
}

void Executor::C_StdText(INTEGER n, Ptr textbufp, Point num, Point den)
{
    /*  if (!ROMlib_text_output_disabled_p) */
    {
        GUEST<Point> swapped_num;
        GUEST<Point> swapped_den;

        swapped_num.h = num.h;
        swapped_num.v = num.v;
        swapped_den.h = den.h;
        swapped_den.v = den.v;
        text_helper(n, textbufp, &swapped_num, &swapped_den, 0, 0,
                    text_helper_draw);
    }
}

#define FIXEDONEHALF (1L << 15)

INTEGER
Executor::xStdTxMeas(INTEGER n, Byte *p, GUEST<Point> *nump, GUEST<Point> *denp,
                     FontInfo *finfop, GUEST<INTEGER> *charlocp)
{
    INTEGER retval;

    retval = text_helper(n, (Ptr)p, nump, denp, finfop, charlocp,
                         text_helper_measure);
    return retval;
}

INTEGER Executor::C_StdTxMeas(INTEGER n, Ptr p, GUEST<Point> *nump,
                              GUEST<Point> *denp, FontInfo *finfop)
{
    return xStdTxMeas(n, (unsigned char *)p, nump, denp, finfop, nullptr);
}

void Executor::C_MeasureText(INTEGER n, Ptr text, Ptr chars) /* IMIV-25 */
{
    GUEST<Point> num, den;

    num.h = num.v = den.h = den.v = 1;
    xStdTxMeas(n, (unsigned char *)text, &num, &den,
               (FontInfo *)0, (GUEST<INTEGER> *)chars);
}
