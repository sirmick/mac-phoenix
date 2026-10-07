/* Copyright 1988 - 1999 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in SysErr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <EventMgr.h>
#include <ToolboxEvent.h>
#include <DialogMgr.h>
#include <OSUtil.h>
#include <SegmentLdr.h>
#include <SysErr.h>
#include <MemoryMgr.h>
#include <BinaryDecimal.h>
#include <LowMem.h>

#include <quickdraw/cquick.h>
#include <quickdraw/quick.h>
#include <rsys/segment.h>
#include <commandline/flags.h>
#include <rsys/version.h>
#include <osevent/osevent.h>
#include <prefs/options.h>
#include <error/syserr.h>

using namespace Executor;

static myalerttab_t myalerttab = {
    8,

#define WELCOME_CODE 0x28
    WELCOME_CODE, /* 1. normally "Welcome to M*cintosh", now copyright stuff */
    10,
    50,
    150,
    31,
    0,
    151,

    50, /* 2. primary text */
    56,
    { 108, 105 },
    "\251 Abacus Research and Development, Inc.  1986-1999",

    31, /* 3. ARDI icon */
    136,
    { 99, 56, 131, 88 },
    { { 0xff, 0xff, 0xff, 0xff },
      { 0x88, 0x88, 0x88, 0x89 },
      { 0x9d, 0xdd, 0xdd, 0xdd },
      { 0x88, 0x88, 0x88, 0x89 },
      { 0x9d, 0xdd, 0xdd, 0xdd },
      { 0x88, 0x88, 0x88, 0x89 },
      { 0xff, 0xff, 0xff, 0xff },
      { 0x88, 0x88, 0x88, 0x89 },
      { 0x88, 0x88, 0x88, 0x89 },
      { 0x88, 0x80, 0x80, 0x89 },
      { 0x88, 0xbc, 0xbc, 0x89 },
      { 0x80, 0xa2, 0x92, 0x81 },
      { 0x88, 0xa2, 0x92, 0x9d },
      { 0x94, 0xa2, 0x92, 0x89 },
      { 0xa2, 0xbc, 0x92, 0x89 },
      { 0xa2, 0xa8, 0x92, 0x89 },
      { 0xa2, 0xa4, 0x92, 0x89 },
      { 0xbe, 0xa2, 0x92, 0x89 },
      { 0xa2, 0xa2, 0x92, 0x89 },
      { 0xa2, 0xa2, 0x92, 0x89 },
      { 0xa2, 0xa2, 0xbc, 0x9d },
      { 0x80, 0x80, 0x80, 0x81 },
      { 0x88, 0x88, 0x88, 0x89 },
      { 0x88, 0x88, 0x88, 0x89 },
      { 0xff, 0xff, 0xff, 0xff } },

    150, /* 4. secondary text */
    50,
    { 122, 125 },
    "Please click on \"Info\" for more information\0\0",

    151, /* 5. the buttons */
    26,
    2,
    153,
    { 150, 50, 170, 100 }, /* OK rect */
    0,
    155,
    { 150, 130, 170, 190 }, /* Abort rect */
    156,

    153, /* 6. OK button */
    4,
    "OK\0",

    155, /* 7. Info button */
    6,
    "Info\0",

    156, /* 8. Info "procedure" */
    4,
    nullptr, /* was mydolicense, which we no longer use */
};

char syserr_msg[256];

static GUEST<INTEGER> *findid(INTEGER);
static void drawtextstring(INTEGER id, INTEGER offsetx, INTEGER offsety);
static void drawicon(INTEGER id, INTEGER offsetx, INTEGER offsety);
static void dobuttons(INTEGER id, INTEGER offsetx, INTEGER offsety);

