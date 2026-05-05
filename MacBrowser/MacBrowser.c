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
#include <Scrap.h>
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

/* Per-instance bridge dir resolved from :System Folder:Preferences:
 * MacPhoenix.cfg (written by the host installer). Same helper
 * BridgeAgent uses — keeps both apps in lock-step. */
#include "bridge_cfg.h"
#include "bridge_paths.h"

#define kAppleMenu      128
#define kFileMenu       129
#define kEditMenu       130
#define kBookmarksMenu  131

/* Bookmarks. Compile-time list; first entry is the home URL. To edit,
 * append/replace here and rebuild — there's no on-disk prefs path yet
 * (TODO: read from Host:MacPhoenix:Prefs:Bookmarks). Each entry has a
 * Pascal-string title for the menu item and a C string URL to navigate
 * to. Title length includes the leading length byte (Mac convention). */
typedef struct {
    const unsigned char *title;     /* Pascal string */
    const char          *url;       /* C string */
} BookmarkEntry;

static const BookmarkEntry gBookmarks[] = {
    { (const unsigned char *)"\pGoogle",
      "https://www.google.com/" },
    { (const unsigned char *)"\pYouTube test video",
      "https://www.youtube.com/watch?v=y0sF5xhGreA" },
    { (const unsigned char *)"\pMacintosh Repository",
      "https://www.macintoshrepository.org/" },
    { (const unsigned char *)"\pMacintosh Garden",
      "https://macintoshgarden.org/" },
    { (const unsigned char *)"\pmac-phoenix on GitHub",
      "https://github.com/sirmick/mac-phoenix" },
};
#define kBookmarkCount   ((short)(sizeof(gBookmarks) / sizeof(gBookmarks[0])))

/* Standard System 7 Edit-menu item indices. Order is mandated by HIG
 * (Inside Macintosh: Macintosh Toolbox Essentials, Ch. 3): Undo,
 * separator, Cut, Copy, Paste, Clear, Select All. The DA-supporting
 * gap (item 1 = Undo) is preserved even though MacBrowser doesn't
 * implement undo — keeping the menu shape conventional means any
 * desk accessory the user opens can use AppendResMenu / SystemEdit
 * dispatch transparently. */
#define kEditUndo       1
#define kEditCut        3
#define kEditCopy       4
#define kEditPaste      5
#define kEditClear      6
#define kEditSelectAll  7

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

/* Initial window port size. The window can be grown / shrunk after
 * launch — gPortW/gPortH track the current size. */
#define kInitPortW  640
#define kInitPortH  428
#define kSBSize     16   /* scroll bar thickness, classic Mac 7.5 */
#define kBtnH       20   /* main toolbar button body height */
#define kToolbarH   (kBtnH + 2 * kPad)   /* 28 = 4 + 20 + 4 */

/* GrowWindow size limits. */
#define kMinPortW    320
#define kMinPortH    (kToolbarH + 100)
#define kMaxPortW    BR_FB_MAX_W
#define kMaxPortH    (kToolbarH + BR_FB_MAX_H)

/* Vertical position of each row's TOP edge, in window-port coords. */
#define kToolbarY   0
#define kViewportY  (kToolbarY  + kToolbarH)

/* Toolbar buttons — text-only, period-correct (NN3 had a Pictures /
 * Text / Pictures+Text option). Four pushButProc controls on the
 * left, two small zoom controls right-justified. All gaps + edge
 * margins use kPad. */
#define kBtnW         64
#define kBtnY         kPad

#define kSmallBtnW    22
#define kSmallBtnH    kBtnH      /* same height as the main buttons */
#define kSmallBtnY    kBtnY

/* "Ready" status label area, between URL bar and the +/- buttons. */
#define kStatusW      48
#define kStatusBaseY  ((kToolbarH + 9) / 2 - 2)

/* Runtime layout: derived from the current window port size. The
 * relayout() function recomputes these whenever we open or grow
 * the window so every drawer + hit-tester sees consistent
 * dimensions. */
