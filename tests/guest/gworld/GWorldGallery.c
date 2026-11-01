/* GWorldGallery: a window whose contents are all drawn offscreen and
   copied to it, as most programs draw (8.6's Finder, word processors):
   text in a GWorld copied with CopyBits; text copied through a second
   GWorld; text whose GWorld's pixels the program then writes itself
   (behind QuickDraw); text copied with CopyMask through a mask of its left
   part; text from GWorlds of other depths, and copied with srcOr and
   transparent (DrawDepths). A second window scrolls text three ways
   (DrawScroll); a third copies text to rectangles of other sizes
   (DrawScaled). For looking at Executor's HiDPI GWorld shadows (--executor-scale 2,
   docs/executor/HiDPI.md "Phase 4") and the ctest cell hidpi_gworld_executor
   (tests/test_hidpi_gworld.sh), which compares the 2x surface with the 1x
   frame there: keep its boxes in step with the layout here. Stays on the
   screen until it is told to quit. */
#include <Types.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <MacWindows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <AppleEvents.h>
#include <Memory.h>
#include <string.h>

static Boolean quit = false;

static pascal OSErr Quit(const AppleEvent *e, AppleEvent *reply, long refcon)
{
    quit = true;
    return noErr;
}

enum
{
    kWidth = 440,
};

/* A GWorld kWidth x HEIGHT (the screen's depth) with LINES drawn in it. */
struct line
{
    short font, size, face, h, v;
    const char *text;
};

static GWorldPtr OffscreenAt(short depth, short width, short height, const struct line *lines, int n)
{
    GWorldPtr gw;
    CGrafPtr port;
    GDHandle gd;
    Rect r;
    int i;
    SetRect(&r, 0, 0, width, height);
    if(NewGWorld(&gw, depth, &r, NULL, NULL, 0) != noErr)
        return NULL;
    GetGWorld(&port, &gd);
    SetGWorld(gw, NULL);
    LockPixels(GetGWorldPixMap(gw));
    EraseRect(&r);
    for(i = 0; i < n; i++)
    {
        TextFont(lines[i].font);
        TextSize(lines[i].size);
        TextFace(lines[i].face);
        MoveTo(lines[i].h, lines[i].v);
        DrawString((ConstStr255Param)lines[i].text);
    }
    TextFont(0);
    TextSize(0);
    TextFace(0);
    SetGWorld(port, gd);
    return gw;
}

static GWorldPtr Offscreen(short height, const struct line *lines, int n)
{
    return OffscreenAt(0, kWidth, height, lines, n);
}

static void CopyTo(GWorldPtr gw, WindowRef w, short top, short height)
{
    Rect s, d;
    SetRect(&s, 0, 0, kWidth, height);
    SetRect(&d, 0, top, kWidth, top + height);
    CopyBits((BitMap *)*GetGWorldPixMap(gw), &((GrafPtr)w)->portBits, &s, &d, srcCopy, NULL);
}

static const struct line main_lines[] = {
    { 3, 12, 0, 10, 20, "\pThe quick brown fox jumps over the lazy dog" },
    { 2, 18, bold, 10, 50, "\pGWorld shadows" },
    { 20, 14, italic, 10, 80, "\pTimes italic, offscreen" },
};
static const struct line twice_lines[] = {
    { 3, 12, 0, 10, 15, "\pCopied through a second GWorld" },
};
static const struct line stale_lines[] = {
    { 3, 12, 0, 10, 15, "\pWritten behind QuickDraw" },
};
static const struct line mask_lines[] = {
    { 3, 12, 0, 10, 15, "\pMasked copy: only the left part of this shows" },
};

static const struct line depth_lines[] = {
    { 3, 12, 0, 6, 15, "\pDepth 8, copied" },
    { 3, 12, 0, 6, 15, "\pDepth 16, copied" },
    { 3, 12, 0, 6, 15, "\pDepth 1, copied" },
};
static const struct line mode_lines[] = {
    { 3, 12, 0, 6, 15, "\psrcOr on yellow" },
    { 3, 12, 0, 6, 15, "\ptransparent on blue" },
};

/* 180-200: text in GWorlds of other depths copied to the window (8, 16,
   1 bits); 205-225: copied with srcOr onto light yellow, with transparent
   onto light blue */
