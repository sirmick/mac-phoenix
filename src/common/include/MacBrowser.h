/*
 *  MacBrowser.h — shared layout for the BrowserShm region.
 *
 *  ALLOCATION: Browser.app allocates the entire BrowserShm struct out
 *  of its own application heap via NewPtrClear(sizeof(BrowserShm)).
 *  The guest is the owner; the host borrows the address.
 *
 *  HANDSHAKE: Browser.app writes the buffer's Mac address (8 hex
 *  digits, ASCII) into Host:MacPhoenix:browser_shm.txt on the ExtFS
 *  share. mac-phoenix polls that file, reads the address, validates
 *  magic + version, and translates Mac → host via Mac2HostAddr() to
 *  get a writable host pointer. From then on the host writes pixels
 *  and events directly into the guest's buffer.
 *
 *  Sized for the worst case (1024×768×16 framebuffer + two 64 KB
 *  rings ≈ 1.7 MiB), so the guest's SIZE resource needs to advertise
 *  at least 4 MiB of partition. NewPtrClear may fail if the
 *  application heap is configured smaller — the guest must handle
 *  that and report it back via the same handshake file.
 *
 *  SYNCHRONIZATION: The existing 60 Hz VBL drives both rings. Host
 *  drains pending events into h2g before firing each VBL; the guest
 *  VBL task drains them and queues commands back through g2h.
 *
 *  WIRE FORMAT: classic Mac is big-endian; host is typically
 *  little-endian. ALL multi-byte fields in this region are stored
 *  big-endian on the wire. Use br_u16/u32_load/store accessors. NEVER
 *  load or store a multi-byte field with a raw pointer dereference.
 *
 *  ── State ownership ────────────────────────────────────────────
 *
 *  HOST (mac-phoenix + QtWebEngine) is authoritative for everything
 *  the page produces. Each piece of state has exactly one canonical
 *  storage location and one event that publishes changes to the
 *  guest. The guest mirrors what it needs for UI; it must not invent
 *  page state on its own.
 *
 *    State                    Owner   Storage                Event
 *    ─────────────────────────────────────────────────────────────────
 *    Current URL              host    QWebEnginePage::url    BR_EV_STATUS
 *    Loading state            host    Qt loadStarted/Finished BR_EV_STATUS.code
 *    Page title               host    QWebEnginePage::title  BR_EV_TITLE
 *    canGoBack/canGoForward   host    QWebEngineHistory      BR_EV_HISTORY
 *    Zoom factor              host    setZoomFactor cache    BR_EV_ZOOM
 *    Page metrics             host    JS poll (4 Hz)         BR_EV_PAGE_METRICS
 *    Selection text           host    JS getSelection()      BR_EV_SELECTION
 *    Download progress        host    QWebEngineDownloadReq  BR_EV_DOWNLOAD
 *    Framebuffer              host    fb.pixels + fb.dirty   BR_EV_FRAME
 *
 *  GUEST (MacBrowser.app) owns its own UI surface only:
 *
 *    State                    Storage
 *    ────────────────────────────────────────────────────────────
 *    Window position          BrowserShm.viewport_screen_left/top
 *    URL bar text (in flight) gURL TextEdit handle (committed via BR_CMD_NAV on Enter)
 *    Visibility flag          (host polls Mac CurApName)
 *
 *  Bidirectional with explicit handoff:
 *    Viewport pixel size      guest commits via BR_CMD_RESIZE,
 *                             host applies + acknowledges by emitting
 *                             the next frame at the new fb.width/height
 *    Page scroll              host reports via BR_EV_PAGE_METRICS,
 *                             guest scrolls page via BR_CMD_SCROLL
 *
 *  PROTOCOL VERSIONING: BR_VERSION below is the single contract
 *  number. Bump it whenever the wire format changes. The host checks
 *  magic + version on handshake and refuses to attach to a mismatched
 *  guest (rather than corrupting state).
 *
 *  See docs/plan/MacBrowser.md for the design rationale.
 */