static int gPortW;             /* port width (= window content width) */
static int gPortH;             /* port height (= chrome + viewport rows) */
static int gPixelsW;           /* visible viewport width  = gPortW - kSBSize */
static int gPixelsH;           /* visible viewport height = gPortH - kToolbarH - kSBSize */
static int gBtnPlusX;          /* x of [+] button */
static int gBtnMinusX;         /* x of [-] button */
static int gStatusLeft;        /* left edge of status label area */
static int gStatusRight;       /* right edge of status label area */
static int gURLLeft;           /* left edge of URL field */
static int gURLRight;          /* right edge of URL field */

/* refCon values used to identify which button got clicked. */
#define kCtlBack     1
#define kCtlForward  2
#define kCtlReload   3
#define kCtlStop     4
#define kCtlMinus    5
#define kCtlPlus     6

static MenuHandle gAppleMenuH;
static MenuHandle gFileMenuH;
static MenuHandle gEditMenuH;
static MenuHandle gBookmarksMenuH;
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
static uint32_t       gH2gReceived = 0;       /* events from host       */
static uint16_t       gLastEvtType = 0;

/* Mirrored from the most recent BR_EV_STATUS we drained out of h2g.
 * gStatusCode is BR_STATUS_*; gStatusDirty signals the main loop to
 * redraw + (optionally) update the URL bar with the new URL. */
static uint8_t        gStatusCode = BR_STATUS_READY;
static unsigned char  gStatusURL[252];   /* Pascal-string scratch */
static Boolean        gStatusDirty = false;

/* Mirrored from the most recent BR_EV_PAGE_METRICS. Latched in
 * drain_h2g; consumed by refresh_scrollbars in the main loop, where
 * the GrafPort is guaranteed to be set to the browser window. */
static uint32_t       gPageW       = 0, gPageH       = 0;
static uint32_t       gScrollX     = 0, gScrollY     = 0;
static uint32_t       gViewportPxW = 0, gViewportPxH = 0;
static Boolean        gMetricsDirty = false;
static unsigned long  gLastBarsRepaint = 0;

/* Mirrored from BR_EV_TITLE/HISTORY/ZOOM. Apply on the next main-loop
 * tick so we have a valid GrafPort. The host emits each event only on
 * change, so dirty-flag → apply_x() → clear-flag is enough; no need to
 * remember the previous value on the guest side. */
static unsigned char  gWinTitle[252];      /* Pascal string */
static Boolean        gTitleDirty    = false;
static Boolean        gCanGoBack     = false;
static Boolean        gCanGoForward  = false;
static Boolean        gHistoryDirty  = false;
static uint16_t       gZoomPct       = 100;
static Boolean        gZoomDirty     = false;

/* Off-screen 16-bit RGB555 PixMap header that wraps gShm->fb.pixels. */
static PixMap     gShmPixMap;
static CTabHandle gShmCTab = NULL;

/* Write a freeform line into Host:MacPhoenix:MacBrowser.log for
 * host-side debugging. Uses ExtFS like BridgeAgent does. */
