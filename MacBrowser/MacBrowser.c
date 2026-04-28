/*
 * MacBrowser — guest-side app for the MacBrowser pipeline.
 *
 * 1. NewPtrClear(sizeof(BrowserShm)) out of the app heap (4 MiB SIZE).
 * 2. Stamp magic + version + framebuffer defaults.
 * 3. Write the buffer's Mac address as ASCII hex into
 *    Host:MacPhoenix:browser_shm.txt so mac-phoenix can find the buffer
 *    via Mac2HostAddr.
 * 4. Open a window with URL bar + pixel viewport + status strip.
 *    Polls fb.seq each tick and CopyBits the host's rendered Firefox
 *    pixels into the viewport. Pushes BR_CMD_* (NAV / CLICK / KEY /
 *    SCROLL / BACK / ...) into g2h on user input; drains BR_EV_*
 *    events out of h2g for status / page / download notifications.
 *
 * Cmd+L = focus URL bar; Return = navigate; Cmd+Q = quit.
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

/* Window layout. Toolbar buttons + URL field + a right-aligned
 * status label share one chrome row at the top (light-gray fill,
 * 1-px separator below). Viewport fills the rest with V + H scroll
 * bars at right + bottom. No dedicated status bar.
 *
 *   ┌──── title bar (window manager, ~18 px) ────────────────────┐
 *   ├ chrome   [Back][Forward][Reload][Stop] [https://... ] Ready│
 *   ├──── 1-px separator ────────────────────────────────────────│
 *   │ viewport (BrowserShm.fb pixels)                          │▲│
 *   │                                                          │ │
 *   │                                                          │▼│
 *   ├──◀────── horizontal scroll bar ───────▶───────────────┬───┤
 *   └────────────────────────────────────────────────────────────┘
 *
 * Visible viewport area shrinks to (kViewportW - kSBSize) ×
 * (kViewportH - kSBSize). Xvfb is sized to match exactly so
 * CopyBits stays 1:1 — no scaling, no clipping. Scroll bars are
 * cosmetic for now; M5+ will hook them up to BR_CMD_SCROLL.
 *
 * Total port height = 24 + 400 = 424; with title bar = 442. */
#define kToolbarH   24
#define kViewportW  640
#define kViewportH  400
#define kSBSize     16   /* scroll bar thickness, classic Mac 7.5 */
#define kStatusW    72   /* right-aligned status label area */
#define kWinH       (kToolbarH + kViewportH)   /* 424 */

/* Vertical position of each row's TOP edge, in window-port coords. */
#define kToolbarY   0
#define kViewportY  (kToolbarY  + kToolbarH)

/* Visible pixel area inside the viewport row (scroll bars consume
 * the right 16 cols and bottom 16 rows). The host pipeline writes
 * fb.pixels into a buffer of these dimensions. */
#define kPixelsW    (kViewportW - kSBSize)
#define kPixelsH    (kViewportH - kSBSize)

/* Toolbar buttons — text-only, period-correct (NN3 had a Pictures /
 * Text / Pictures+Text option). Four pushButProc controls. */
#define kBtnW        64
#define kBtnGap       4
#define kBtnLeftPad   8
#define kBtnH        20
#define kBtnY        ((kToolbarH - kBtnH) / 2)   /* vertical-center in row */

/* refCon values used to identify which button got clicked. */
#define kCtlBack     1
#define kCtlForward  2
#define kCtlReload   3
#define kCtlStop     4

static MenuHandle gAppleMenuH;
static MenuHandle gFileMenuH;
static WindowPtr  gWin = NULL;
static Boolean    gRunning = true;

/* URL bar TextEdit field. Active = caret blinks + keys go here. */
static TEHandle   gURL = NULL;
static Boolean    gURLActive = false;

/* Toolbar control handles. Tagged via the refCon so click dispatch
 * can find them in O(1). */
static ControlHandle gBtnBack    = NULL;
static ControlHandle gBtnForward = NULL;
static ControlHandle gBtnReload  = NULL;
static ControlHandle gBtnStop    = NULL;

/* Scroll bars on the viewport. Cosmetic for M5-B; M5+ wires them
 * to BR_CMD_SCROLL + a guest-side scroll offset. */
static ControlHandle gVSB = NULL;
static ControlHandle gHSB = NULL;

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

/* Write a freeform line into Host:MacPhoenix:MacBrowser.log for
 * host-side debugging. Uses ExtFS like BridgeAgent does. */