#ifndef MACBROWSER_H
#define MACBROWSER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ──────────────────────────────────────────────── */

#define BR_MAGIC          0x42525753u    /* 'BRWS' */
/* v2 (qt-port): added BR_EV_TITLE/HISTORY/ZOOM. Strict per-opcode
 * length validation in the host; any mismatched payload is dropped. */
#define BR_VERSION        2u

/* Path on the ExtFS share where Browser.app publishes the Mac address
 * of its NewPtrClear'd BrowserShm. ASCII hex, 8 chars + newline. The
 * host polls this file (via the same bridge_dir mechanism BridgeAgent
 * uses for command IPC) and parses it once present. */
#define BR_HANDSHAKE_FILE "browser_shm.txt"

#define BR_RING_SIZE      65536u         /* 64 KiB per direction */
#define BR_DIRTY_MAX      64u

/* Framebuffer max dimensions. Matches the largest viewport we expect
 * to render. 16-bit RGB555 = 2 bytes per pixel. */
#define BR_FB_MAX_W       1024u
#define BR_FB_MAX_H       768u
#define BR_FB_BPP         2u
#define BR_FB_BYTES       (BR_FB_MAX_W * BR_FB_MAX_H * BR_FB_BPP)

/* ── Byte-order accessors ───────────────────────────────────── */

/* On the host (BR_HOST defined by the build), all multi-byte access
 * goes through __builtin_bswap so we read/write big-endian on the
 * wire. On the guest (Retro68 m68k, no define), we're already
 * big-endian and the swap is a no-op. */
#ifdef BR_HOST
#  define BR_BSWAP32(x) __builtin_bswap32((uint32_t)(x))
#  define BR_BSWAP16(x) __builtin_bswap16((uint16_t)(x))
#else
#  define BR_BSWAP32(x) (x)
#  define BR_BSWAP16(x) (x)
#endif

static inline uint32_t br_u32_load(const volatile uint32_t *p) {
    return BR_BSWAP32(*p);
}
static inline void br_u32_store(volatile uint32_t *p, uint32_t v) {
    *p = BR_BSWAP32(v);
}
static inline uint16_t br_u16_load(const volatile uint16_t *p) {
    return BR_BSWAP16(*p);
}
static inline void br_u16_store(volatile uint16_t *p, uint16_t v) {
    *p = BR_BSWAP16(v);
}

/* Memory barriers. PPC has weak ordering — index updates need
 * release/acquire semantics or the consumer can see the index advance
 * before the data writes are visible. 68k has TSO-ish ordering and
 * doesn't strictly need fences for this pattern, but we emit them
 * anyway; the cost is a no-op or a single instruction. */
#ifdef BR_HOST
#  define BR_FENCE_RELEASE() __sync_synchronize()
#  define BR_FENCE_ACQUIRE() __sync_synchronize()
#else
#  if defined(__ppc__) || defined(__POWERPC__)
#    define BR_FENCE_RELEASE() __asm__ volatile ("eieio" ::: "memory")
#    define BR_FENCE_ACQUIRE() __asm__ volatile ("lwsync" ::: "memory")
#  else
#    define BR_FENCE_RELEASE() __asm__ volatile ("" ::: "memory")
#    define BR_FENCE_ACQUIRE() __asm__ volatile ("" ::: "memory")
#  endif
#endif

/* ── Message types ──────────────────────────────────────────── */

/* Guest → Host (commands, written into BrowserShm.g2h)
 *
 * Each opcode has a STRICT payload length contract. The host
 * validates len before dispatch; mismatched payloads are dropped
 * with a one-line warning. Don't pad or truncate at the producer. */
#define BR_CMD_NAV            1   /* len = url length, payload = utf8 url           */
#define BR_CMD_CLICK          2   /* len = 10: i32 x, i32 y, u8 btn, u8 click_count */
#define BR_CMD_MOUSE_MOVE     3   /* len =  8: i32 x, i32 y                         */
#define BR_CMD_MOUSE_OUT      4   /* len =  0                                       */
#define BR_CMD_KEY_DOWN       5   /* len = 5 + text_len:                            */
                                   /*   u16 vk, u16 mods, u8 text_len, u8 text[]    */
