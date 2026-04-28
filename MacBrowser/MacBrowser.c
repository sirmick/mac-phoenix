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
/* Single padding constant. Used between every pair of adjacent
 * controls, between the controls and the top/bottom of the chrome
 * bar, and between the leftmost / rightmost button and the window
 * edge. Keeps spacing consistent everywhere. */
#define kPad         4

#define kViewportW  640
#define kViewportH  400
#define kSBSize     16   /* scroll bar thickness, classic Mac 7.5 */
#define kBtnH       20   /* main toolbar button body height */
#define kToolbarH   (kBtnH + 2 * kPad)   /* 28 = 4 + 20 + 4 */
#define kWinH       (kToolbarH + kViewportH)

/* Vertical position of each row's TOP edge, in window-port coords. */
#define kToolbarY   0
#define kViewportY  (kToolbarY  + kToolbarH)

/* Visible pixel area inside the viewport row (scroll bars consume
 * the right 16 cols and bottom 16 rows). The host pipeline writes
 * fb.pixels into a buffer of these dimensions. */
#define kPixelsW    (kViewportW - kSBSize)
#define kPixelsH    (kViewportH - kSBSize)

/* Toolbar buttons — text-only, period-correct (NN3 had a Pictures /
 * Text / Pictures+Text option). Four pushButProc controls on the
 * left, two small zoom controls right-justified. All gaps + edge
 * margins use kPad. */
#define kBtnW         64
#define kBtnY         kPad

#define kSmallBtnW    22
#define kSmallBtnH    kBtnH      /* same height as the main buttons */
#define kSmallBtnY    kBtnY

/* +/- buttons (right-justified, [-] left of [+]). */
#define kBtnPlusX    (kViewportW - kPad - kSmallBtnW)
#define kBtnMinusX   (kBtnPlusX - kPad - kSmallBtnW)

/* "Ready" status label area, between URL bar and the +/- buttons. */
#define kStatusW      48
#define kStatusRight (kBtnMinusX - kPad)
#define kStatusLeft  (kStatusRight - kStatusW)
/* Status baseline — slightly below center, sits closer to the
 * baseline of the button labels. */
#define kStatusBaseY  ((kToolbarH + 9) / 2 - 2)

/* refCon values used to identify which button got clicked. */
#define kCtlBack     1
#define kCtlForward  2
#define kCtlReload   3
#define kCtlStop     4
#define kCtlMinus    5
#define kCtlPlus     6

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
static ControlHandle gBtnMinus   = NULL;
static ControlHandle gBtnPlus    = NULL;

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

/* Mirrored from the most recent BR_EV_STATUS we drained out of h2g.
 * gStatusCode is BR_STATUS_*; gStatusDirty signals the main loop to
 * redraw + (optionally) update the URL bar with the new URL. */
static uint8_t        gStatusCode = BR_STATUS_READY;
static unsigned char  gStatusURL[252];   /* Pascal-string scratch */
static Boolean        gStatusDirty = false;

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

static void draw_chrome_row(void);

/* Apply the latest BR_EV_STATUS to the UI: refresh the URL bar with
 * the newly-committed URL (unless the user is mid-edit), then
 * redraw the chrome row so the status label picks up the new code. */
