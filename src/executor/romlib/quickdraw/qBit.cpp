/* Copyright 1986, 1988, 1989, 1990, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in QuickDraw.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <MemoryMgr.h>
#include <OSUtil.h>

#include <quickdraw/cquick.h>
#include <quickdraw/stdbits.h>
#include <quickdraw/picture.h>
#include <mman/mman.h>

using namespace Executor;

void Executor::C_CopyBits(BitMap *src_bitmap, BitMap *dst_bitmap,
                          const Rect *src_rect, const Rect *dst_rect,
                          INTEGER mode, RgnHandle mask)
{
    if(ROMlib_text_output_disabled_p)
        /*-->*/ return;

#define StdBits_TOOLTRAP_NUMBER (0xEB)

    // FIXME: #warning ctm hack below (mode = srcCopy) -- might not be best solution

    TheZoneGuard guard(LM(SysZone));
    int dst_is_theport_p;

    /* use `->portBits' instead of `PORT_BITS ()', because here we want
	  those actual bytes, not the logical structure that is the
	  portBits */
    dst_is_theport_p
        = (!memcmp(dst_bitmap, &qdGlobals().thePort->portBits, sizeof(BitMap))
           || (IS_PIXMAP_PTR_P(dst_bitmap)
               && CGrafPort_p(qdGlobals().thePort)
               && !memcmp(dst_bitmap,
                          *CPORT_PIXMAP(theCPort),
                          sizeof(BitMap))));
    if(dst_is_theport_p)
    {
        if(!CGrafPort_p(qdGlobals().thePort) && mode >= blend)
        {
            warning_unexpected("mode = %d", mode);
            mode = srcCopy;
        }

        CALLBITS(src_bitmap, src_rect, dst_rect, mode, mask);
    }
    else
    {
        GUEST<RgnHandle> savevisr;
        GUEST<RgnHandle> saveclipr;
        RgnHandle big_region;
        QDProcsPtr gp;

        /* save away qdGlobals().thePort fields */
        /* this saves the port bits (in the case of a GrafPort), and
	      the portPixMap in the case of a CGrafPort (among other
	      things */
        savevisr = PORT_VIS_REGION(qdGlobals().thePort);
        saveclipr = PORT_CLIP_REGION(qdGlobals().thePort);

        big_region = NewRgn();
        SetRectRgn(big_region, -32767, -32767, 32767, 32767);
        PORT_VIS_REGION(qdGlobals().thePort) = big_region;
        PORT_CLIP_REGION(qdGlobals().thePort) = big_region;

        /* give a warning of _StdBits or qdGlobals().thePort's bitsProc has
	      been patched out */
        gp = PORT_GRAF_PROCS(qdGlobals().thePort);
        if(gp
           && gp->bitsProc != &StdBits)
            warning_unexpected("qdGlobals().thePort bitsProc patched out!");

        if(StdBits.isPatched())
            warning_unexpected("_StdBits patched out!");

        ROMlib_bogo_stdbits(src_bitmap, dst_bitmap,
                            src_rect, dst_rect, mode, mask);

        /* restore fields */
        PORT_VIS_REGION(qdGlobals().thePort) = savevisr;
        PORT_CLIP_REGION(qdGlobals().thePort) = saveclipr;
        DisposeRgn(big_region);
    }

/* cruft */
#if 0
  if (is_port_bits_p || is_cport_pixmap_ptr_p)
    {
      CALLBITS (src_bitmap, src_rect, dst_rect, mode, mask);
    }
  else
    {
      BitMap savemap;
      Rect tmp_rect, saverect;
      RgnHandle savevisr, saveclipr, big_region;
      PixMapHandle bogo_port_pixmap;

      tmp_rect = *src_rect; /* Save now, before we mung portRect, since
			       src_rect could be an alias to portRect,
			       like it is in "SimuVent" */
            
      bogo_port_pixmap = nullptr;
      
      /* save away qdGlobals().thePort fields */
      /* this saves the port bits (in the case of a GrafPort), and
	 the portPixMap in the case of a CGrafPort (among other
	 things */
      savemap = qdGlobals().thePort->portBits;
      savevisr = PORT_VIS_REGION (qdGlobals().thePort);
      saveclipr = PORT_CLIP_REGION (qdGlobals().thePort);
      saverect = PORT_RECT (qdGlobals().thePort);
      
      if ((dst_bitmap->rowBytes & 1 << 15)
	  && ! (dst_bitmap->rowBytes & 1 << 14))
	{
	  GUEST<Handle> t;

	  PtrToHand ((Ptr) dst_bitmap, &t, sizeof (PixMap));
	  bogo_port_pixmap = (PixMapHandle) t;
	  CPORT_PIXMAP_X_NO_ASSERT (theCPort) = bogo_port_pixmap;
	  CPORT_VERSION_X_NO_ASSERT (theCPort) = CPORT_FLAG_BITS;
	}
      else
	qdGlobals().thePort->portBits = *dst_bitmap;
      
      big_region = NewRgn ();
      SetRectRgn (big_region, -32767, -32767, 32767, 32767);
      PORT_VIS_REGION (qdGlobals().thePort) = big_region;
      PORT_CLIP_REGION (qdGlobals().thePort) = big_region;
      SetRect (&PORT_RECT (qdGlobals().thePort), -32767, -32767, 32767, 32767);
      
      if (src_bitmap == &qdGlobals().thePort->portBits)
	src_bitmap = &savemap;
      
      CALLBITS (src_bitmap, &tmp_rect, dst_rect, mode, mask);
      
      /* restore fields */
      PORT_VIS_REGION (qdGlobals().thePort) = savevisr;
      PORT_CLIP_REGION (qdGlobals().thePort) = saveclipr;
      PORT_RECT (qdGlobals().thePort) = saverect;
      qdGlobals().thePort->portBits = savemap;
      if (bogo_port_pixmap)
	DisposeHandle ((Handle) bogo_port_pixmap);
      DisposeRgn (big_region);
    }