static void spike_log(const char *tag)
{
    FSSpec sp;
    short ref;
    Str255 log_path; br_cfg_path(log_path, BR_FILE_BROWSER_LOG);
    if (FSMakeFSSpec(0, 0, log_path, &sp) == fnfErr)
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
    Str255 shm_path; br_cfg_path(shm_path, BR_FILE_BROWSER_SHM);
    OSErr err = FSMakeFSSpec(0, 0, shm_path, &sp);
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

/* Apply the latest BR_EV_TITLE: paint the new page title into the
 * window's title bar. SetWTitle handles the title-bar redraw. */
static void apply_title(void)
{
    if (!gWin) return;
    if (gWinTitle[0] == 0) {
        SetWTitle(gWin, "\pMacBrowser");
    } else {
        SetWTitle(gWin, gWinTitle);
    }
}

/* Apply the latest BR_EV_HISTORY: enable/disable Back+Forward toolbar
 * buttons. HiliteControl(c, 0) = active; HiliteControl(c, 255) =
 * inactive (greyed out — Mac convention for "can't act on this"). */
static void apply_history(void)
{
    if (!gWin) return;
    GrafPtr saved;
    GetPort(&saved);
    SetPort(gWin);

    if (gBtnBack)
        HiliteControl(gBtnBack,    gCanGoBack    ? 0 : 255);
    if (gBtnForward)
        HiliteControl(gBtnForward, gCanGoForward ? 0 : 255);

    SetPort(saved);
}

/* Read big-endian u32 from a byte buffer. */
static uint32_t be32_load(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

/* Update one scrollbar from its corresponding axis of the latest
 * BR_EV_PAGE_METRICS. min stays 0; max = max(0, page - viewport);
 * value = scroll. SetControlMaximum to 0 → bar renders inactive
 * (greyed-out, no thumb), which is exactly the "no scrolling
 * possible" state.
 *
 * NB: SetControlMaximum/SetControlValue auto-redraw the control
 * but only into the *current* GrafPort. Call sites guarantee the
 * window's port is set before invoking us. We additionally force a
 * Draw1Control at the end as belt-and-suspenders — the auto-redraw
 * is conditional on "value actually changed" which trips when the
 * page is fully static (no metrics change → no SetControlValue call
 * → no redraw → bar visually stale after a window-update event). */
static void apply_scroll_axis(ControlHandle ctl, long page, long vp, long pos)
{
    if (!ctl) return;
    long maxv = page - vp;
    if (maxv < 0) maxv = 0;
    if (maxv > 32767) maxv = 32767;   /* Mac Control Manager is i16 */
    if (pos  < 0) pos  = 0;
    if (pos  > maxv) pos = maxv;

    SetControlMaximum(ctl, (short)maxv);
    SetControlMinimum(ctl, 0);
    SetControlValue  (ctl, (short)pos);
    /* HiliteControl 255 = inactive; 0 = active. The Control Manager
     * also auto-greys when min==max, but being explicit keeps the
     * appearance crisp on slow paint paths. */
    HiliteControl(ctl, (maxv == 0) ? 255 : 0);
    Draw1Control(ctl);
}

/* Apply the latched gPage / gScroll metrics to the V/H scrollbars,
 * then force a Draw1Control on each. Called from the main loop
 * with gWin's GrafPort already current.
 *
 * Why this is its own pass instead of just inside drain_h2g: the
 * Control Manager only paints into the *current* port, and drain_h2g
 * runs in whatever port WaitNextEvent left set, typically not the
 * browser window's. Also, calling this every tick (not only on
 * gMetricsDirty) papers over the "control didn't redraw" cases
 * where update events for the bar's region get coalesced or eaten
 * by the framebuffer copy. Cost is two Draw1Control calls per
 * tick, each microseconds on a Quadra. */
static void refresh_scrollbars(void)
{
    if (!gWin) return;

    /* The 16×16 corner where the V and H scrollbars meet sits outside
     * both controls' invalidated rects, so it accumulates stale
     * framebuffer pixels (Firefox content drifts in via CopyBits, the
     * scrollbars paint around it). Easy fix: white-fill it before the
     * bars repaint. Mac default fore/back is black/white, so a plain
     * EraseRect with the saved port produces the correct color. */
    Rect corner;
    SetRect(&corner, gPortW - kSBSize, gPortH - kSBSize, gPortW, gPortH);
    EraseRect(&corner);

    apply_scroll_axis(gVSB, (long)gPageH, (long)gViewportPxH,
                      (long)gScrollY);
    apply_scroll_axis(gHSB, (long)gPageW, (long)gViewportPxW,
                      (long)gScrollX);
    gMetricsDirty = false;
    gLastBarsRepaint = TickCount();
}

/* Drain any host events queued in h2g. Cheap on every tick.
 * Buffer sized for the largest event payload we currently push:
 * BR_EV_SELECTION caps text at 4096 bytes. Anything bigger gets
 * truncated by br_ring_pop (still ring-consistent) — bump if we
 * ever ship larger payloads. */
static void drain_h2g(void)
{
    if (!gShm) return;
    static uint8_t buf[4096 + 4];     /* static so we don't blow the stack */
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
        /* BR_EV_SELECTION: u16 len, u8 text[len]. Reply to a
         * BR_CMD_GET_SELECTION we sent on Cmd+C. Land the payload
         * into the Mac scrap as 'TEXT' so the user can Cmd+V the
         * web-copied text into any other Mac app. v1: text bytes
         * are UTF-8 from Firefox; we drop them into 'TEXT' as-is,
         * which is correct for ASCII and lossy for non-ASCII (the
         * Mac apps will see UTF-8 multi-byte sequences as MacRoman
         * gibberish). UTF-8↔MacRoman lookup is a TODO. */
        else if (type == BR_EV_SELECTION && len >= 2) {
            uint16_t n = (uint16_t)((buf[0] << 8) | buf[1]);
            if ((uint16_t)(2 + n) > len) n = (uint16_t)(len - 2);
            ZeroScrap();
            if (n > 0) PutScrap((long)n, 'TEXT', (Ptr)(buf + 2));
        }
        /* BR_EV_PAGE_METRICS: 6× u32 BE — page_w, page_h,
         * scroll_x, scroll_y, vp_w, vp_h. Drives V/H scrollbar
         * range + thumb. Sent only when something changed host-side,
         * so this path is cold most ticks. The Control Manager paints
         * into the *current* GrafPort, so swap to the browser window
         * before applying. */
        else if (type == BR_EV_PAGE_METRICS && len >= 24) {
            uint32_t pw = be32_load(buf +  0);
            uint32_t ph = be32_load(buf +  4);
            uint32_t sx = be32_load(buf +  8);
            uint32_t sy = be32_load(buf + 12);
            uint32_t vw = be32_load(buf + 16);
            uint32_t vh = be32_load(buf + 20);
            /* Mirror the latest values so the per-tick repaint can
             * keep the bars fresh between events. */
            gPageW = pw; gPageH = ph;
            gScrollX = sx; gScrollY = sy;
            gViewportPxW = vw; gViewportPxH = vh;
            gMetricsDirty = true;
        }
        /* BR_EV_TITLE: u8 title_len, u8 title[title_len]. utf8 from
         * QtWebEngine; we land it as a Pascal string and let
         * SetWTitle paint it on the next apply_title() tick. UTF-8 →
         * MacRoman is a pass-through here (lossy for non-ASCII);
         * proper transcoding is a TODO. */
        else if (type == BR_EV_TITLE && len >= 1) {
            uint8_t tlen = buf[0];
            if ((uint16_t)(1 + tlen) > len) tlen = (uint8_t)(len - 1);
            if (tlen > sizeof(gWinTitle) - 1)
                tlen = (uint8_t)(sizeof(gWinTitle) - 1);
            gWinTitle[0] = tlen;
            if (tlen) memcpy(gWinTitle + 1, buf + 1, tlen);
            gTitleDirty = true;
        }
        /* BR_EV_HISTORY: u8 can_back, u8 can_forward. Drives
         * Back/Forward toolbar button enable. */
        else if (type == BR_EV_HISTORY && len >= 2) {
            gCanGoBack    = (buf[0] != 0);
            gCanGoForward = (buf[1] != 0);
            gHistoryDirty = true;
        }
        /* BR_EV_ZOOM: u16 zoom_pct (BE). Mirrored for future UI; v2
         * stores it without a chrome-row indicator. */
        else if (type == BR_EV_ZOOM && len >= 2) {
            gZoomPct   = (uint16_t)((buf[0] << 8) | buf[1]);
            gZoomDirty = true;
        }
    }
}

static void url_bar_rect(Rect *out)
{
    SetRect(out, gURLLeft, kBtnY, gURLRight, kBtnY + kBtnH);
}

/* Recompute layout from gPortW/gPortH. Reposition every control,
 * update the URL field's TE bounds, and force a redraw. Called
 * on window-open and after a successful GrowWindow. */
static void relayout(void)
{
    /* Pixel area shrinks by the scroll bar thickness on right + bottom. */
    gPixelsW = gPortW - kSBSize;
    gPixelsH = gPortH - kToolbarH - kSBSize;
    if (gPixelsW < 32) gPixelsW = 32;
    if (gPixelsH < 32) gPixelsH = 32;

    /* Right-justified positions on the chrome row. */
    gBtnPlusX    = gPortW - kPad - kSmallBtnW;
    gBtnMinusX   = gBtnPlusX - kPad - kSmallBtnW;
    gStatusRight = gBtnMinusX - kPad;
    gStatusLeft  = gStatusRight - kStatusW;
    /* URL field stretches from after the last toolbar button to
     * just before the status label. */
    gURLLeft     = kPad + 4 * kBtnW + 3 * kPad + kPad;
    gURLRight    = gStatusLeft - kPad;

    if (!gWin) return;

    /* Move/size the right-side controls. */
    Rect r;
    if (gBtnMinus) {
        SetRect(&r, gBtnMinusX, kSmallBtnY,
                    gBtnMinusX + kSmallBtnW, kSmallBtnY + kSmallBtnH);
        MoveControl(gBtnMinus, r.left, r.top);
    }
    if (gBtnPlus) {
        SetRect(&r, gBtnPlusX, kSmallBtnY,
                    gBtnPlusX + kSmallBtnW, kSmallBtnY + kSmallBtnH);
        MoveControl(gBtnPlus, r.left, r.top);
    }
    if (gVSB) {
        MoveControl(gVSB, gPortW - kSBSize, kViewportY);
        SizeControl(gVSB, kSBSize,
                          gPortH - kToolbarH - kSBSize);
        /* Force a fresh paint at the new geometry. MoveControl +
         * SizeControl normally redraw, but during fast drag-resize
         * the bar's region can be left holding stale framebuffer
         * pixels (CopyBits had it before, the resize moved it, no
         * updateEvt fired in time). Draw1Control bypasses the event
         * queue and paints right now. */
        Draw1Control(gVSB);
    }
    if (gHSB) {
        MoveControl(gHSB, 0, gPortH - kSBSize);
        SizeControl(gHSB, gPortW - kSBSize, kSBSize);
        Draw1Control(gHSB);
    }

    /* Re-bound the URL TE. */
    if (gURL) {
        Rect ur;
        url_bar_rect(&ur);
        InsetRect(&ur, 3, 2);
        (**gURL).destRect = ur;
        (**gURL).viewRect = ur;
    }

    /* Force a repaint of the whole window. */
    Rect all;
    SetRect(&all, 0, 0, gPortW, gPortH);
    InvalRect(&all);
}

/* Push current pixel area dims through the ring as BR_CMD_RESIZE.
 * Host's cmd_dispatch maps this to bidi.set_viewport. Called on
 * window-open (sync host with whatever size we ended up at) and
 * after every GrowWindow. */
static void publish_size(void)
{
    if (!gShm) return;
    uint8_t buf[4];
    uint16_t w = (uint16_t)gPixelsW;
    uint16_t h = (uint16_t)gPixelsH;
    buf[0] = (uint8_t)((w >> 8) & 0xFF);
    buf[1] = (uint8_t)( w       & 0xFF);
    buf[2] = (uint8_t)((h >> 8) & 0xFF);
    buf[3] = (uint8_t)( h       & 0xFF);
    br_ring_push(&gShm->g2h, BR_CMD_RESIZE, buf, 4);
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
    SetRect(&r, gPortW - kSBSize, kViewportY,
                gPortW,             gPortH - kSBSize);
    gVSB = NewControl(gWin, &r, "\p", true, 0, 0, 100, 16, 0);

    /* Horizontal: bottom edge of viewport, from 0 to corner. */
    SetRect(&r, 0,                  gPortH - kSBSize,
                gPortW - kSBSize,   gPortH);
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
    SetRect(&r, gBtnMinusX, kSmallBtnY,
                gBtnMinusX + kSmallBtnW, kSmallBtnY + kSmallBtnH);
    gBtnMinus = NewControl(gWin, &r, "\p-", true, 0,0,0,
                           0, (long)kCtlMinus);
    SetRect(&r, gBtnPlusX, kSmallBtnY,
                gBtnPlusX + kSmallBtnW, kSmallBtnY + kSmallBtnH);
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
    SetRect(&bounds, 0, 40, kInitPortW, 40 + kInitPortH);
    gWin = NewCWindow(NULL, &bounds, "\pMacBrowser",
                      true, documentProc, (WindowPtr)-1, true, 0);

    /* Seed runtime layout from the window we just opened. SetPort
     * must come first because relayout() ends with InvalRect, which
     * needs a valid current port. */
    SetPort(gWin);
    gPortW = kInitPortW;
    gPortH = kInitPortH;
    relayout();

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
    SetRect(&row, 0, kToolbarY, gPortW, kToolbarY + kToolbarH);
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
    short sx = gStatusRight - tw;
    if (sx < gStatusLeft) sx = gStatusLeft;
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
    LineTo(gPortW - 1, kToolbarH - 1);

    SetPort(saved);
}

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
    gShm->viewport_screen_left = (int16_t)tl.h;
    gShm->viewport_screen_top  = (int16_t)tl.v;
}