static void apply_status(void)
{
    if (!gWin) return;

    /* Update URL bar text (only if user isn't actively editing it,
     * to avoid stomping on partial input). */
    if (gURL && !gURLActive && gStatusURL[0] > 0) {
        TESetText((Ptr)(gStatusURL + 1), (long)gStatusURL[0], gURL);
    }

    draw_chrome_row();
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

        /* BR_EV_STATUS: u8 code, u8 urllen, u8 url[urllen]. */
        if (type == BR_EV_STATUS && len >= 2) {
            uint8_t code = buf[0];
            uint8_t urllen = buf[1];
            if ((uint16_t)(2 + urllen) > len) urllen = (uint8_t)(len - 2);
            if (urllen > sizeof(gStatusURL) - 1)
                urllen = (uint8_t)(sizeof(gStatusURL) - 1);
            gStatusCode = code;
            gStatusURL[0] = urllen;
            if (urllen) memcpy(gStatusURL + 1, buf + 2, urllen);
            gStatusDirty = true;
        }
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


/* URL field fills the chrome row between the last toolbar button
 * (right edge of [Stop]) and the start of the right-aligned status
 * label area. Left edge: [Stop right edge] + kPad. */
#define kURLLeft     (kPad + 4 * kBtnW + 3 * kPad + kPad)
#define kURLRight    (kStatusLeft - kPad)

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
    int x = kPad;
    Rect r;
    SetRect(&r, x, kBtnY, x + kBtnW, kBtnY + kBtnH);
    gBtnBack    = NewControl(gWin, &r, "\pBack",     true, 0,0,0,
                              0, (long)kCtlBack);     /* 0 = pushButProc */
    x += kBtnW + kPad;
    SetRect(&r, x, kBtnY, x + kBtnW, kBtnY + kBtnH);
    gBtnForward = NewControl(gWin, &r, "\pForward",  true, 0,0,0,
                              0, (long)kCtlForward);  /* 0 = pushButProc */
    x += kBtnW + kPad;
    SetRect(&r, x, kBtnY, x + kBtnW, kBtnY + kBtnH);
    gBtnReload  = NewControl(gWin, &r, "\pReload",   true, 0,0,0,
                              0, (long)kCtlReload);   /* 0 = pushButProc */
    x += kBtnW + kPad;
    SetRect(&r, x, kBtnY, x + kBtnW, kBtnY + kBtnH);
    gBtnStop    = NewControl(gWin, &r, "\pStop",     true, 0,0,0,
                              0, (long)kCtlStop);     /* 0 = pushButProc */

    /* Right-side small buttons: zoom out [-] and zoom in [+]. */
    SetRect(&r, kBtnMinusX, kSmallBtnY,
                kBtnMinusX + kSmallBtnW, kSmallBtnY + kSmallBtnH);
    gBtnMinus = NewControl(gWin, &r, "\p-", true, 0,0,0,
                           0, (long)kCtlMinus);
    SetRect(&r, kBtnPlusX, kSmallBtnY,
                kBtnPlusX + kSmallBtnW, kSmallBtnY + kSmallBtnH);
    gBtnPlus  = NewControl(gWin, &r, "\p+", true, 0,0,0,
                           0, (long)kCtlPlus);
}

static void open_window(void)
{
    /* Window y origin = 22 puts the title bar just below the 20 px
     * menu bar; total height (port + title bar) = 440 + 18 = 458,
     * so the window ends at y=480 exactly — full Mac screen used.
     *
     * NewCWindow gives us a CGrafPort with full Color QuickDraw, so
     * RGBBackColor + EraseRect can fill a solid light gray for the
     * chrome row (instead of QD's dithered ltGray pattern). */
    Rect bounds;
    SetRect(&bounds, 0, 40, kViewportW, 40 + kWinH);
    gWin = NewCWindow(NULL, &bounds, "\pMacBrowser",
                      true, documentProc, (WindowPtr)-1, true, 0);

    SetPort(gWin);
    make_toolbar();
    make_scrollbars();

    /* TENew destRect == viewRect for simple single-line input. Empty
     * at startup — the BR_CMD_NAV we push from main() will trigger a
     * navigation event whose BR_EV_STATUS fills the URL bar. */
    Rect ur;
    url_bar_rect(&ur);
    InsetRect(&ur, 3, 2);
    gURL = TENew(&ur, &ur);
    if (gURL) TEAutoView(true, gURL);
}

static void draw_chrome_row(void)
{
    if (!gWin) return;
    GrafPtr saved;
    GetPort(&saved);
    SetPort(gWin);

    /* "Platinum" gray that the Mac OS 7.5+ chrome conjures up —
     * RGB ≈ 0xBBBB (73% white) is the classic platinum fill, the
     * same shade Apple later codified in Mac OS 8's Appearance
     * Manager. Color QuickDraw + RGBBackColor + EraseRect gives a
     * solid (non-dithered) fill. */
    RGBColor gray  = { 0xBBBB, 0xBBBB, 0xBBBB };
    RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };

    /* 1. Erase the whole chrome row to gray. */
    RGBBackColor(&gray);
    Rect row;
    SetRect(&row, 0, kToolbarY, kViewportW, kToolbarY + kToolbarH);
    EraseRect(&row);

    /* 2. Draw all controls. With BackColor still = gray, each
     * button's bounding box erases to gray (matching the chrome),
     * leaving the rounded white button bodies on a gray field. */
    DrawControls(gWin);

    /* 3. Right-aligned status label, drawn on the gray bg between
     * URL bar and the +/- buttons. The label text is derived from
     * the most recent BR_EV_STATUS code; "\311" is the MacRoman
     * ellipsis glyph (…). */
    TextFont(applFont);   /* Geneva */
    TextSize(9);
    const unsigned char *label;
    switch (gStatusCode) {
    case BR_STATUS_LOADING: label = (const unsigned char *)"\pLoading\311"; break;
    case BR_STATUS_ERROR:   label = (const unsigned char *)"\pError"; break;
    case BR_STATUS_READY:
    default:                label = (const unsigned char *)"\pReady"; break;
    }
    short tw = StringWidth(label);
    short sx = kStatusRight - tw;
    if (sx < kStatusLeft) sx = kStatusLeft;
    MoveTo(sx, kStatusBaseY);
    DrawString(label);

    /* 4. URL text field: switch to white BackColor for the field
     * interior; frame + TE contents on top. */
    RGBBackColor(&white);
    Rect ur;
    url_bar_rect(&ur);
    EraseRect(&ur);
    FrameRect(&ur);
    if (gURL) {
        TextFont(4);   /* monaco — period-correct for URL display */
        TextSize(9);
        TEUpdate(&ur, gURL);
    }

    /* 5. 1-px separator between chrome and viewport. */
    PenNormal();
    MoveTo(0, kToolbarH - 1);
    LineTo(kViewportW - 1, kToolbarH - 1);

    SetPort(saved);
}

