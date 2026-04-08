/*
 * IPC Protocol for mac-phoenix subprocess mode
 *
 * Used when PPC runs as a subprocess (--ipc mode).
 * Child creates SHM + Unix socket; parent connects by PID.
 *
 * SHM: /macemu-video-{PID}  (triple-buffered video + boot status)
 * Socket: /tmp/macemu-{PID}.sock  (binary input: key/mouse/command)
 * Eventfd: sent via SCM_RIGHTS for zero-latency frame notifications
 *
 * Video frames stored as packed pixels (no stride padding).
 */

#ifndef MAC_PHOENIX_IPC_PROTOCOL_H
#define MAC_PHOENIX_IPC_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifdef __cplusplus
#include <atomic>
#define IPC_ATOMIC_UINT32 std::atomic<uint32_t>
#define IPC_ATOMIC_UINT64 std::atomic<uint64_t>
#define IPC_ATOMIC_LOAD(ptr) (ptr).load(std::memory_order_acquire)
#define IPC_ATOMIC_STORE(ptr, val) (ptr).store(val, std::memory_order_release)
#else
#include <stdatomic.h>
#define IPC_ATOMIC_UINT32 _Atomic uint32_t
#define IPC_ATOMIC_UINT64 _Atomic uint64_t
#define IPC_ATOMIC_LOAD(ptr) atomic_load_explicit(&(ptr), memory_order_acquire)
#define IPC_ATOMIC_STORE(ptr, val) atomic_store_explicit(&(ptr), val, memory_order_release)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ── Resource naming ─────────────────────────────────────────────── */

#define IPC_VIDEO_SHM_PREFIX "/macemu-video-"
#define IPC_CONTROL_SOCK_PREFIX "/tmp/macemu-"
#define IPC_CONTROL_SOCK_SUFFIX ".sock"

/* ── Constants ───────────────────────────────────────────────────── */

#define IPC_VIDEO_MAGIC 0x4D504850  /* "MPHP" */
#define IPC_VERSION 1

#define IPC_MAX_WIDTH  1920
#define IPC_MAX_HEIGHT 1080
#define IPC_FRAME_SIZE (IPC_MAX_WIDTH * IPC_MAX_HEIGHT * 4)
#define IPC_NUM_BUFFERS 3

#define IPC_STATE_STOPPED   0
#define IPC_STATE_RUNNING   1

#define IPC_PIXFMT_ARGB     0   /* Mac native: bytes A,R,G,B */
#define IPC_PIXFMT_BGRA     1   /* Converted: bytes B,G,R,A  */

/* ── Binary input protocol (parent → child over Unix socket) ───── */

#define IPC_INPUT_KEY       1
#define IPC_INPUT_MOUSE     2
#define IPC_INPUT_COMMAND   3

#define IPC_KEY_DOWN        0x01
#define IPC_KEY_UP          0x00

#define IPC_MOUSE_LEFT      0x01
#define IPC_MOUSE_RIGHT     0x02
#define IPC_MOUSE_ABSOLUTE  0x10

#define IPC_CMD_STOP        2
#define IPC_CMD_RESET       3
#define IPC_CMD_INVOKE_DEBUG 4

#define IPC_INPUT_AUDIO_REQUEST 4

/* ── Audio constants ────────────────────────────────────────────── */

#define IPC_AUDIO_FRAME_RING_SIZE  4      /* 4 frames = 80ms buffer */
#define IPC_AUDIO_MAX_FRAME_BYTES  3840   /* 48kHz * 2ch * 2bytes * 20ms */

typedef struct {
    uint8_t type;           /* IPC_INPUT_* */
    uint8_t flags;
    uint16_t _reserved;
} IPCInputHeader;           /* 4 bytes */

typedef struct {
    IPCInputHeader hdr;
    uint8_t mac_keycode;
    uint8_t modifiers;
    uint16_t _reserved;
} IPCKeyInput;              /* 8 bytes */

typedef struct {
    IPCInputHeader hdr;
    int16_t x, y;           /* absolute coord or relative delta */
    uint8_t buttons;        /* IPC_MOUSE_LEFT | IPC_MOUSE_RIGHT */
    uint8_t _reserved[3];
} IPCMouseInput;            /* 12 bytes */

typedef struct {
    IPCInputHeader hdr;
    uint8_t command;        /* IPC_CMD_* */
    uint8_t _reserved[3];
} IPCCommandInput;          /* 8 bytes */

typedef struct {
    IPCInputHeader hdr;     /* type = IPC_INPUT_AUDIO_REQUEST */
    uint32_t requested_samples;
} IPCAudioRequestInput;     /* 8 bytes */

typedef union {
    IPCInputHeader hdr;
    IPCKeyInput key;
    IPCMouseInput mouse;
    IPCCommandInput cmd;
    IPCAudioRequestInput audio_request;
} IPCInput;

/* ── Audio frame (child writes to SHM ring buffer) ──────────────── */

typedef struct {
    uint32_t sample_rate;       /* 11025/22050/44100/48000 */
    uint32_t channels;          /* 1=mono, 2=stereo */
    uint32_t samples;           /* actual sample count per channel */
    uint32_t format;            /* 1=PCM_S16 */
    uint8_t  data[IPC_AUDIO_MAX_FRAME_BYTES];  /* raw S16MSB (big-endian) */
} IPCAudioFrame;

