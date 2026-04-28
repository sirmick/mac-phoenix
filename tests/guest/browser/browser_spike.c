/*
 * BrowserSpike — M1 validation app.
 *
 * 1. NewPtrClear(sizeof(BrowserShm)) out of the app heap (4 MiB SIZE).
 * 2. Stamp magic + version + framebuffer defaults.
 * 3. Write the buffer's Mac address as ASCII hex into
 *    Host:MacPhoenix:browser_shm.txt so mac-phoenix can find the buffer
 *    via Mac2HostAddr.
 * 4. Open one window, poll fb.seq, CopyBits the gradient that the host
 *    writes into fb.pixels.
 *
 * Quit via File→Quit (Cmd+Q).
 */
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <Memory.h>
#include <OSUtils.h>
#include <ToolUtils.h>
#include <Resources.h>
#include <Files.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* Layout constants from the host header. The guest is native big-endian,
 * so all br_*_load/store macros are no-ops here. */
#include "MacBrowser.h"
#include "browser_shm.h"

#define kAppleMenu  128
#define kFileMenu   129

/* Window layout. URL bar at top, viewport in middle, status strip
 * at bottom. Heights chosen so the viewport is exactly 480 px
 * tall — matches the host pipeline's BrowserShm.fb.height. */
#define kURLBarH    24
#define kStatusH    16
#define kViewportW  640
#define kViewportH  480
#define kWinH       (kURLBarH + kViewportH + kStatusH)   /* 520 */

static MenuHandle gAppleMenuH;
static MenuHandle gFileMenuH;
static WindowPtr  gWin = NULL;
static Boolean    gRunning = true;

/* URL bar TextEdit field. Active = caret blinks + keys go here. */
static TEHandle   gURL = NULL;
static Boolean    gURLActive = false;

/* The BrowserShm buffer this app owns. Allocated once at startup. */
static Ptr            gShmPtr  = NULL;        /* raw heap pointer       */
static BrowserShm    *gShm     = NULL;        /* same, typed            */
static uint32_t       gShmAddr = 0;           /* Mac address of gShm    */
static uint32_t       gLastSeq = 0;
static uint32_t       gFramesShown = 0;
static uint32_t       gG2hPushed   = 0;       /* commands sent to host  */
static uint32_t       gH2gReceived = 0;       /* events from host       */
static uint16_t       gLastEvtType = 0;
static unsigned long  gLastG2hTicks = 0;

/* Off-screen 16-bit RGB555 PixMap header that wraps gShm->fb.pixels. */
static PixMap     gShmPixMap;
static CTabHandle gShmCTab = NULL;

/* Write a freeform line into Host:MacPhoenix:browser_spike for host-side
 * debugging. Uses ExtFS like BridgeAgent does. */
static void spike_log(const char *tag)
{
    FSSpec sp;
    short ref;
    if (FSMakeFSSpec(0, 0, "\pHost:MacPhoenix:browser_spike", &sp) == fnfErr)
        FSpCreate(&sp, 'ttxt', 'TEXT', 0);
    if (FSpOpenDF(&sp, fsWrPerm, &ref) != noErr) return;
    long len = (long)strlen(tag);
    SetEOF(ref, 0);
    FSWrite(ref, &len, tag);
    FSClose(ref);
    FlushVol(NULL, 0);
}

/* Allocate the BrowserShm buffer out of the app heap and stamp the
 * header. Returns true on success. */
static Boolean alloc_shm(void)
{
    gShmPtr = NewPtrClear((Size)sizeof(BrowserShm));
    if (!gShmPtr) {
        char buf[64];
        snprintf(buf, sizeof(buf), "alloc fail: NewPtrClear(%lu) err=%d",
                 (unsigned long)sizeof(BrowserShm), (int)MemError());
        spike_log(buf);
        return false;
    }
    gShm     = (BrowserShm *)gShmPtr;
    gShmAddr = (uint32_t)(uintptr_t)gShmPtr;

    /* All multi-byte stores are big-endian native here — the host is the
     * one that has to byte-swap on read/write. */
    gShm->magic   = BR_MAGIC;
    gShm->version = BR_VERSION;
    gShm->flags   = 0;
    gShm->fb.width  = 640;
    gShm->fb.height = 480;
    gShm->fb.depth  = 16;
    /* Rings already zeroed by NewPtrClear: read_idx == write_idx == 0
     * means "empty," which is the correct initial state. */

    return true;
}

/* Write the Mac address of the BrowserShm to
 * Host:MacPhoenix:browser_shm.txt as 8 hex chars + CR. The host poller
 * reads this file, parses the address, and translates Mac → host. */