/* Push a 0-byte ring command through g2h. Several of these aren't in
 * the protocol header yet — host dispatcher logs unknown types but
 * keeps running, so we forward-define them here for now. */
#define BR_CMD_STOP       14
#define BR_CMD_ZOOM_OUT   15
#define BR_CMD_ZOOM_IN    16

/* Publish the viewport's screen position to the host so its VBL
 * hook can synthesize mouse events from the Mac.Mouse globals. The
 * guest's window port + chrome-row offset gives us viewport top-left
 * in *window* coords; LocalToGlobal turns it into screen coords. */
static void publish_viewport_pos(void)
{
    if (!gShm || !gWin) return;
    GrafPtr saved;
    GetPort(&saved);
    SetPort(gWin);

    Point tl;
    tl.h = 0;
    tl.v = kViewportY;
    LocalToGlobal(&tl);
    SetPort(saved);

    /* Guest is m68k native big-endian; the int16_t stores are no-op
     * on the host's BR_HOST byte-swap path. */
    gShm->viewport.screen_left = (int16_t)tl.h;
    gShm->viewport.screen_top  = (int16_t)tl.v;
}

/* Push a BR_CMD_KEY_DOWN with the cooked character that came out of
 * Mac OS's key translation (Mac's Event Manager has already run
 * dead-keys + modifiers + KCHR/KCHK against the raw scancode, so
 * `ch` is the Unicode-ish character we want to send to the page).
 * Payload format matches MacBrowser.h:
 *   u16 vk BE, u16 mods BE, u8 text_len, u8 text[text_len] */
static void send_key(uint16_t vk, uint16_t mods, char ch)
{
    if (!gShm) return;
    /* For now we send a single ASCII byte; the host's bidi.type
     * helper handles UTF-8 already, so when we later feed it a
     * full unicode char (via TextEncoding) it'll just work. */
    uint8_t buf[6];
    buf[0] = (uint8_t)((vk   >> 8) & 0xFF);
    buf[1] = (uint8_t)( vk         & 0xFF);
    buf[2] = (uint8_t)((mods >> 8) & 0xFF);
    buf[3] = (uint8_t)( mods       & 0xFF);
    buf[4] = 1;
    buf[5] = (uint8_t)ch;
    br_ring_push(&gShm->g2h, BR_CMD_KEY_DOWN, buf, 6);
}

/* Push a BR_CMD_SCROLL with delta-x/delta-y in pixels. Used by the
 * scroll-bar click handler. Payload: i32 dx BE, i32 dy BE = 8 bytes. */
static void send_scroll(int dx, int dy)
{
    if (!gShm) return;
    uint8_t buf[8];
    int32_t bx = (int32_t)dx;
    int32_t by = (int32_t)dy;
    memcpy(buf + 0, &bx, 4);
    memcpy(buf + 4, &by, 4);
    br_ring_push(&gShm->g2h, BR_CMD_SCROLL, buf, 8);
}

/* Push a BR_CMD_CLICK with page-coord (x,y), button index (0 = left,
 * 1 = middle, 2 = right), and click count. Payload is BE i32 x, i32 y,
 * u8 btn, u8 count = 10 bytes. */
static void send_click(int page_x, int page_y,
                       uint8_t btn, uint8_t count)
{
    if (!gShm) return;
    uint8_t buf[10];
    int32_t bx = (int32_t)page_x;   /* m68k native = BE; copy verbatim */
    int32_t by = (int32_t)page_y;
    memcpy(buf + 0, &bx, 4);
    memcpy(buf + 4, &by, 4);
    buf[8] = btn;
    buf[9] = count;
    if (br_ring_push(&gShm->g2h, BR_CMD_CLICK, buf, 10) == 0) {
        br_log(&gShm->log, BR_LOG_INF,
               "click %d,%d btn=%u", page_x, page_y, (unsigned)btn);
    }
}
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

/* Scroll-step deltas (in CSS pixels). Match common browser behavior:
 * arrow click = ~ one line, page-area click = roughly the visible
 * viewport height. */
#define kScrollStepLine    40
#define kScrollStepPage    320