/* ── Shared memory buffer ────────────────────────────────────────
 *
 * Triple-buffered video frames (packed pixels, no stride padding),
 * boot progress, and cursor state. Child owns; parent maps read-only
 * (except cursor fields which parent doesn't write).
 *
 * Total size: ~24.9 MB (dominated by 3 × 1920×1080×4 frame buffers).
 */
typedef struct IPCBuffer {
    /* Header (validated by parent on connect) */
    uint32_t magic;
    uint32_t version;
    uint32_t pid;
    uint32_t state;                  /* IPC_STATE_* */

    /* Frame dimensions (actual, ≤ IPC_MAX_*) */
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;           /* IPC_PIXFMT_* */
    uint32_t _reserved;

    /* Triple buffer indices (atomic for cross-process safety) */
    IPC_ATOMIC_UINT32 write_index;   /* child writes here (0–2) */
    IPC_ATOMIC_UINT32 ready_index;   /* parent reads here (0–2) */
    IPC_ATOMIC_UINT64 frame_count;   /* monotonic, for new-frame detection */
    uint64_t timestamp_us;

    /* Frame notification (sent to parent via SCM_RIGHTS) */
    int32_t frame_ready_eventfd;
    int32_t _eventfd_pad;

    /* Boot progress (child writes, parent reads for /api/status) */
    char boot_phase[32];
    IPC_ATOMIC_UINT32 checkload_count;
    IPC_ATOMIC_UINT64 boot_start_us; /* CLOCK_MONOTONIC µs */

    /* Current app name (child writes, parent reads for /api/app) */
    char cur_app_name[32];

    /* Cursor state (child writes, parent reads for /api/mouse) */
    IPC_ATOMIC_UINT32 shm_cursor_x, shm_cursor_y;
    IPC_ATOMIC_UINT32 shm_raw_x,    shm_raw_y;
    IPC_ATOMIC_UINT32 shm_mtemp_x,  shm_mtemp_y;
    IPC_ATOMIC_UINT32 shm_crsr_new,  shm_crsr_couple, shm_crsr_busy;

    /* Mac OS state snapshot (child writes, parent reads for /api/status).
     * Pre-serialized JSON with windows, ticks, menu_bar.
     * Updated every ~500ms from the 60Hz tick thread. */
    char mac_state_json[2048];

    /* ── Audio ring buffer (child writes, parent reads) ──────────── */
    IPC_ATOMIC_UINT32 audio_write_idx;   /* child increments after writing frame */
    IPC_ATOMIC_UINT32 audio_read_idx;    /* parent increments after reading frame */
    IPCAudioFrame audio_frames[IPC_AUDIO_FRAME_RING_SIZE];

    /* Frame buffers — packed pixels, width×height×4 bytes per frame.
     * Allocated for max resolution; only width×height bytes are valid. */
    uint8_t frames[IPC_NUM_BUFFERS][IPC_FRAME_SIZE];
} IPCBuffer;

/* ── Helpers ─────────────────────────────────────────────────────── */

/* Called by child after writing a frame to frames[write_index] */
static inline void ipc_frame_complete(IPCBuffer* buf, uint64_t ts_us) {
    uint32_t cur = IPC_ATOMIC_LOAD(buf->write_index);
    buf->timestamp_us = ts_us;
    IPC_ATOMIC_STORE(buf->ready_index, cur);
    IPC_ATOMIC_STORE(buf->write_index, (cur + 1) % IPC_NUM_BUFFERS);
    IPC_ATOMIC_STORE(buf->frame_count, IPC_ATOMIC_LOAD(buf->frame_count) + 1);

    if (buf->frame_ready_eventfd >= 0) {
        uint64_t val = 1;
        ssize_t ignored = write(buf->frame_ready_eventfd, &val, sizeof(val));
        (void)ignored;
    }
}

/* Called by child at startup */
static inline void ipc_init_buffer(IPCBuffer* buf, uint32_t pid,
                                    uint32_t width, uint32_t height) {
    /* Zero everything (atomics re-initialized below) */
    memset((void*)buf, 0, sizeof(IPCBuffer));

    buf->magic = IPC_VIDEO_MAGIC;
    buf->version = IPC_VERSION;
    buf->pid = pid;
    buf->state = IPC_STATE_STOPPED;
    buf->width = width;
    buf->height = height;
    buf->pixel_format = IPC_PIXFMT_ARGB;

    buf->frame_ready_eventfd = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE);
    if (buf->frame_ready_eventfd < 0)
        fprintf(stderr, "IPC: eventfd failed: %s\n", strerror(errno));

    strncpy(buf->boot_phase, "pre-reset", sizeof(buf->boot_phase));
    IPC_ATOMIC_STORE(buf->checkload_count, 0);
    IPC_ATOMIC_STORE(buf->boot_start_us, 0);
}

/* Called by parent on connect */
static inline int ipc_validate_buffer(const IPCBuffer* buf, uint32_t expected_pid) {
    if (buf->magic != IPC_VIDEO_MAGIC) return -1;
    if (buf->version != IPC_VERSION)   return -2;
    if (buf->pid != expected_pid)      return -3;
    if (buf->width > IPC_MAX_WIDTH || buf->height > IPC_MAX_HEIGHT) return -4;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* MAC_PHOENIX_IPC_PROTOCOL_H */
