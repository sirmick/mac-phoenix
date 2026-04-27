/*
 *  MacBrowser.h — shared layout for the BrowserShm region.
 *
 *  Mapped at guest address 0x02520000 (~1.7 MiB). Both mac-phoenix
 *  (host) and Browser.app (Retro68 m68k guest) include this header so
 *  the layout stays in lockstep.
 *
 *  Synchronization is the existing 60 Hz VBL: host drains pending
 *  events into the h2g ring before firing each VBL; guest's VBL task
 *  drains them, queues commands the other way. No locks on the guest
 *  side; one mutex on the host.
 *
 *  Wire format: classic Mac is big-endian; host is typically
 *  little-endian. ALL multi-byte fields in this region are stored
 *  big-endian on the wire. Use br_u16/u32_load/store accessors. NEVER
 *  load or store a multi-byte field with a raw pointer dereference.
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
#define BR_VERSION        1u

/* Guest-visible Mac address. Sits past the existing FrameBuffer
 * region (0x02110000 + 4 MiB = 0x02510000) with a small pad. */
#define BR_BASE_ADDR      0x02520000u

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

/* Guest → Host (commands, written into BrowserShm.g2h) */
#define BR_CMD_NAV            1   /* utf8 url                                       */
#define BR_CMD_CLICK          2   /* i32 page_x, i32 page_y, u8 btn, u8 click_count */
#define BR_CMD_MOUSE_MOVE     3   /* i32 page_x, i32 page_y                         */
#define BR_CMD_MOUSE_OUT      4   /* —                                              */
#define BR_CMD_KEY_DOWN       5   /* u16 vk, u16 mods, u8 text_len, u8 text[]       */
#define BR_CMD_KEY_UP         6   /* u16 vk                                         */
#define BR_CMD_SCROLL         7   /* i32 dx, i32 dy                                 */
#define BR_CMD_BACK           8   /* —                                              */
#define BR_CMD_FORWARD        9   /* —                                              */
#define BR_CMD_RELOAD        10   /* —                                              */
#define BR_CMD_GET_SELECTION 11   /* — (server replies with BR_EV_SELECTION)        */
#define BR_CMD_PASTE         12   /* u16 len, u8 text[len] — insert at focused el   */
#define BR_CMD_RESIZE        13   /* u16 w, u16 h — guest viewport size changed     */

/* Host → Guest (events, written into BrowserShm.h2g) */
#define BR_EV_STATUS        128   /* u8 code, u8 msglen, u8 msg[msglen]             */
#define BR_EV_PAGE          129   /* u32 page_w, u32 page_h, u8 tlen, u8 title[]    */
#define BR_EV_FRAME         130   /* — (FB updated; consult fb.seq + fb.dirty)      */
#define BR_EV_DOWNLOAD      131   /* u32 guid, u8 state, u32 bytes, u32 total,      */
                                  /*   u8 plen, u8 path[plen]                       */
#define BR_EV_SELECTION     132   /* u16 len, u8 text[len]                          */

/* Sentinel — written into either ring when a message would straddle
 * the buffer end. Consumer interprets type=0 as "jump read_idx to 0
 * and re-read from there." Carries no length. */
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

/* ── Ring buffer ────────────────────────────────────────────── */

/* Single-producer / single-consumer ring. Producer writes write_idx
 * (with release fence after data writes); consumer writes read_idx
 * (with acquire fence before data reads).
 *
 * Empty:    write_idx == read_idx
 * Full:     (write_idx + 1) % BR_RING_SIZE == read_idx
 * Capacity: BR_RING_SIZE - 1 bytes (one slot reserved to disambiguate)
 *
 * Each message is laid out as:
 *   [u16 type][u16 len][u8 payload[len]]   (header = 4 bytes)
 *
 * BR_MSG_WRAP carries no length — it's a single u16 that tells the
 * reader to jump back to offset 0 in data[]. Producer emits it when
 * 4+len wouldn't fit before BR_RING_SIZE.
 *
 * Both write_idx and read_idx are byte offsets into data[]. */
typedef struct {
    uint32_t write_idx;             /* big-endian; producer writes only */
    uint32_t read_idx;              /* big-endian; consumer writes only */
    uint8_t  data[BR_RING_SIZE];
} BrRing;

#define BR_MSG_HDR_BYTES   4u       /* sizeof(u16 type) + sizeof(u16 len) */
#define BR_MSG_MAX_PAYLOAD (BR_RING_SIZE - BR_MSG_HDR_BYTES - 1u)

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

/* ── Top-level region ───────────────────────────────────────── */

typedef struct {
    uint32_t      magic;            /* BR_MAGIC; guest checks this first */
    uint32_t      version;          /* BR_VERSION */
    uint32_t      flags;            /* host capability bits, future use */
    uint32_t      reserved;
    BrRing        h2g;              /* host → guest events */
    BrRing        g2h;              /* guest → host commands */
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
BR_STATIC_ASSERT(sizeof(BrFrameBuffer) == 16 + BR_DIRTY_MAX * 8 + BR_FB_BYTES,
                 "BrFrameBuffer layout drift");
BR_STATIC_ASSERT(sizeof(BrowserShm)    == 16 + 2 * (8 + BR_RING_SIZE)
                                            + 16 + BR_DIRTY_MAX * 8 + BR_FB_BYTES,
                 "BrowserShm total size drift");

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* MACBROWSER_H */