static OSErr publish_handshake(void)
{
    FSSpec sp;
    OSErr err = FSMakeFSSpec(0, 0, "\pHost:MacPhoenix:browser_shm.txt", &sp);
    if (err == noErr) {
        FSpDelete(&sp);
    } else if (err != fnfErr) {
        return err;
    }
    err = FSpCreate(&sp, 'ttxt', 'TEXT', 0);
    if (err != noErr) return err;

    short ref;
    err = FSpOpenDF(&sp, fsWrPerm, &ref);
    if (err != noErr) return err;

    char line[16];
    int n = snprintf(line, sizeof(line), "%08lx\n", (unsigned long)gShmAddr);
    long len = (long)n;
    err = FSWrite(ref, &len, line);
    FSClose(ref);
    FlushVol(NULL, 0);
    return err;
}

static void init_shm_pixmap(uint16_t w, uint16_t h)
{
    gShmCTab = (CTabHandle)NewHandleClear(sizeof(ColorTable));
    if (!gShmCTab) return;
    HLock((Handle)gShmCTab);
    (**gShmCTab).ctSeed  = GetCTSeed();
    (**gShmCTab).ctFlags = 0;
    (**gShmCTab).ctSize  = 0;  /* direct-color: 1 entry, contents ignored */
    HUnlock((Handle)gShmCTab);

    memset(&gShmPixMap, 0, sizeof(gShmPixMap));
    gShmPixMap.baseAddr   = (Ptr)gShm->fb.pixels;
    gShmPixMap.rowBytes   = (short)((w * 2) | 0x8000);
    gShmPixMap.bounds.top    = 0;
    gShmPixMap.bounds.left   = 0;
    gShmPixMap.bounds.bottom = (short)h;
    gShmPixMap.bounds.right  = (short)w;
    gShmPixMap.pmVersion = 0;
    gShmPixMap.hRes      = 0x00480000;
    gShmPixMap.vRes      = 0x00480000;
    gShmPixMap.pixelType = RGBDirect;
    gShmPixMap.pixelSize = 16;
    gShmPixMap.cmpCount  = 3;
    gShmPixMap.cmpSize   = 5;
    gShmPixMap.pmTable   = gShmCTab;
}

static void blit_one_frame(void)
{
    if (!gWin || !gShm) return;
    GrafPtr saved;
    GetPort(&saved);
    SetPort(gWin);

    uint16_t w = gShm->fb.width;
    uint16_t h = gShm->fb.height;
    if (w == 0 || h == 0 || w > BR_FB_MAX_W || h > BR_FB_MAX_H) {
        SetPort(saved);
        return;
    }
    gShmPixMap.bounds.bottom = (short)h;
    gShmPixMap.bounds.right  = (short)w;
    gShmPixMap.rowBytes      = (short)((w * 2) | 0x8000);
    gShmPixMap.baseAddr      = (Ptr)gShm->fb.pixels;

    Rect src = gShmPixMap.bounds;
    Rect dst = src;
    /* Viewport sits below the URL bar in window-port coords. */
    OffsetRect(&dst, 0, kURLBarH);

    CopyBits((BitMap *)&gShmPixMap, &(gWin->portBits),
             &src, &dst, srcCopy, NULL);

    SetPort(saved);
    gFramesShown++;
}

static void poll_shm(void)
{
    if (!gShm) return;
    if (gShm->magic != BR_MAGIC) return;
    uint32_t seq = gShm->fb.seq;
    if (seq == gLastSeq) return;
    gLastSeq = seq;
    blit_one_frame();
}

/* Drain any host events queued in h2g. Cheap on every tick. */
static void drain_h2g(void)
{
    if (!gShm) return;
    uint8_t buf[256];
    uint16_t type = 0, len = 0;
    while (br_ring_pop(&gShm->h2g, &type, buf, sizeof(buf), &len) == 0) {
        gLastEvtType = type;
        gH2gReceived++;
    }
}

/* Push one BR_CMD_BACK roughly once per second. The payload is a
 * little 4-byte counter so the host log can verify framing + content. */
static void maybe_push_g2h(void)
{
    if (!gShm) return;
    unsigned long now = TickCount();
    if (now - gLastG2hTicks < 60) return;  /* ~1s */
    gLastG2hTicks = now;

    uint32_t payload = gG2hPushed;
    /* Already big-endian since the guest is native BE. */
    if (br_ring_push(&gShm->g2h, BR_CMD_BACK,
                     &payload, sizeof(payload)) == 0) {
        gG2hPushed++;
        br_log(&gShm->log, BR_LOG_DBG,
               "g2h tick %lu  h2g_seen=%lu  evt=0x%x",
               (unsigned long)gG2hPushed,
               (unsigned long)gH2gReceived,
               (unsigned)gLastEvtType);
    }
}