static void spike_log(const char *tag)
{
    FSSpec sp;
    short ref;
    if (FSMakeFSSpec(0, 0, "\pHost:MacPhoenix:MacBrowser.log", &sp) == fnfErr)
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
    /* Viewport sits below the toolbar + URL bar rows. */
    OffsetRect(&dst, 0, kViewportY);

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


/* URL field starts right after the last toolbar button and ends
 * before the right-aligned status label area. */
#define kURLLeft     (kBtnLeftPad + 4 * kBtnW + 3 * kBtnGap + kBtnGap)
#define kURLRight    (kViewportW - kStatusW - 6)

static void url_bar_rect(Rect *out)
{
    SetRect(out, kURLLeft, kBtnY, kURLRight, kBtnY + kBtnH);
}

/* Vertical + horizontal scroll bars on the viewport. M5-B is just
 * the visual; clicks/thumb-drag get hooked to BR_CMD_SCROLL later.
 * scrollBarProc = 16 in classic Mac control procIDs. */
static void make_scrollbars(void)
{
    if (!gWin) return;
    Rect r;
    /* min=max would render an inactive (blank) scroll bar. Setting
     * min=0 max=100 gives a normal-looking bar with a thumb at
     * value=0; M5+ wires real ranges from page dimensions. */
    /* Vertical: right edge, from kViewportY to bottom of viewport
     * minus the corner square. */
    SetRect(&r, kViewportW - kSBSize, kViewportY,
                kViewportW,             kViewportY + kViewportH - kSBSize);
    gVSB = NewControl(gWin, &r, "\p", true, 0, 0, 100, 16, 0);

    /* Horizontal: bottom edge of viewport, from 0 to corner. */
    SetRect(&r, 0,                       kViewportY + kViewportH - kSBSize,
                kViewportW - kSBSize,    kViewportY + kViewportH);
    gHSB = NewControl(gWin, &r, "\p", true, 0, 0, 100, 16, 0);
}

/* Allocate the four toolbar buttons. refCon = control ID so click
 * dispatch can identify which one. Visible+active by default. */
static void make_toolbar(void)
{
    if (!gWin) return;
    int x = kBtnLeftPad;
    Rect r;
    SetRect(&r, x, kBtnY, x + kBtnW, kBtnY + kBtnH);
    gBtnBack    = NewControl(gWin, &r, "\pBack",     true, 0,0,0,
                              0, (long)kCtlBack);     /* 0 = pushButProc */
    x += kBtnW + kBtnGap;
    SetRect(&r, x, kBtnY, x + kBtnW, kBtnY + kBtnH);
    gBtnForward = NewControl(gWin, &r, "\pForward",  true, 0,0,0,
                              0, (long)kCtlForward);  /* 0 = pushButProc */
    x += kBtnW + kBtnGap;
    SetRect(&r, x, kBtnY, x + kBtnW, kBtnY + kBtnH);
    gBtnReload  = NewControl(gWin, &r, "\pReload",   true, 0,0,0,
                              0, (long)kCtlReload);   /* 0 = pushButProc */
    x += kBtnW + kBtnGap;
    SetRect(&r, x, kBtnY, x + kBtnW, kBtnY + kBtnH);
    gBtnStop    = NewControl(gWin, &r, "\pStop",     true, 0,0,0,
                              0, (long)kCtlStop);     /* 0 = pushButProc */
}

static void open_window(void)
{
    /* Window y origin = 22 puts the title bar just below the 20 px
     * menu bar; total height (port + title bar) = 440 + 18 = 458,
     * so the window ends at y=480 exactly — full Mac screen used. */
    Rect bounds;
    SetRect(&bounds, 0, 40, kViewportW, 40 + kWinH);
    gWin = NewWindow(NULL, &bounds, "\pMacBrowser",
                     true, documentProc, (WindowPtr)-1, true, 0);

    SetPort(gWin);
    make_toolbar();
    make_scrollbars();

    /* TENew destRect == viewRect for simple single-line input. */
    Rect ur;
    url_bar_rect(&ur);
    InsetRect(&ur, 3, 2);   /* margin inside the framed field */
    gURL = TENew(&ur, &ur);
    if (gURL) {
        TEAutoView(true, gURL);
        /* Pre-fill with the initial URL Firefox is loading. */
        const char *seed = "https://example.com/";
        TESetText((Ptr)seed, (long)strlen(seed), gURL);
    }
}

static void draw_chrome_row(void)
{
    if (!gWin) return;
    GrafPtr saved;
    GetPort(&saved);
    SetPort(gWin);

    /* Fill the chrome row with stereotypical light Mac gray (ltGray
     * = 50%-density 8x8 dot pattern, the System 7 dialog backdrop
     * idiom). Keep BackPat=ltGray briefly for EraseRect to use it. */
    BackPat(&qd.ltGray);
    Rect row;
    SetRect(&row, 0, kToolbarY, kViewportW, kToolbarY + kToolbarH);
    EraseRect(&row);
    BackPat(&qd.white);

    /* Re-draw all controls (buttons + scroll bars) on top of the
     * fresh background. */
    DrawControls(gWin);

    /* URL text field: white background under the framed rect, then
     * the frame, then the TE contents on top. */
    Rect ur;
    url_bar_rect(&ur);
    EraseRect(&ur);          /* white (BackPat just reset) */
    FrameRect(&ur);
    if (gURL) {
        TextFont(4);   /* monaco — period-correct for URL display */
        TextSize(9);
        TEUpdate(&ur, gURL);
    }

    /* Right-aligned status label. Placeholder for M5-C; will read
     * BR_EV_STATUS strings out of h2g once that lands. */
    TextFont(applFont);   /* Geneva */
    TextSize(9);
    const unsigned char *label = (const unsigned char *)"\pReady";
    short tw = StringWidth(label);
    MoveTo(kViewportW - 6 - tw, kBtnY + kBtnH - 4);
    DrawString(label);

    /* 1-px separator between chrome and viewport. */
    PenNormal();
    MoveTo(0, kToolbarH - 1);
    LineTo(kViewportW - 1, kToolbarH - 1);

    SetPort(saved);
}

/* Push a 0-byte BR_CMD_BACK / FORWARD / RELOAD / STOP through g2h.
 * STOP isn't in the protocol header yet — host dispatcher rejects
 * unknown types harmlessly so we send it anyway as BR_CMD_STOP=14
 * for forward compatibility. */
#define BR_CMD_STOP   14
static void send_toolbar_cmd(uint16_t cmd_type)
{
    if (!gShm) return;
    if (br_ring_push(&gShm->g2h, cmd_type, NULL, 0) == 0) {
        br_log(&gShm->log, BR_LOG_INF,
               "toolbar cmd 0x%x", (unsigned)cmd_type);
    } else {
        br_log(&gShm->log, BR_LOG_ERR,
               "toolbar cmd 0x%x ring full", (unsigned)cmd_type);
    }
}

static void dispatch_button(ControlHandle ctl)
{
    long id = (**ctl).contrlRfCon;
    switch (id) {
    case kCtlBack:    send_toolbar_cmd(BR_CMD_BACK);    break;
    case kCtlForward: send_toolbar_cmd(BR_CMD_FORWARD); break;
    case kCtlReload:  send_toolbar_cmd(BR_CMD_RELOAD);  break;
    case kCtlStop:    send_toolbar_cmd(BR_CMD_STOP);    break;
    }
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
    AppendMenu(gAppleMenuH, "\pAbout MacBrowser\311");
    InsertMenu(gAppleMenuH, 0);

    gFileMenuH = NewMenu(kFileMenu, "\pFile");
    AppendMenu(gFileMenuH, "\pQuit/Q");
    InsertMenu(gFileMenuH, 0);

    DrawMenuBar();
}

static void do_about(void)
{
    ParamText("\pMacPhoenix MacBrowser\rModern web inside System 7.",
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

            /* 1. Toolbar button hit? */
            ControlHandle ctl = NULL;
            short part = FindControl(local, win, &ctl);
            if (part && ctl) {
                if (TrackControl(ctl, local, NULL) == part) {
                    dispatch_button(ctl);
                }
                break;
            }

            /* 2. URL bar hit? */
            Rect ur;
            url_bar_rect(&ur);
            if (PtInRect(local, &ur)) {
                set_url_active(true);
                if (gURL) TEClick(local, (evt->modifiers & shiftKey) != 0,
                                  gURL);
                break;
            }

            /* 3. Anything else: deactivate URL bar.
             * TODO M5 phase D: forward viewport clicks as BR_CMD_CLICK. */
            set_url_active(false);
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
            draw_chrome_row();
            blit_one_frame();
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
               "MacBrowser up; shm=0x%08lx size=%lu",
               (unsigned long)gShmAddr,
               (unsigned long)sizeof(BrowserShm));
    }

    EventRecord evt;
    while (gRunning) {
        if (WaitNextEvent(everyEvent, &evt, 1, NULL))
            handle_event(&evt);

        poll_shm();
        drain_h2g();
        maybe_push_g2h();

        /* TEIdle blinks the caret roughly every 30 ticks. Cheap. */
        if (gURL && gURLActive) TEIdle(gURL);
    }

    if (gURL)    { TEDispose(gURL);   gURL = NULL; }
    if (gShmPtr) { DisposePtr(gShmPtr); gShmPtr = NULL; gShm = NULL; }
    return 0;
}