#define BR_CMD_KEY_UP         6   /* len =  2: u16 vk                               */
#define BR_CMD_SCROLL         7   /* len =  8: i32 dx, i32 dy                       */
#define BR_CMD_BACK           8   /* len =  0                                       */
#define BR_CMD_FORWARD        9   /* len =  0                                       */
#define BR_CMD_RELOAD        10   /* len =  0                                       */
#define BR_CMD_GET_SELECTION 11   /* len =  0; host replies via BR_EV_SELECTION     */
#define BR_CMD_PASTE         12   /* len = 2 + text_len:                            */
                                   /*   u16 text_len, u8 text[text_len]             */
#define BR_CMD_RESIZE        13   /* len =  4: u16 w, u16 h                         */
#define BR_CMD_SELECT_ALL    14   /* len =  0                                       */
#define BR_CMD_STOP          15   /* len =  0                                       */
#define BR_CMD_ZOOM_OUT      16   /* len =  0                                       */
#define BR_CMD_ZOOM_IN       17   /* len =  0                                       */
#define BR_CMD_ZOOM_RESET    18   /* len =  0                                       */

/* Host → Guest (events, written into BrowserShm.h2g)
 *
 * Events are coalesced at the host: each is emitted only when the
 * host-owned state actually changes. Repeating an event with
 * identical payload is a host bug (it will spam the guest's
 * drain_h2g loop). */
#define BR_EV_STATUS         128   /* u8 code, u8 urllen, u8 url[urllen]            */
                                   /*   code = BR_STATUS_LOADING/READY/ERROR        */
#define BR_EV_PAGE           129   /* u32 page_w, u32 page_h, u8 tlen, u8 title[]   */
                                   /*   (legacy combined event; titles now ride     */
                                   /*    BR_EV_TITLE, dimensions BR_EV_PAGE_METRICS) */
#define BR_EV_FRAME          130   /* — (FB updated; consult fb.seq + fb.dirty)     */
#define BR_EV_DOWNLOAD       131   /* u32 guid, u8 state, u32 bytes, u32 total,     */
                                   /*   u8 plen, u8 path[plen]                      */
#define BR_EV_SELECTION      132   /* u16 text_len, u8 text[text_len]               */
#define BR_EV_PAGE_METRICS   133   /* u32 page_w, u32 page_h,                       */
                                   /*   u32 scroll_x, u32 scroll_y,                 */
                                   /*   u32 viewport_w, u32 viewport_h              */
                                   /* — page sizing for scrollbar thumb/range.      */
                                   /* Guest sets V/H scrollbar min=0,               */
                                   /* max=max(0, page_h-viewport_h),                */
                                   /* value=scroll_y. max=0 → bar inactive          */
                                   /* (Mac scrollBarProc behavior).                 */
#define BR_EV_TITLE          134   /* u8 title_len, u8 title[title_len]             */
                                   /*   utf8; guest blits to window title (truncated*/
                                   /*    + MacRoman-best-effort).                   */
#define BR_EV_HISTORY        135   /* u8 can_back, u8 can_forward                   */
                                   /*   each 0 or 1; guest enables/disables Back +  */
                                   /*   Forward toolbar buttons via HiliteControl.  */
#define BR_EV_ZOOM           136   /* u16 zoom_pct                                  */
                                   /*   percent (e.g. 100 = 100%); guest displays   */
                                   /*   on the chrome row (or stores for menu UI).  */

/* Wrap sentinel — written when the next real message wouldn't fit
 * before the end of the ring. Producer emits a normal 4-byte header
 * with type=BR_MSG_WRAP and len=0, then sets write_idx to 0 and
 * writes the real message at offset 0. Consumer that reads type=0
 * advances read_idx to 0 and re-reads from there. Sized identical
 * to a normal header so wrap handling is just one branch. */
#define BR_MSG_WRAP           0