static void DrawDepths(WindowRef w)
{
    static const short depths[] = { 8, 16, 1 };
    static const RGBColor yellow = { 0xFFFF, 0xFFFF, 0xC000 }, blue = { 0xD000, 0xE800, 0xFFFF };
    GWorldPtr g;
    Rect s, d;
    int i;
    for(i = 0; i < 3; i++)
        if((g = OffscreenAt(depths[i], 145, 20, &depth_lines[i], 1)))
        {
            SetRect(&s, 0, 0, 145, 20);
            SetRect(&d, i * 147, 180, i * 147 + 145, 200);
            CopyBits((BitMap *)*GetGWorldPixMap(g), &((GrafPtr)w)->portBits, &s, &d, srcCopy, NULL);
            DisposeGWorld(g);
        }
    for(i = 0; i < 2; i++)
        if((g = OffscreenAt(0, 215, 20, &mode_lines[i], 1)))
        {
            SetRect(&s, 0, 0, 215, 20);
            SetRect(&d, i * 225, 205, i * 225 + 215, 225);
            RGBForeColor(i ? &blue : &yellow);
            PaintRect(&d);
            ForeColor(blackColor);
            CopyBits((BitMap *)*GetGWorldPixMap(g), &((GrafPtr)w)->portBits, &s, &d, i ? transparent : srcOr, NULL);
            DisposeGWorld(g);
        }
}

static void DrawGallery(WindowRef w)
{
    GWorldPtr a, b, c, d, e;
    Rect r;
    SetPort((GrafPtr)w);
    ForeColor(blackColor);
    BackColor(whiteColor);

    /* 0-100: text drawn offscreen, copied */
    if((a = Offscreen(100, main_lines, 3)))
    {
        CopyTo(a, w, 0, 100);
        DisposeGWorld(a);
    }

    /* 105-125: through a second GWorld */
    b = Offscreen(20, twice_lines, 1);
    c = Offscreen(20, NULL, 0);
    if(b && c)
    {
        CGrafPtr port;
        GDHandle gd;
        SetRect(&r, 0, 0, kWidth, 20);
        GetGWorld(&port, &gd);
        SetGWorld(c, NULL);
        CopyBits((BitMap *)*GetGWorldPixMap(b), (BitMap *)*GetGWorldPixMap(c), &r, &r, srcCopy, NULL);
        SetGWorld(port, gd);
        CopyTo(c, w, 105, 20);
    }
    if(b)
        DisposeGWorld(b);
    if(c)
        DisposeGWorld(c);

    /* 130-150: the program inverts its GWorld's pixels itself */
    if((d = Offscreen(20, stale_lines, 1)))
    {
        PixMapHandle pm = GetGWorldPixMap(d);
        unsigned char *p = (unsigned char *)GetPixBaseAddr(pm);
        long rb = (**pm).rowBytes & 0x3FFF;
        long i;
        for(i = 0; i < rb * 20; i++)
            p[i] = ~p[i];
        CopyTo(d, w, 130, 20);
        DisposeGWorld(d);
    }

    /* 155-175: CopyMask through a mask of the left 200 pixels */
    if((e = Offscreen(20, mask_lines, 1)))
    {
        BitMap mask;
        Rect s, dr;
        int y, x;
        mask.rowBytes = (kWidth + 15) / 16 * 2;
        mask.baseAddr = NewPtrClear(mask.rowBytes * 20);
        SetRect(&mask.bounds, 0, 0, kWidth, 20);
        for(y = 0; y < 20; y++)
            for(x = 0; x < 200 / 8; x++)
                mask.baseAddr[y * mask.rowBytes + x] = 0xFF;
        SetRect(&s, 0, 0, kWidth, 20);
        SetRect(&dr, 0, 155, kWidth, 175);
        CopyMask((BitMap *)*GetGWorldPixMap(e), &mask, &((GrafPtr)w)->portBits, &s, &s, &dr);
        DisposePtr(mask.baseAddr);
        DisposeGWorld(e);
    }
    DrawDepths(w);
}

