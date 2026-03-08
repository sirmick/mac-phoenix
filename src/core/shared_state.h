/*
 * shared_state.h - Shared memory layout for fork-based CPU process
 *
 * Single mmap(MAP_SHARED | MAP_ANONYMOUS) region, inherited by child via fork.
 * ~25MB total. Parent and child communicate through this struct.
 *
 * Video: triple-buffered frames (child writes, parent reads via relay)
 * Input: ring buffer (parent writes, child polls at 60Hz)
 * Status: boot phase, cursor state (child writes, parent reads)
 * Config: serialized JSON (parent writes before fork, child reads at startup)
 */

#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <stdint.h>
#include <string.h>
#include <atomic>
#include <sys/mman.h>

// Video constants
#define SHM_VIDEO_MAX_WIDTH   1920
#define SHM_VIDEO_MAX_HEIGHT  1080
#define SHM_VIDEO_NUM_BUFFERS 3

// Input event types (match WebRTC binary protocol)
#define SHM_INPUT_MOUSE_REL    1
#define SHM_INPUT_MOUSE_BUTTON 2
#define SHM_INPUT_KEY          3
#define SHM_INPUT_MOUSE_ABS    5
#define SHM_INPUT_MOUSE_MODE   6

// Child process state
#define SHM_STATE_STOPPED  0
#define SHM_STATE_STARTING 1
#define SHM_STATE_RUNNING  2
#define SHM_STATE_ERROR    3

struct SharedState {
    // ── Video: Triple-Buffered Frames (~24.9MB) ──
    struct VideoBuffer {
        uint8_t  pixels[SHM_VIDEO_MAX_WIDTH * SHM_VIDEO_MAX_HEIGHT * 4];
        int32_t  width;
        int32_t  height;
        int32_t  pixel_format;   // 0=ARGB, 1=BGRA
        uint64_t sequence;       // monotonic frame counter
        uint64_t timestamp_us;
    } video_buffers[SHM_VIDEO_NUM_BUFFERS];

    std::atomic<int32_t>  video_write_index;
    std::atomic<int32_t>  video_ready_index;
    std::atomic<uint64_t> video_frame_count;

    // ── Input: Event Queue (~2.5KB) ──
    struct InputEvent {
        uint8_t  type;       // SHM_INPUT_* constant
        uint8_t  flags;      // down=1/up=0 for keys/buttons; relative=1 for mode
        int16_t  x, y;      // position or delta
        uint8_t  keycode;   // Mac keycode or button number
        uint8_t  _pad[3];
    } input_queue[256];

    std::atomic<int32_t> input_write_pos;   // parent increments
    std::atomic<int32_t> input_read_pos;    // child increments

    // ── Status (child -> parent) ──
    std::atomic<int32_t>  child_state;          // SHM_STATE_* constant
    std::atomic<uint32_t> checkload_count;
    std::atomic<int64_t>  boot_start_us;        // CLOCK_MONOTONIC microseconds
    char                  boot_phase_name[32];  // "pre-reset", "Finder", etc.

    // Cursor state (child writes at 60Hz, parent reads for /api/mouse)
    std::atomic<int16_t> cursor_x, cursor_y;
    std::atomic<int16_t> raw_x, raw_y;
    std::atomic<int16_t> mtemp_x, mtemp_y;
    std::atomic<int32_t> crsr_new, crsr_couple, crsr_busy;

    char error_msg[256];

    // ── Config Snapshot ──
    char    config_json[16384];
    int32_t config_json_len;

    // ── Initialization ──
    void init() {
        memset(this, 0, sizeof(*this));
        // Re-initialize atomics (memset zeroed the underlying storage)
        video_write_index.store(0, std::memory_order_relaxed);
        video_ready_index.store(0, std::memory_order_relaxed);
        video_frame_count.store(0, std::memory_order_relaxed);
        input_write_pos.store(0, std::memory_order_relaxed);
        input_read_pos.store(0, std::memory_order_relaxed);
        child_state.store(SHM_STATE_STOPPED, std::memory_order_relaxed);
        checkload_count.store(0, std::memory_order_relaxed);
        boot_start_us.store(0, std::memory_order_relaxed);
        cursor_x.store(0); cursor_y.store(0);
        raw_x.store(0); raw_y.store(0);
        mtemp_x.store(0); mtemp_y.store(0);
        crsr_new.store(0); crsr_couple.store(0); crsr_busy.store(0);
        strncpy(boot_phase_name, "pre-reset", sizeof(boot_phase_name));
    }

    // Reset for new child start (keep config, clear runtime state)
    void reset_runtime() {
        for (auto& vb : video_buffers) {
            vb.width = 0;
            vb.height = 0;
            vb.sequence = 0;
        }
        video_write_index.store(0, std::memory_order_relaxed);
        video_ready_index.store(0, std::memory_order_relaxed);
        video_frame_count.store(0, std::memory_order_relaxed);
        input_read_pos.store(input_write_pos.load(std::memory_order_relaxed),
                             std::memory_order_relaxed);
        child_state.store(SHM_STATE_STARTING, std::memory_order_relaxed);
        checkload_count.store(0, std::memory_order_relaxed);
        boot_start_us.store(0, std::memory_order_relaxed);
        strncpy(boot_phase_name, "pre-reset", sizeof(boot_phase_name));
        cursor_x.store(0); cursor_y.store(0);
        raw_x.store(0); raw_y.store(0);
        mtemp_x.store(0); mtemp_y.store(0);
        crsr_new.store(0); crsr_couple.store(0); crsr_busy.store(0);
        memset(error_msg, 0, sizeof(error_msg));
    }
};

// ── Create / Destroy ──

inline SharedState* create_shared_state() {
    void* mem = mmap(nullptr, sizeof(SharedState),
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) return nullptr;
    SharedState* shm = reinterpret_cast<SharedState*>(mem);
    shm->init();
    return shm;
}

inline void destroy_shared_state(SharedState* shm) {
    if (shm) munmap(shm, sizeof(SharedState));
}

// ── Input helpers (parent side) ──

inline void shared_input_push(SharedState* shm, uint8_t type, uint8_t flags,
                               int16_t x, int16_t y, uint8_t keycode) {
    int32_t pos = shm->input_write_pos.load(std::memory_order_relaxed);
    auto& ev = shm->input_queue[pos & 0xFF];
    ev.type = type;
    ev.flags = flags;
    ev.x = x;
    ev.y = y;
    ev.keycode = keycode;
    shm->input_write_pos.store(pos + 1, std::memory_order_release);
}

#endif // SHARED_STATE_H