/* Push BR_CMD_GET_SELECTION (no payload). Host BiDi-evaluates
 * window.getSelection().toString() and replies with a
 * BR_EV_SELECTION whose payload we land into the Mac scrap from
 * drain_h2g. v1 is text only; HTML / styled text → defer. */
static void request_web_copy(void)
{
    if (!gShm) return;
    br_ring_push(&gShm->g2h, BR_CMD_GET_SELECTION, NULL, 0);
}

/* Read 'TEXT' out of the Mac scrap and ship it as BR_CMD_PASTE.
 * Mac 'TEXT' is MacRoman. v1: pass bytes through verbatim — works
 * for ASCII content (which is what 99% of paste targets are);
 * upper-MacRoman codepoints arrive at Firefox as garbled UTF-8.
 * MacRoman→UTF-8 conversion is a 256-entry lookup we can wire
 * later if it bites.
 *
 * Logs to the host shm log on each call so we can see "scrap empty"
 * vs "scrap has N bytes" without instrumenting both sides. */
static void paste_scrap_to_web(void)
{
    if (!gShm) return;
    Handle h = NewHandle(0);
    if (!h) return;
    long off = 0;
    long size = GetScrap(h, 'TEXT', &off);
    if (size <= 0) {
        /* No 'TEXT' flavor — try 'utxt' (UTF-16). Some apps don't
         * push 'TEXT' alongside, especially if the source had
         * non-Latin characters. */
        SetHandleSize(h, 0);
        size = GetScrap(h, 'utxt', &off);
        br_log(&gShm->log, BR_LOG_INF,
               "paste_scrap: TEXT=empty, utxt=%ld", (long)size);
        DisposeHandle(h);
        return;
    }
    br_log(&gShm->log, BR_LOG_INF,
           "paste_scrap: TEXT=%ld bytes", (long)size);
    if (size > 4096) size = 4096;     /* clamp to ring payload */
    HLock(h);
    uint16_t n = (uint16_t)size;
    static uint8_t buf[2 + 4096];
    buf[0] = (uint8_t)(n >> 8);
    buf[1] = (uint8_t)(n & 0xFF);
    memcpy(buf + 2, *h, n);
    HUnlock(h);
    DisposeHandle(h);
    br_ring_push(&gShm->g2h, BR_CMD_PASTE, buf, (uint16_t)(2 + n));
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
    if (part == kPartIndicator) {
        /* Thumb drag: control's value is the new absolute scroll
         * position in the same units we set on the scale (CSS pixels).
         * Compute the delta from the cached gScrollX/Y mirror, ship
         * it as BR_CMD_SCROLL, and update the mirror locally so the
         * next drag computes from the new position rather than
         * waiting on the next BR_EV_PAGE_METRICS round-trip. */
        long target  = (long)GetControlValue(ctl);
        long current = is_v ? (long)gScrollY : (long)gScrollX;
        long delta   = target - current;
        if (delta == 0) return;
        if (is_v) { dy = (int)delta; gScrollY = (uint32_t)target; }
        else      { dx = (int)delta; gScrollX = (uint32_t)target; }
        send_scroll(dx, dy);
        return;
    }

    int sign = 0, step = 0;
    switch (part) {
    case kPartUpArrow:    sign = -1; step = kScrollStepLine; break;
    case kPartDownArrow:  sign = +1; step = kScrollStepLine; break;
    case kPartPageUp:     sign = -1; step = kScrollStepPage; break;
    case kPartPageDown:   sign = +1; step = kScrollStepPage; break;
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

    /* Standard Edit menu — the keyboard equivalents are handled in
     * the keyDown branch directly (so URL-bar TextEdit gets the
     * local-edit path), but the menu items must exist so MenuKey()
     * highlights them on Cmd-press and they're discoverable in the
     * menu bar. The "/" syntax in AppendMenu binds Cmd+letter to
     * the item. */
    gEditMenuH = NewMenu(kEditMenu, "\pEdit");
    AppendMenu(gEditMenuH, "\pUndo/Z");
    AppendMenu(gEditMenuH, "\p-");
    AppendMenu(gEditMenuH, "\pCut/X");
    AppendMenu(gEditMenuH, "\pCopy/C");
    AppendMenu(gEditMenuH, "\pPaste/V");
    AppendMenu(gEditMenuH, "\pClear");
    AppendMenu(gEditMenuH, "\pSelect All/A");
    DisableItem(gEditMenuH, kEditUndo);    /* No undo path yet. */
    InsertMenu(gEditMenuH, 0);

    /* Bookmarks menu — one item per entry in gBookmarks. AppendMenu's
     * "/X" syntax interprets the rest of the string as an option flag,
     * so we use SetMenuItemText after AppendMenu("\px") to set the
     * actual title verbatim and avoid surprises with URL-y characters. */
    gBookmarksMenuH = NewMenu(kBookmarksMenu, "\pBookmarks");
    for (short i = 0; i < kBookmarkCount; i++) {
        AppendMenu(gBookmarksMenuH, "\px");
        SetMenuItemText(gBookmarksMenuH, i + 1,
                        (unsigned char *)gBookmarks[i].title);
    }
    InsertMenu(gBookmarksMenuH, 0);

    DrawMenuBar();
}

static void do_about(void)
{
    ParamText("\pMacPhoenix MacBrowser\rModern web inside System 7.",
              "\p", "\p", "\p");
    NoteAlert(128, NULL);
}

/* Navigate to one of the precanned bookmarks. Pushes BR_CMD_NAV with
 * the URL exactly as authored (no trailing CR like the URL-bar path).
 * The URL bar will refresh from the host's BR_EV_STATUS once the load
 * starts, so we don't need to update gURL ourselves. */
static void go_bookmark(short item_1based)
{
    if (!gShm) return;
    if (item_1based < 1 || item_1based > kBookmarkCount) return;
    const char *url = gBookmarks[item_1based - 1].url;
    if (!url) return;
    br_ring_push(&gShm->g2h, BR_CMD_NAV,
                 (void *)url, (uint16_t)strlen(url));
}

static void do_menu(long choice)
{
    short menuID = HiWord(choice);
    short item   = LoWord(choice);
    if (menuID == kAppleMenu) {
        if (item == 1) do_about();
    } else if (menuID == kFileMenu) {
        if (item == 1) gRunning = false;
    } else if (menuID == kBookmarksMenu) {
        go_bookmark(item);
    } else if (menuID == kEditMenu) {
        /* URL-bar Cmd+C / Cmd+X always need ZeroScrap + TECut/TECopy
         * + TEToScrap so the text reaches the desk scrap (TECut/TECopy
         * alone only fill TextEdit's *private* scrap, invisible to
         * other apps and to our paste_scrap_to_web reader). Cmd+V
         * needs TEFromScrap before TEPaste so any text the user just
         * copied in another app overrides the stale TE scrap. */
        switch (item) {
        case kEditCut:
            if (gURLActive && gURL) {
                ZeroScrap(); TECut(gURL); TEToScrap();
            } else {
                request_web_copy();
            }
            break;
        case kEditCopy:
            if (gURLActive && gURL) {
                ZeroScrap(); TECopy(gURL); TEToScrap();
            } else {
                request_web_copy();
            }
            break;
        case kEditPaste:
            if (gURLActive && gURL) {
                TEFromScrap(); TEPaste(gURL);
            } else {
                paste_scrap_to_web();
            }
            break;
        case kEditClear:
            if (gURLActive && gURL) TEDelete(gURL);
            break;
        case kEditSelectAll:
            if (gURLActive && gURL) TESetSelect(0, 32767, gURL);
            else if (gShm) br_ring_push(&gShm->g2h, BR_CMD_SELECT_ALL,
                                        NULL, 0);
            break;
        }
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
        case inGrow: {
            Rect limits;
            SetRect(&limits, kMinPortW, kMinPortH, kMaxPortW, kMaxPortH);
            long sz = GrowWindow(win, evt->where, &limits);
            if (sz != 0) {
                int new_w = (int)(sz & 0xFFFF);
                int new_h = (int)((sz >> 16) & 0xFFFF);
                SizeWindow(win, new_w, new_h, true);
                gPortW = new_w;
                gPortH = new_h;
                relayout();
                publish_viewport_pos();
                publish_size();
            }
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
            /* Cmd+L: focus URL bar (browser convention; not exposed
             * as a menu item). All other Cmd-keys route through the
             * menu bar — Cmd+C/X/V/A hit the Edit menu, Cmd+Q hits
             * File. do_menu branches URL-bar vs web-context. */
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

    /* Resolve the bridge dir (Host:MacPhoenix:<pid>) before any code
     * that writes log lines or the shm handshake — both go through
     * br_cfg_path, which lazy-loads but resolving up-front is one
     * less surprise during startup. */
    br_cfg_load();

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

        /* Sync the host's Firefox viewport with whatever pixel area
         * we ended up at. Even if we stayed at the initial size,
         * this catches any drift between the supervisor's Xvfb
         * dims and our compiled-in defaults. */
        publish_size();

        /* MacBrowser owns the start URL. The first bookmark is the
         * home page — keep this convention so the next iteration
         * (ExtFS-backed bookmarks file, one URL per line) can drop in
         * with the same "first line wins" rule. */
        const char *home = gBookmarks[0].url;
        br_ring_push(&gShm->g2h, BR_CMD_NAV,
                     (void *)home, (uint16_t)strlen(home));
    }

    EventRecord evt;
    while (gRunning) {
        if (WaitNextEvent(everyEvent, &evt, 1, NULL))
            handle_event(&evt);

        poll_shm();
        drain_h2g();

        if (gStatusDirty) {
            gStatusDirty = false;
            apply_status();
        }
        if (gTitleDirty) {
            gTitleDirty = false;
            apply_title();
        }
        if (gHistoryDirty) {
            gHistoryDirty = false;
            apply_history();
        }
        if (gZoomDirty) {
            /* No chrome-row UI for zoom yet; just consume the flag so
             * the host's BR_EV_ZOOM doesn't pile up. The mirrored
             * gZoomPct is available for future menu / status display. */
            gZoomDirty = false;
        }

        /* Repaint the scrollbars. Always force-redraw on a fresh
         * BR_EV_PAGE_METRICS, *and* unconditionally every ~15 ticks
         * (~250 ms) to paper over the cases where the auto-redraw
         * inside SetControlValue gets eaten by an update-event race
         * or a stale GrafPort on the host side. Cheap — Draw1Control
         * on a 16-px-wide control is microseconds. */
        {
            GrafPtr saved;
            GetPort(&saved);
            SetPort(gWin);
            unsigned long now = TickCount();
            if (gMetricsDirty || now - gLastBarsRepaint > 15) {
                refresh_scrollbars();
            }
            SetPort(saved);
        }

        /* TEIdle blinks the caret roughly every 30 ticks. Cheap. */
        if (gURL && gURLActive) TEIdle(gURL);
    }

    if (gURL)    { TEDispose(gURL);   gURL = NULL; }
    if (gShmPtr) { DisposePtr(gShmPtr); gShmPtr = NULL; gShm = NULL; }
    return 0;
}
