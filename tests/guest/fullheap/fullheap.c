/* fullheap: what the Toolbox does when the application heap is full.
   Fills the heap with handles until not even 8 bytes are left, then calls
   routines that allocate (or that Executor allocates for), one line each:
   the result (nil or which heap it came from), MemErr, QDError. Written to
   Host:fullheap.txt as it goes (a crash leaves the lines before it). */
#include <stdio.h>
#include <string.h>
#include <Quickdraw.h>
#include <QDOffscreen.h>
#include <Fonts.h>
#include <Windows.h>
#include <Dialogs.h>
#include <TextEdit.h>
#include <Palettes.h>
#include <Memory.h>
#include <LowMem.h>

static FILE *f;
static char only[64];        /* Host:fullheap.only: run just this step */

static int want(const char *name)
{
    return !only[0] || !strcmp(only, name);
}
static Handle fill;          /* the filling handles, chained by their first long */

static const char *zone_of(void *h, int is_handle)
{
    THz z;
    if(is_handle < 0) return "ok";      /* a drawing call: it came back */
    if(!h) return "nil";
    z = is_handle ? HandleZone((Handle)h) : PtrZone((Ptr)h);
    if(z == ApplicationZone()) return "app";
    if(z == SystemZone()) return "sys";
    return "other";
}

static long lm0(void) { return *(volatile long *)0; }

static void line(const char *what, void *h, int is_handle, long before0)
{
    /* MemErr and QDError first: FreeMem and HandleZone set MemErr */
    short memerr = LMGetMemErr(), qderr = QDError();
    long changed = lm0() != before0;
    const char *zone = zone_of(h, is_handle);
    fprintf(f, "%s: %s memerr %d qderr %d lm0 %s free %ld\n", what, zone,
            memerr, qderr, changed ? "changed" : "same", FreeMem());
    fflush(f);
}

#define STEP(name, expr, is_handle) if(want(name)) do { long b0 = lm0(); void *r_; LMSetMemErr(0); r_ = (void *)(expr); line(name, r_, is_handle, b0); } while(0)
#define DRAW(name, stmt) if(want(name)) do { long b0 = lm0(); LMSetMemErr(0); stmt; line(name, (void *)1, -1, b0); } while(0)

static void fill_heap(void)
{
    long size;
    for(size = 65536; size >= 8; size /= 2)
        for(;;)
        {
            Handle h = NewHandle(size);
            if(!h)
                break;
            *(Handle *)*h = fill;
            fill = h;
        }
}

static void free_fill(long bytes)
{
    while(fill && bytes > 0)
    {
        Handle next = *(Handle *)*fill;
        bytes -= GetHandleSize(fill);
        DisposeHandle(fill);
        fill = next;
    }
}

int main(void)
{
    Rect wr = { 60, 60, 260, 360 }, r = { 10, 10, 110, 160 }, big = { 0, 0, 200, 300 };
    WindowPtr w;
    PixPatHandle pp;
    RgnHandle rgn, rgn2, upd;
    Handle items;
    PicHandle pic;
    int pass;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    MaxApplZone();

    {
        FILE *o = fopen("Host:fullheap.only", "r");
        if(o)
        {
            if(fgets(only, sizeof only, o))
                only[strcspn(only, "\r\n")] = 0;
            fclose(o);
        }
    }
    f = fopen("Host:fullheap.txt", "w");
    if(!f) f = fopen("fullheap.txt", "w");
    setvbuf(f, NULL, _IOFBF, 1024);
    w = NewCWindow(NULL, &wr, "\pFull heap", true, documentProc, (WindowPtr)-1, false, 0);
    SetPort(w);
    pp = NewPixPat();
    MakeRGBPat(pp, &(RGBColor){ 0x8000, 0x4000, 0xC000 });
    rgn = NewRgn();
    rgn2 = NewRgn();
    upd = NewRgn();
    OpenRgn();
    FrameOval(&r);
    FrameRect(&big);
    CloseRgn(rgn);
    items = NewHandleClear(2);
    *(short *)*items = -1;             /* no items */

    fprintf(f, "before fill: free %ld maxblock %ld\n", FreeMem(), MaxBlock());
    fflush(f);
    for(pass = 0; pass < 2; pass++)
    {
        fill_heap();
        fprintf(f, "pass %d full: free %ld maxblock %ld\n", pass, FreeMem(), MaxBlock());
        fflush(f);

        STEP("NewHandle(64)", NewHandle(64), 1);
        STEP("NewPtr(64)", NewPtr(64), 0);
        STEP("NewRgn", NewRgn(), 1);
        STEP("NewPixPat", NewPixPat(), 1);
        STEP("NewPalette(256)", NewPalette(256, NULL, pmTolerant, 0), 1);
        if(want("NewGWorld"))
        {
            Rect gr = { 0, 0, 64, 64 };
            GWorldPtr g = NULL;
            QDErr e = NewGWorld(&g, 8, &gr, NULL, NULL, 0);
            fprintf(f, "NewGWorld: err %d %s\n", e, g ? "made" : "nil");
            fflush(f);
        }
        DRAW("FrameOval", FrameOval(&r));
        DRAW("PaintOval", PaintOval(&r));
        DRAW("FrameRoundRect", FrameRoundRect(&r, 20, 20));
        DRAW("InvertRect", InvertRect(&r));
        DRAW("FillRect gray", FillRect(&r, &qd.gray));
        DRAW("FillCRect", FillCRect(&r, pp));
        DRAW("PenPixPat+PaintRect", (PenPixPat(pp), PaintRect(&r), PenNormal()));
        DRAW("ScrollRect", ScrollRect(&r, 5, 5, upd));
        DRAW("DrawString", (MoveTo(20, 150), DrawString("\pFull heap")));
        DRAW("CopyBits", CopyBits(&w->portBits, &w->portBits, &r, &big, srcCopy, NULL));
        DRAW("ClipRect+EraseRect", (ClipRect(&r), EraseRect(&big), ClipRect(&big)));
        pic = NULL;
        STEP("OpenPicture", (pic = OpenPicture(&r)), 1);
        if(pic)
        {
            FrameRect(&r);
            ClosePicture();
            DRAW("OpenPicture", DrawPicture(pic, &r));
        }
        /* region arithmetic last (InsetRgn left out: it hangs a 7.6.1
           Quadra when MaxBlock is under its buffer) */
        DRAW("FrameRgn", FrameRgn(rgn));
        DRAW("UnionRgn", UnionRgn(rgn, rgn2, rgn2));
        DRAW("XorRgn", XorRgn(rgn, rgn2, rgn2));

        /* last: a 7.6.1 Quadra stops at TENew with System error 25
           (dsMemFullErr) */
        STEP("NewDialog", NewDialog(NULL, &wr, "\p", false, dBoxProc, (WindowPtr)-1, false, 0, items), 0);
        STEP("TENew", TENew(&r, &r), 1);
        free_fill(0x7FFFFFFF);
    }
    fprintf(f, "done\n---\n");
    fclose(f);
    return 0;
}