/* The scroll window: three columns of Geneva 12 lines 14 pixels apart
   (their boxes overlap: as AppleWorks' lines), each scrolled up twice by
   19 pixels (cutting through lines) after it is drawn.
   A: TextEdit, TEScroll (ScrollRect on the screen). B: a GWorld scrolled
   with ScrollRect in it, the lines it exposes drawn, copied. C: a tall
   GWorld copied, then scrolled as AppleWorks does: the window copied onto
   itself, the strip it exposes copied from the GWorld. */
enum
{
    kColumn = 145,
    kScrollHeight = 150,
    kLine = 14,
    kScroll = 19,
    kSteps = 2,
};

static TEHandle te;

static void Lines(int from, int to, short top, short left)
{
    int i;
    for(i = from; i <= to; i++)
    {
        Str255 s;
        NumToString(i, s);
        MoveTo(left + 6, top + i * kLine - 3);
        DrawString("\pLine ");
        DrawString(s);
        DrawString("\p of text");
    }
}

static void DrawScroll(WindowRef w)
{
    GWorldPtr b, c;
    CGrafPtr port;
    GDHandle gd;
    Rect r, s, d;
    int i;
    SetPort((GrafPtr)w);
    ForeColor(blackColor);
    BackColor(whiteColor);
    TextFont(3);
    TextSize(12);

    /* A */
    TEUpdate(&((GrafPtr)w)->portRect, te);
    for(i = 0; i < kSteps; i++)
        TEScroll(0, -kScroll, te);

    /* B */
    SetRect(&r, 0, 0, kColumn, kScrollHeight);
    if(NewGWorld(&b, 0, &r, NULL, NULL, 0) == noErr)
    {
        RgnHandle upd = NewRgn();
        GetGWorld(&port, &gd);
        SetGWorld(b, NULL);
        LockPixels(GetGWorldPixMap(b));
        TextFont(3);
        TextSize(12);
        EraseRect(&r);
        Lines(1, 11, 0, 0);
        for(i = 1; i <= kSteps; i++)
        {
            ScrollRect(&r, 0, -kScroll, upd);
            EraseRgn(upd);
            SetClip(upd);
            Lines(1, 14, -i * kScroll, 0);
            ClipRect(&r);
        }
        DisposeRgn(upd);
        SetGWorld(port, gd);
        SetRect(&d, kColumn + 2, 0, 2 * kColumn + 2, kScrollHeight);
        CopyBits((BitMap *)*GetGWorldPixMap(b), &((GrafPtr)w)->portBits, &r, &d, srcCopy, NULL);
        DisposeGWorld(b);
    }

    /* C */
    SetRect(&r, 0, 0, kColumn, 26 * kLine);
    if(NewGWorld(&c, 0, &r, NULL, NULL, 0) == noErr)
    {
        short left = 2 * kColumn + 4;
        GetGWorld(&port, &gd);
        SetGWorld(c, NULL);
        LockPixels(GetGWorldPixMap(c));
        TextFont(3);
        TextSize(12);
        EraseRect(&r);
        Lines(1, 26, 0, 0);
        SetGWorld(port, gd);
        SetRect(&s, 0, 0, kColumn, kScrollHeight);
        SetRect(&d, left, 0, left + kColumn, kScrollHeight);
        CopyBits((BitMap *)*GetGWorldPixMap(c), &((GrafPtr)w)->portBits, &s, &d, srcCopy, NULL);
        for(i = 1; i <= kSteps; i++)
        {
            SetRect(&s, left, kScroll, left + kColumn, kScrollHeight);
            SetRect(&d, left, 0, left + kColumn, kScrollHeight - kScroll);
            CopyBits(&((GrafPtr)w)->portBits, &((GrafPtr)w)->portBits, &s, &d, srcCopy, NULL);
            SetRect(&s, 0, kScrollHeight + (i - 1) * kScroll, kColumn, kScrollHeight + i * kScroll);
            SetRect(&d, left, kScrollHeight - kScroll, left + kColumn, kScrollHeight);
            CopyBits((BitMap *)*GetGWorldPixMap(c), &((GrafPtr)w)->portBits, &s, &d, srcCopy, NULL);
        }
        DisposeGWorld(c);
    }
}