static GUEST<INTEGER> *findid(INTEGER id)
{
    int i;
    GUEST<INTEGER> *ip;

    for(i = *(GUEST<INTEGER> *)LM(DSAlertTab),
    ip = (GUEST<INTEGER> *)LM(DSAlertTab) + 1;
        i > 0 && *ip != id;
        --i, ip = (GUEST<INTEGER> *)((char *)ip + ip[1] + 2 * sizeof(INTEGER)))
        ;
    return i > 0 ? ip : nullptr;
}

static void drawtextstring(INTEGER id, INTEGER offsetx, INTEGER offsety)
{
    struct tdef *tp;

    if(id && (tp = (struct tdef *)findid(id)))
    {
        MoveTo(tp->loc.h + offsetx, tp->loc.v + offsety);
        DrawText_c_string(tp->text);
    }
}

static void drawicon(INTEGER id, INTEGER offsetx, INTEGER offsety)
{
    struct idef *ip;
    BitMap bm;
    Rect old_loc;

    ip = (struct idef *)findid(id);
    if(id && ip)
    {
        bm.baseAddr = (Ptr)ip->ike;
        bm.rowBytes = 4;
        bm.bounds.left = bm.bounds.top = 0;
        bm.bounds.right = bm.bounds.bottom = 32;
        old_loc = ip->loc;
        C_OffsetRect(&ip->loc, offsetx, offsety);
        CopyBits(&bm, PORT_BITS_FOR_COPY(qdGlobals().thePort),
                 &bm.bounds, &ip->loc, srcCopy, nullptr);
        ip->loc = old_loc;
    }
}

static void dobuttons(INTEGER id, INTEGER offsetx, INTEGER offsety)
{
    struct bdef *bp;
    struct sdef *sp;
    struct pdef *pp;
    int i;
    EventRecord evt;
    int done;
    int tcnt, twid;
    Point p;
    char *textp;

    if((bp = (struct bdef *)findid(id)))
    {
        for(i = 0; i < bp->nbut; i++)
        {

            /* Offset buttons; this hack is to center the splash screen
	     * on non-512x342 root windows...yuck!
	     */

            C_OffsetRect(&bp->buts[i].butloc, offsetx, offsety);
            if((sp = (struct sdef *)findid(bp->buts[i].butstrid)))
            {
                textp = sp->text;
                tcnt = strlen(textp);
                twid = TextWidth((Ptr)textp, 0, tcnt);
                MoveTo((bp->buts[i].butloc.left + bp->buts[i].butloc.right - twid) / 2,
                       (bp->buts[i].butloc.top + bp->buts[i].butloc.bottom) / 2 + 4);
                DrawText((Ptr)textp, 0, tcnt);
            }
            if(!ROMlib_rect_buttons)
                FrameRoundRect(&bp->buts[i].butloc, 10, 10);
            else
                FrameRect(&bp->buts[i].butloc);
        }

        for(done = 0; !done;)
        {
            C_GetNextEvent(mDownMask | keyDownMask, &evt);
            if(evt.what == mouseDown || evt.what == keyDown)
            {
                p.h = evt.where.h;
                p.v = evt.where.v;
                for(i = 0; !done && i < bp->nbut; i++)
                {
                    if(PtInRect(p, &bp->buts[i].butloc) || ((evt.what == keyDown) && (((evt.message & charCodeMask) == '\r') || ((evt.message & charCodeMask) == NUMPAD_ENTER))))
                    {
                        if((pp = (struct pdef *)
                                findid(bp->buts[i].butprocid)))
                            /* NOTE:  we will have to do a better
				      job here sometime */
                            (*(void (*)(void))pp->proc)();
                        done = 1;
                    }
                }
                if(!done)
                    SysBeep(1);
            }
        }
        if(evt.what == mouseDown)
            while(!C_GetNextEvent(mUpMask, &evt))
                ;

        /* Move all buttons back. */
        for(i = 0; i < bp->nbut; i++)
            C_OffsetRect(&bp->buts[i].butloc, -offsetx, -offsety);
    }
}