#endif
}

void Executor::C_ScrollRect(Rect *rp, INTEGER dh, INTEGER dv,
                            RgnHandle updatergn)
{
    Rect srcr, dstr;
    RgnHandle temp, temp2, updatergn2, srcregion;
    RGBColor bk_rgb, fg_rgb;
    GUEST<int32_t> bk_color, fg_color;
    int cgrafport_p;
    PAUSEDECL;

    PAUSERECORDING;

    cgrafport_p = CGrafPort_p(qdGlobals().thePort);
    if(cgrafport_p)
    {
        fg_rgb = CPORT_RGB_FG_COLOR(qdGlobals().thePort);
        bk_rgb = CPORT_RGB_BK_COLOR(qdGlobals().thePort);
    }
    fg_color = PORT_FG_COLOR(qdGlobals().thePort);
    bk_color = PORT_BK_COLOR(qdGlobals().thePort);

    RGBForeColor(&ROMlib_black_rgb_color);
    RGBBackColor(&ROMlib_white_rgb_color);

    SectRect(rp, &(*PORT_VIS_REGION(qdGlobals().thePort))->rgnBBox, rp);
    SectRect(rp, &(*PORT_CLIP_REGION(qdGlobals().thePort))->rgnBBox, rp);
    SectRect(rp, &PORT_RECT(qdGlobals().thePort), rp);
    SectRect(rp, &PORT_BOUNDS(qdGlobals().thePort), rp);
    srcregion = NewRgn();
    CopyRgn(PORT_VIS_REGION(qdGlobals().thePort), srcregion);
    SectRgn(PORT_CLIP_REGION(qdGlobals().thePort), srcregion, srcregion);
    OffsetRgn(srcregion, dh, dv);

    RectRgn(temp2 = NewRgn(), rp);
    DiffRgn(temp2, srcregion, temp2);

    srcr = *rp;
#if 0
  OffsetRect(&srcr, -PORT_BOUNDS (qdGlobals().thePort).left,
	     -PORT_BOUNDS (qdGlobals().thePort).top);   /* loc to glob */
#endif
    dstr = srcr;
    updatergn2 = NewRgn();
    RectRgn(updatergn2, &srcr);
    if(dh > 0)
    {
        dstr.left = dstr.left + dh;
        srcr.right = srcr.right - dh;
    }
    else
    {
        srcr.left = srcr.left - dh;
        dstr.right = dstr.right + dh;
    }
    if(dv > 0)
    {
        dstr.top = dstr.top + dv;
        srcr.bottom = srcr.bottom - dv;
    }
    else
    {
        srcr.top = srcr.top - dv;
        dstr.bottom = dstr.bottom + dv;
    }
    RectRgn(temp = NewRgn(), &dstr);
    DiffRgn(updatergn2, temp, updatergn2);
#if 0
  OffsetRgn(updatergn2,
	    PORT_BOUNDS (qdGlobals().thePort).left,
	    PORT_BOUNDS (qdGlobals().thePort).top); /* glob to loc */
#endif
    UnionRgn(updatergn2, temp2, updatergn2);

    /* I guess here we should clip to portRect, visRgn and clipRgn */
    CopyRgn(PORT_VIS_REGION(qdGlobals().thePort), temp);
    SectRgn(temp, PORT_CLIP_REGION(qdGlobals().thePort), temp);
    SectRgn(temp, srcregion, temp);

    CopyBits(PORT_BITS_FOR_COPY(qdGlobals().thePort), PORT_BITS_FOR_COPY(qdGlobals().thePort),
             &srcr, &dstr, srcCopy, temp);

    if(cgrafport_p)
    {
        CPORT_RGB_FG_COLOR(qdGlobals().thePort) = fg_rgb;
        CPORT_RGB_BK_COLOR(qdGlobals().thePort) = bk_rgb;
    }
    PORT_FG_COLOR(qdGlobals().thePort) = fg_color;
    PORT_BK_COLOR(qdGlobals().thePort) = bk_color;

    EraseRgn(updatergn2);
    if(updatergn)
        CopyRgn(updatergn2, updatergn);
    DisposeRgn(temp);
    DisposeRgn(temp2);
    DisposeRgn(srcregion);
    DisposeRgn(updatergn2);

    RESUMERECORDING;
}