/* BR_EV_STATUS codes */
#define BR_STATUS_LOADING     1
#define BR_STATUS_READY       2
#define BR_STATUS_ERROR       3

/* BR_EV_DOWNLOAD states */
#define BR_DL_BEGIN           1
#define BR_DL_PROGRESS        2
#define BR_DL_DONE            3
#define BR_DL_CANCELED        4
#define BR_DL_ERROR           5

/* Debug log severity levels (BrLogSlot.level). Host filters at print
 * time via BROWSER_LOG_LEVEL env var; guest emits unconditionally. */
#define BR_LOG_DBG            0
#define BR_LOG_INF            1
#define BR_LOG_WRN            2
#define BR_LOG_ERR            3

/* ── Ring buffer ────────────────────────────────────────────── */

/* Single-producer / single-consumer ring. Race-free without locks:
 * producer writes write_idx (only producer touches it), consumer
 * writes read_idx (only consumer touches it). Memory ordering is
 * release-on-write_idx (producer) and acquire-on-write_idx-read
 * (consumer). Mirrors hold for read_idx in the opposite direction.
 *
 *   Empty: write_idx == read_idx
 *   Full:  (write_idx + 4) % BR_RING_SIZE == read_idx
 *   (4 bytes reserved so empty and full are distinguishable, and
 *    every legal write_idx leaves room for at least a WRAP header.)
 *
 * Layout per message:
 *   [u16 type][u16 len][u8 payload[ALIGN4(len)]]   header = 4 bytes
 *
 * Payloads are PADDED to a 4-byte boundary in the ring (extra bytes
 * are uninitialized — consumer reads only `len` bytes). This keeps
 * write_idx u32-aligned so the wrap check is a simple compare, and
 * lets a 4-byte-wide WRAP header always fit at the tail.
 *
 * BR_MSG_WRAP is a normal 4-byte header (type=0, len=0). Producer
 * emits it whenever the next real message wouldn't fit before
 * BR_RING_SIZE; it then resets write_idx to 0 and writes the real
 * message at offset 0. Consumer that reads type=0 jumps read_idx
 * to 0 and re-reads.
 *
 * Both write_idx and read_idx are byte offsets into data[]. Always
 * stored big-endian on the wire — use br_u32_load/store accessors. */
typedef struct BrRing {
    uint32_t write_idx;             /* big-endian; producer writes only */
    uint32_t read_idx;              /* big-endian; consumer writes only */
    uint8_t  data[BR_RING_SIZE];
} BrRing;

#define BR_MSG_HDR_BYTES   4u       /* sizeof(u16 type) + sizeof(u16 len) */
#define BR_MSG_ALIGN(n)    (((n) + 3u) & ~3u)
#define BR_MSG_FRAME(len)  (BR_MSG_HDR_BYTES + BR_MSG_ALIGN(len))
#define BR_MSG_MAX_PAYLOAD (BR_RING_SIZE - BR_MSG_HDR_BYTES - 4u)

/* ── Framebuffer (snapshot, not queued) ─────────────────────── */

/* Mac-style Rect ordering (top, left, bottom, right). All fields
 * big-endian on the wire. */
typedef struct {
    int16_t top;
    int16_t left;
    int16_t bottom;
    int16_t right;
} BrRect;

/* The framebuffer is updated in-place: host writes pixel bytes
 * directly into pixels[], records the changed rectangles in dirty[],
 * bumps seq, and emits a BR_EV_FRAME event. Guest reads dirty[] and
 * CopyBits each rectangle from pixels[] into its window. */
typedef struct {
    uint32_t seq;                   /* big-endian; bumped per publish */
    uint16_t width;                 /* current usable width  (≤ BR_FB_MAX_W) */
    uint16_t height;                /* current usable height (≤ BR_FB_MAX_H) */
    uint8_t  depth;                 /* always 16 (RGB555) for now */
    uint8_t  reserved[3];
    uint16_t dirty_count;           /* big-endian; valid entries in dirty[] */
    uint16_t pad;
    BrRect   dirty[BR_DIRTY_MAX];   /* damage rects covered by this seq */
    /* pixels: top-down, rowBytes = width * BR_FB_BPP. Trailing rows
     * past `height` are unused but kept to fixed BR_FB_BYTES so the
     * struct size is layout-stable. */
    uint8_t  pixels[BR_FB_BYTES];
} BrFrameBuffer;