/*
 * NOTE: The version of SysError below will only handle natively compiled
 *	 code.  When we want to be able to run arbitrary code we'll need
 *	 to call syn68k appropriately.
 */

void Executor::C_SysError(short errorcode)
{
    GrafPort alertport;
    Region viscliprgn;
    GUEST<RgnPtr> rp;
    Rect r;
    struct adef *ap;
    QDGlobals quickbytes;
    INTEGER offsetx, offsety;
    Rect main_gd_rect;

    LONGINT tmpa5;

    main_gd_rect = PIXMAP_BOUNDS(GD_PMAP(LM(MainDevice)));

    if(!LM(DSAlertTab))
    {
        INTEGER screen_width = main_gd_rect.right;
        INTEGER screen_height = main_gd_rect.bottom;

        LM(DSAlertTab) = (Ptr)&myalerttab;
        LM(DSAlertRect).top = (screen_height - 126) / 3;
        LM(DSAlertRect).left = (screen_width - 448) / 2;
        LM(DSAlertRect).bottom = LM(DSAlertRect).top + 126;
        LM(DSAlertRect).right = LM(DSAlertRect).left + 448;

        offsetx = LM(DSAlertRect).left - 32;
        offsety = LM(DSAlertRect).top - 64;
    }
    else
    {
        offsetx = offsety = 0;
    }

    /* IM-362 */
    /* 1. Save registers and Stack Pointer */
    /*	  NOT DONE YET... signal handlers sort of do that anyway */

    /* 2. Store errorcode in LM(DSErrCode) */
    LM(DSErrCode) = errorcode;

    /* 3. If no LM(DSAlertTab), bitch */
    if(!LM(DSAlertTab))
    {
        fputs("This machine thinks its a sadmac\n", stderr);
        exit(255);
    }

/* 4. Allocate and re-initialize QuickDraw */
    EM_A5 = US_TO_SYN68K(&tmpa5);
    LM(CurrentA5) = guest_cast<Ptr>(EM_A5);
    InitGraf(&quickbytes.thePort);
    ROMlib_initport(&alertport);
    SetPort(&alertport);
    InitCursor();
    rp = &viscliprgn;
    alertport.visRgn = alertport.clipRgn = &rp;
    viscliprgn.rgnSize = 10;
    viscliprgn.rgnBBox = main_gd_rect;

    /* 5, 6. Draw alert box if the errorcode is >= 0 */

    if(errorcode < 0)
        errorcode = -errorcode;
    else
    {
        r = LM(DSAlertRect);
        FillRect(&r, &qdGlobals().white);
#if defined(OLDSTYLEALERT)
        r.right = r.right - (2);
        r.bottom = r.bottom - (2);
        FrameRect(&r);
        PenSize(2, 2);
        MoveTo(r.left + 2, r.bottom);
        LineTo(r.right, r.bottom);
        LineTo(r.right, r.top + 2);
        PenSize(1, 1);
#else /* OLDSTYLEALERT */
        FrameRect(&r);
        InsetRect(&r, 3, 3);
        PenSize(2, 2);
        FrameRect(&r);
        PenSize(1, 1);
#endif /* OLDSTYLEALERT */
    }

    /* find appropriate entry */

    ap = (struct adef *)findid(errorcode);
    if(!ap)
        ap = (struct adef *)((INTEGER *)LM(DSAlertTab) + 1);

    /* 7. text strings */
    drawtextstring(ap->primetextid, offsetx, offsety);
    drawtextstring(ap->secondtextid, offsetx, offsety);

    /* 8. icon */
    drawicon(ap->iconid, offsetx, offsety);

    /* 9. TODO: figure out what to do with the proc ... */

    /* 10, 11, 12, 13. check for non-zero button id */
    /* #warning We blow off LM(ResumeProc) until we can properly handle it */
    if(ap->buttonid)
        dobuttons(/* LM(ResumeProc) ? ap->buttonid + 1 : */ ap->buttonid,
                  offsetx, offsety);
}