static void draw_status(void)
{
    if (!gWin) return;
    GrafPtr saved;
    GetPort(&saved);
    SetPort(gWin);

    Rect bar = gWin->portRect;
    bar.top = bar.bottom - 16;
    EraseRect(&bar);

    TextFont(3);
    TextSize(9);

    char buf[160];
    Str255 pbuf;
    snprintf(buf, sizeof(buf),
             "shm=0x%08lx %s  seq=%lu shown=%lu  g2h=%lu  h2g=%lu evt=0x%x",
             (unsigned long)gShmAddr,
             (gShm && gShm->magic == BR_MAGIC) ? "OK" : "MISS",
             (unsigned long)gLastSeq,
             (unsigned long)gFramesShown,
             (unsigned long)gG2hPushed,
             (unsigned long)gH2gReceived,
             (unsigned)gLastEvtType);
    pbuf[0] = (unsigned char)strlen(buf);
    memcpy(pbuf + 1, buf, pbuf[0]);
    MoveTo(8, bar.bottom - 4);
    DrawString(pbuf);

    SetPort(saved);
}

static void url_bar_rect(Rect *out)
{
    /* URL bar: full window width minus 8 px margin on each side,
     * inset 4 px from top, height = kURLBarH - 8 (room for the
     * frame). */
    SetRect(out, 8, 4, kViewportW - 8, kURLBarH - 4);
}

static void open_window(void)
{
    Rect bounds;
    SetRect(&bounds, 30, 60, 30 + kViewportW, 60 + kWinH);
    gWin = NewWindow(NULL, &bounds, "\pBrowserSpike",
                     true, documentProc, (WindowPtr)-1, true, 0);

    /* TENew destRect == viewRect for simple single-line input. */
    SetPort(gWin);
    Rect ur;
    url_bar_rect(&ur);
    InsetRect(&ur, 3, 3);   /* margin inside the framed URL bar */
    gURL = TENew(&ur, &ur);
    if (gURL) {
        TEAutoView(true, gURL);
        /* Pre-fill with the initial URL Firefox is loading; users can
         * select-all + retype to navigate. Real "fetch current URL
         * from BiDi" is M5 phase C. */
        const char *seed = "https://example.com/";
        TESetText((Ptr)seed, (long)strlen(seed), gURL);
    }
}

static void draw_url_bar(void)
{
    if (!gWin) return;
    GrafPtr saved;
    GetPort(&saved);
    SetPort(gWin);

    Rect ur;
    url_bar_rect(&ur);
    EraseRect(&ur);
    FrameRect(&ur);

    if (gURL) {
        TextFont(4);  /* monaco — period-correct for URL fields */
        TextSize(9);
        TEUpdate(&ur, gURL);
    }
    SetPort(saved);
}

/* Push a BR_CMD_NAV with the current URL bar text. The text comes
 * out of the TE handle, with Mac CR (\r, 0x0D) line terminators
 * still in it — strip a trailing CR so the URL is clean. */
static void send_url_nav(void)
{
    if (!gShm || !gURL) return;
    Handle h = (**gURL).hText;
    if (!h) return;
    long len = GetHandleSize(h);
    /* Strip trailing CR introduced by the Return key event. */
    while (len > 0 && (((unsigned char *)*h)[len - 1] == 0x0D ||
                       ((unsigned char *)*h)[len - 1] == 0x0A)) len--;
    if (len <= 0) return;

    HLock(h);
    int rc = br_ring_push(&gShm->g2h, BR_CMD_NAV, *h, (uint16_t)len);
    HUnlock(h);

    if (rc == 0) {
        br_log(&gShm->log, BR_LOG_INF,
               "BR_CMD_NAV pushed (%ld bytes)", (long)len);
    } else {
        br_log(&gShm->log, BR_LOG_ERR,
               "BR_CMD_NAV ring push failed rc=%d len=%ld", rc, (long)len);
    }
}

static void build_menus(void)
{
    gAppleMenuH = NewMenu(kAppleMenu, "\p\024");
    AppendMenu(gAppleMenuH, "\pAbout BrowserSpike\311");
    InsertMenu(gAppleMenuH, 0);

    gFileMenuH = NewMenu(kFileMenu, "\pFile");
    AppendMenu(gFileMenuH, "\pQuit/Q");
    InsertMenu(gFileMenuH, 0);

    DrawMenuBar();
}

static void do_about(void)
{
    ParamText("\pMacPhoenix BrowserSpike\rM1 host-pipe validator.",
              "\p", "\p", "\p");
    NoteAlert(128, NULL);
}