/* ── Debug log channel ──────────────────────────────────────── */

/* Single-slot lossy debug log. Guest writes a printf-formatted line
 * into buf[], sets len + level, release-fences, and bumps seq. The
 * host polls seq each tick (browser_spike thread today, VBL hook in
 * M3) and prints any new line to its own stderr with a
 * [BrowserGuest <level>] tag.
 *
 * Lossy by design: if the guest writes faster than the host polls,
 * intermediate lines are overwritten. The host detects gaps via seq
 * deltas and emits "(dropped N lines)" before the latest entry, so
 * losses are observable.
 *
 * Race: a partial overwrite is possible if the guest writes line N+1
 * while the host is mid-read of line N. Tolerable for a debug
 * channel — at 60 Hz polling vs typical log rates, vanishingly rare.
 * Fix later by double-buffering if needed. */
#define BR_LOG_BUFSZ        256

typedef struct BrLogSlot {
    uint32_t seq;                   /* big-endian; bumped after write (0 = no log) */
    uint16_t len;                   /* big-endian; bytes valid in buf */
    uint8_t  level;                 /* BR_LOG_DBG/INF/WRN/ERR */
    uint8_t  reserved;
    uint8_t  buf[BR_LOG_BUFSZ];
} BrLogSlot;

/* ── Top-level region ───────────────────────────────────────── */

typedef struct BrowserShm {
    uint32_t      magic;            /* BR_MAGIC; guest checks this first */
    uint32_t      version;          /* BR_VERSION */
    uint32_t      flags;            /* host capability bits, future use */
    /* Viewport top-left in Mac screen coords. Updated by the guest
     * on window-open + drag so the host's mouse poller can convert
     * mouse_screen_xy → page_xy without per-event ring traffic. */
    int16_t       viewport_screen_left;
    int16_t       viewport_screen_top;
    BrRing        h2g;              /* host → guest events */
    BrRing        g2h;              /* guest → host commands */
    BrLogSlot     log;              /* guest → host debug log (lossy) */
    BrFrameBuffer fb;
} BrowserShm;

/* Compile-time layout assertions. Both compilers (host C++ and
 * Retro68 C) must agree on these sizes or the contract is broken. */
#if defined(__cplusplus) && __cplusplus >= 201103L
#  define BR_STATIC_ASSERT(c, m) static_assert((c), m)
#elif __STDC_VERSION__ >= 201112L
#  define BR_STATIC_ASSERT(c, m) _Static_assert((c), m)
#else
   /* Fallback for older compilers (Retro68 should support _Static_assert
    * but be defensive). Generates a negative-size array on failure. */
#  define BR_STATIC_ASSERT(c, m) typedef char br_sa_##__LINE__[(c) ? 1 : -1]
#endif

BR_STATIC_ASSERT(sizeof(BrRect)        == 8,    "BrRect must be 8 bytes");
BR_STATIC_ASSERT(sizeof(BrRing)        == 8 + BR_RING_SIZE,
                 "BrRing layout drift");
BR_STATIC_ASSERT(sizeof(BrLogSlot)     == 8 + BR_LOG_BUFSZ,
                 "BrLogSlot layout drift");
BR_STATIC_ASSERT(sizeof(BrFrameBuffer) == 16 + BR_DIRTY_MAX * 8 + BR_FB_BYTES,
                 "BrFrameBuffer layout drift");
BR_STATIC_ASSERT(sizeof(BrowserShm)    == 16
                                            + 2 * (8 + BR_RING_SIZE)
                                            + (8 + BR_LOG_BUFSZ)
                                            + 16 + BR_DIRTY_MAX * 8 + BR_FB_BYTES,
                 "BrowserShm total size drift");

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* MACBROWSER_H */
