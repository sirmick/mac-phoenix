/* GWorldGallery: a window whose contents are all drawn offscreen and
   copied to it, as most programs draw (8.6's Finder, word processors):
   text in a GWorld copied with CopyBits; text copied through a second
   GWorld; text whose GWorld's pixels the program then writes itself
   (behind QuickDraw); text copied with CopyMask through a mask of its left
   part. For looking at Executor's HiDPI GWorld shadows (--executor-scale 2,
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

static GWorldPtr Offscreen(short height, const struct line *lines, int n)
{
    GWorldPtr gw;
    CGrafPtr port;
    GDHandle gd;
    Rect r;
    int i;
    SetRect(&r, 0, 0, kWidth, height);
    if(NewGWorld(&gw, 0, &r, NULL, NULL, 0) != noErr)
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
}

int main(void)
{
    WindowRef doc;
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
    DisposeWindow(doc);
    return 0;
}