static void do_menu(long choice)
{
    short menuID = HiWord(choice);
    short item   = LoWord(choice);
    if (menuID == kAppleMenu) {
        if (item == 1) do_about();
    } else if (menuID == kFileMenu) {
        if (item == 1) gRunning = false;
    }
    HiliteMenu(0);
}

static void set_url_active(Boolean active)
{
    if (!gURL) return;
    if (active == gURLActive) return;
    SetPort(gWin);
    if (active) TEActivate(gURL); else TEDeactivate(gURL);
    gURLActive = active;
}

static void focus_url_bar(void)
{
    set_url_active(true);
    if (gURL) TESetSelect(0, 32767, gURL);  /* select all */
}

static void handle_event(EventRecord *evt)
{
    switch (evt->what) {
    case mouseDown: {
        WindowPtr win;
        short part = FindWindow(evt->where, &win);
        switch (part) {
        case inMenuBar: do_menu(MenuSelect(evt->where)); break;
        case inSysWindow: SystemClick(evt, win); break;
        case inDrag: {
            Rect b = (*GetGrayRgn())->rgnBBox;
            DragWindow(win, evt->where, &b);
            break;
        }
        case inContent: {
            if (win != FrontWindow()) { SelectWindow(win); break; }
            SetPort(win);
            Point local = evt->where;
            GlobalToLocal(&local);
            Rect ur;
            url_bar_rect(&ur);
            if (PtInRect(local, &ur)) {
                set_url_active(true);
                if (gURL) TEClick(local, (evt->modifiers & shiftKey) != 0,
                                  gURL);
            } else {
                set_url_active(false);
                /* TODO M5 phase D: forward click to viewport (page coords). */
            }
            break;
        }
        case inGoAway:
            if (TrackGoAway(win, evt->where)) gRunning = false;
            break;
        }
        break;
    }
    case keyDown:
    case autoKey: {
        char ch = evt->message & charCodeMask;
        if (evt->modifiers & cmdKey) {
            /* Cmd+L: focus URL bar (browser convention). */
            if (ch == 'l' || ch == 'L') { focus_url_bar(); break; }
            long choice = MenuKey(ch);
            if (HiWord(choice)) do_menu(choice);
            break;
        }
        if (gURLActive && gURL) {
            if (ch == 0x0D || ch == 0x03) {
                /* Return / Enter: ship the URL, deactivate. */
                send_url_nav();
                set_url_active(false);
            } else {
                TEKey(ch, gURL);
            }
        }
        break;
    }
    case activateEvt: {
        WindowPtr win = (WindowPtr)evt->message;
        if (win == gWin && gURL) {
            if ((evt->modifiers & activeFlag) && gURLActive) TEActivate(gURL);
            else TEDeactivate(gURL);
        }
        break;
    }
    case updateEvt: {
        WindowPtr win = (WindowPtr)evt->message;
        BeginUpdate(win);
        if (win == gWin) {
            blit_one_frame();
            draw_url_bar();
            draw_status();
        }
        EndUpdate(win);
        break;
    }
    }
}

int main(void)
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    MaxApplZone();

    build_menus();
    open_window();

    if (!alloc_shm()) {
        spike_log("alloc_shm failed — bailing");
        /* Fall through so the user sees the empty window with status. */
    } else {
        char tag[80];
        snprintf(tag, sizeof(tag),
                 "shm allocated at 0x%08lx (size=%lu)",
                 (unsigned long)gShmAddr, (unsigned long)sizeof(BrowserShm));
        spike_log(tag);

        OSErr ph = publish_handshake();
        snprintf(tag, sizeof(tag), "publish_handshake err=%d", (int)ph);
        spike_log(tag);

        init_shm_pixmap(640, 480);

        /* Now that BrowserShm is published and the host has resolved
         * the pointer, log a startup line through the new debug channel
         * so we can see end-to-end flow. */
        br_log(&gShm->log, BR_LOG_INF,
               "BrowserSpike up; shm=0x%08lx size=%lu",
               (unsigned long)gShmAddr,
               (unsigned long)sizeof(BrowserShm));
    }

    unsigned long last_status = TickCount();

    EventRecord evt;
    while (gRunning) {
        if (WaitNextEvent(everyEvent, &evt, 1, NULL))
            handle_event(&evt);

        poll_shm();
        drain_h2g();
        maybe_push_g2h();

        /* TEIdle blinks the caret roughly every 30 ticks. Cheap. */
        if (gURL && gURLActive) TEIdle(gURL);

        unsigned long now = TickCount();
        if (now - last_status >= 60) {
            last_status = now;
            draw_status();
        }
    }

    if (gURL)    { TEDispose(gURL);   gURL = NULL; }
    if (gShmPtr) { DisposePtr(gShmPtr); gShmPtr = NULL; gShm = NULL; }
    return 0;
}