/* Classic Mac Control Manager part codes for scroll bars. Retro68's
 * Controls.h doesn't always define these symbolically, so we use
 * the canonical numeric values from Inside Macintosh. */
#define kPartUpArrow      20
#define kPartDownArrow    21
#define kPartPageUp       22
#define kPartPageDown     23
#define kPartIndicator   129

static void dispatch_control(ControlHandle ctl, short part)
{
    long id = (**ctl).contrlRfCon;
    switch (id) {
    case kCtlBack:    send_toolbar_cmd(BR_CMD_BACK);     return;
    case kCtlForward: send_toolbar_cmd(BR_CMD_FORWARD);  return;
    case kCtlReload:  send_toolbar_cmd(BR_CMD_RELOAD);   return;
    case kCtlStop:    send_toolbar_cmd(BR_CMD_STOP);     return;
    case kCtlMinus:   send_toolbar_cmd(BR_CMD_ZOOM_OUT); return;
    case kCtlPlus:    send_toolbar_cmd(BR_CMD_ZOOM_IN);  return;
    }

    /* Scroll bars (refCon = 0; identify by control handle). */
    Boolean is_v = (ctl == gVSB);
    Boolean is_h = (ctl == gHSB);
    if (!is_v && !is_h) return;

    int dx = 0, dy = 0;
    int sign = 0, step = 0;
    switch (part) {
    case kPartUpArrow:    sign = -1; step = kScrollStepLine; break;
    case kPartDownArrow:  sign = +1; step = kScrollStepLine; break;
    case kPartPageUp:     sign = -1; step = kScrollStepPage; break;
    case kPartPageDown:   sign = +1; step = kScrollStepPage; break;
    case kPartIndicator:  /* TODO: read new value, compute delta */ return;
    default: return;
    }
    if (is_v) dy = sign * step;
    else      dx = sign * step;
    send_scroll(dx, dy);
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
            /* Window moved — refresh the published top-left so the
             * host can keep mouse-coord translation accurate. */
            publish_viewport_pos();
            break;
        }
        case inContent: {
            if (win != FrontWindow()) { SelectWindow(win); break; }
            SetPort(win);
            Point local = evt->where;
            GlobalToLocal(&local);

            /* 1. Toolbar button or scroll bar hit? FindControl
             * returns the part code at the click location; for
             * push buttons we still TrackControl so the user can
             * drag off to cancel, but for scroll bar arrows /
             * track we dispatch directly on the FindControl result
             * (TrackControl with a NULL actionProc returns 0 for
             * those parts in classic Mac, swallowing the click). */
            ControlHandle ctl = NULL;
            short part = FindControl(local, win, &ctl);
            if (part && ctl) {
                if (part == kPartIndicator || part == 10 /* button */) {
                    short tracked = TrackControl(ctl, local, NULL);
                    if (tracked) dispatch_control(ctl, tracked);
                } else {
                    /* arrows + page areas */
                    dispatch_control(ctl, part);
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

            /* 3. Click landed outside the chrome row — deactivate
             * URL bar so further keypresses don't go there. The host
             * polls Mac.Mouse globals each VBL and synthesizes the
             * click directly via BiDi when MacBrowser is the front
             * app — no need to forward the click through the ring. */
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
        char     ch   = evt->message & charCodeMask;
        uint16_t vk   = (uint16_t)((evt->message & keyCodeMask) >> 8);
        uint16_t mods = (uint16_t)evt->modifiers;
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
            break;
        }
        /* URL bar isn't active — forward the key to the page. */
        send_key(vk, mods, ch);
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

        /* Publish our viewport's on-screen position so the host's
         * VBL hook can compute page coords from Mac.Mouse globals. */
        publish_viewport_pos();

        /* Push our default home URL through the ring. Host's
         * cmd_dispatch picks it up → bidi.navigate → Firefox loads
         * the page → load events fire → BR_EV_STATUS comes back →
         * apply_status puts the URL into our URL bar. The host
         * spawned Firefox at about:blank, so this is the first
         * navigation. */
        const char *start = "https://example.com/";
        br_ring_push(&gShm->g2h, BR_CMD_NAV,
                     (void *)start, (uint16_t)strlen(start));
    }

    EventRecord evt;
    while (gRunning) {
        if (WaitNextEvent(everyEvent, &evt, 1, NULL))
            handle_event(&evt);

        poll_shm();
        drain_h2g();
        maybe_push_g2h();

        if (gStatusDirty) {
            gStatusDirty = false;
            apply_status();
        }

        /* TEIdle blinks the caret roughly every 30 ticks. Cheap. */
        if (gURL && gURLActive) TEIdle(gURL);
    }

    if (gURL)    { TEDispose(gURL);   gURL = NULL; }
    if (gShmPtr) { DisposePtr(gShmPtr); gShmPtr = NULL; gShm = NULL; }
    return 0;
}