/* The scale window: text in GWorlds copied to rectangles of other sizes */
struct scaled
{
    struct line l;
    short w, h;                         /* the GWorld */
    short top, left, bottom, right;     /* where it is copied */
};
static const struct scaled scaleds[] = {
    { { 3, 12, 0, 4, 13, "\pTwice" }, 70, 18, 0, 4, 36, 144 },          /* 2x */
    { { 3, 12, 0, 4, 13, "\pWide" }, 70, 18, 44, 4, 62, 144 },          /* 2x wide, 1x high */
    { { 2, 24, 0, 4, 26, "\pHalf" }, 140, 36, 70, 4, 88, 74 },          /* half */
    { { 3, 10, 0, 2, 11, "\pOne and a half" }, 96, 14, 96, 4, 117, 148 }, /* 1.5x */
};

static void DrawScaled(WindowRef w)
{
    int i;
    SetPort((GrafPtr)w);
    ForeColor(blackColor);
    BackColor(whiteColor);
    for(i = 0; i < (int)(sizeof scaleds / sizeof scaleds[0]); i++)
    {
        const struct scaled *c = &scaleds[i];
        GWorldPtr g = OffscreenAt(0, c->w, c->h, &c->l, 1);
        Rect s, d;
        if(!g)
            continue;
        SetRect(&s, 0, 0, c->w, c->h);
        SetRect(&d, c->left, c->top, c->right, c->bottom);
        CopyBits((BitMap *)*GetGWorldPixMap(g), &((GrafPtr)w)->portBits, &s, &d, srcCopy, NULL);
        DisposeGWorld(g);
    }
}

int main(void)
{
    WindowRef doc, scroll, scale;
    Rect r;
    EventRecord e;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    AEInstallEventHandler(kCoreEventClass, kAEQuitApplication, NewAEEventHandlerUPP(Quit), 0, false);

    SetRect(&r, 20, 60, 20 + kWidth, 300);
    doc = NewCWindow(NULL, &r, "\pGWorld Gallery", true, documentProc, (WindowRef)-1, true, 0);
    SetRect(&r, 478, 60, 630, 190);
    scale = NewCWindow(NULL, &r, "\pScale", true, documentProc, (WindowRef)-1, true, 0);
    SetRect(&r, 20, 322, 20 + kWidth, 322 + kScrollHeight);
    scroll = NewCWindow(NULL, &r, "\pScroll Gallery", true, documentProc, (WindowRef)-1, true, 0);
    SetPort((GrafPtr)scroll);
    TextFont(3);
    TextSize(12);
    {
        Rect v;
        Handle text = NewHandle(0);
        int i;
        SetRect(&v, 0, 0, kColumn, kScrollHeight);
        te = TENew(&v, &v);
        for(i = 1; i <= 20; i++)
        {
            Str255 n;
            NumToString(i, n);
            PtrAndHand("Line ", text, 5);
            PtrAndHand(n + 1, text, n[0]);
            PtrAndHand(" of text\r", text, 9);
        }
        HLock(text);
        TESetText(*text, GetHandleSize(text), te);
        DisposeHandle(text);
    }

    while(!quit)
    {
        if(!WaitNextEvent(everyEvent, &e, 30, NULL))
            continue;
        switch(e.what)
        {
            case updateEvt:
                BeginUpdate((WindowRef)e.message);
                SetPort((GrafPtr)e.message);
                EraseRect(&((GrafPtr)e.message)->portRect);
                if((WindowRef)e.message == doc)
                    DrawGallery(doc);
                else if((WindowRef)e.message == scale)
                    DrawScaled(scale);
                else if((WindowRef)e.message == scroll)
                {
                    /* TextEdit back at its first line, then scrolled */
                    TEScroll(0, (**te).viewRect.top - (**te).destRect.top, te);
                    EraseRect(&((GrafPtr)scroll)->portRect);
                    DrawScroll(scroll);
                }
                EndUpdate((WindowRef)e.message);
                break;
            case mouseDown:
            {
                WindowRef w;
                if(FindWindow(e.where, &w) == inDrag)
                    SelectWindow(w);
                break;
            }
            case kHighLevelEvent:
                AEProcessAppleEvent(&e);
                break;
        }
    }
    DisposeWindow(scale);
    DisposeWindow(scroll);
    DisposeWindow(doc);
    return 0;
}
