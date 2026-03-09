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
    char                  cur_app_name[32];     // CurApName (0x0910), child writes at 60Hz

    // Cursor state (child writes at 60Hz, parent reads for /api/mouse)
    std::atomic<int16_t> cursor_x, cursor_y;
    std::atomic<int16_t> raw_x, raw_y;
    std::atomic<int16_t> mtemp_x, mtemp_y;
    std::atomic<int32_t> crsr_new, crsr_couple, crsr_busy;

    char error_msg[256];

    // ── Command Queue (parent -> child -> parent) ──
    // Parent submits commands, child executes on next 60Hz tick and writes results.
    // SPSC ring buffer: parent writes cmd_queue, child reads cmd_queue and writes result_queue.

    static constexpr int CMD_QUEUE_SIZE = 16;
    static constexpr int CMD_ARG_SIZE = 256;
    static constexpr int CMD_DATA_SIZE = 4096;

    // Command types (plain ints for shared memory compatibility)
    static constexpr int32_t CMD_GET_APP_NAME    = 1;
    static constexpr int32_t CMD_GET_WINDOW_LIST = 2;
    static constexpr int32_t CMD_GET_TICKS       = 3;
    static constexpr int32_t CMD_READ_MEMORY     = 4;
    static constexpr int32_t CMD_LAUNCH_APP      = 5;
    static constexpr int32_t CMD_QUIT_APP         = 6;

    struct ShmCommand {
        uint32_t id;
        int32_t  type;           // CMD_* constant
        char     arg[CMD_ARG_SIZE];  // path for LAUNCH_APP, etc.
        uint32_t addr;           // for READ_MEMORY
        uint32_t len;            // for READ_MEMORY
    };

    struct ShmResult {
        uint32_t id;
        int32_t  done;           // 1 = complete
        int16_t  err;            // Mac OS error code (0 = noErr)
        char     data[CMD_DATA_SIZE]; // JSON or text result
    };

    ShmCommand  cmd_queue[CMD_QUEUE_SIZE];
    std::atomic<int32_t> cmd_write_pos;    // parent increments
    std::atomic<int32_t> cmd_read_pos;     // child increments

    ShmResult   result_queue[CMD_QUEUE_SIZE];
    std::atomic<int32_t> result_write_pos; // child increments
    std::atomic<int32_t> result_read_pos;  // parent increments

    std::atomic<uint32_t> cmd_next_id;     // parent increments for unique IDs

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
        cmd_write_pos.store(0, std::memory_order_relaxed);
        cmd_read_pos.store(0, std::memory_order_relaxed);
        result_write_pos.store(0, std::memory_order_relaxed);
        result_read_pos.store(0, std::memory_order_relaxed);
        cmd_next_id.store(1, std::memory_order_relaxed);
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
        memset(cur_app_name, 0, sizeof(cur_app_name));
        cursor_x.store(0); cursor_y.store(0);
        raw_x.store(0); raw_y.store(0);
        mtemp_x.store(0); mtemp_y.store(0);
        crsr_new.store(0); crsr_couple.store(0); crsr_busy.store(0);
        memset(error_msg, 0, sizeof(error_msg));
        cmd_write_pos.store(0, std::memory_order_relaxed);
        cmd_read_pos.store(0, std::memory_order_relaxed);
        result_write_pos.store(0, std::memory_order_relaxed);
        result_read_pos.store(0, std::memory_order_relaxed);
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

// ── Command queue helpers (parent side) ──

// Submit a command to the child. Returns command ID for matching results.
inline uint32_t shared_cmd_submit(SharedState* shm, int32_t type,
                                   const char* arg = nullptr,
                                   uint32_t addr = 0, uint32_t len = 0) {
    uint32_t id = shm->cmd_next_id.fetch_add(1, std::memory_order_relaxed);
    int32_t pos = shm->cmd_write_pos.load(std::memory_order_relaxed);
    auto& cmd = shm->cmd_queue[pos & (SharedState::CMD_QUEUE_SIZE - 1)];
    cmd.id = id;
    cmd.type = type;
    cmd.addr = addr;
    cmd.len = len;
    if (arg) {
        strncpy(cmd.arg, arg, SharedState::CMD_ARG_SIZE - 1);
        cmd.arg[SharedState::CMD_ARG_SIZE - 1] = '\0';
    } else {
        cmd.arg[0] = '\0';
    }
    shm->cmd_write_pos.store(pos + 1, std::memory_order_release);
    return id;
}

// Poll for a result with the given ID. Returns true if found.
// Consumes all results up to and including the matching one.
inline bool shared_cmd_poll(SharedState* shm, uint32_t id,
                             int16_t* err, char* data, int data_size) {
    int32_t read_pos = shm->result_read_pos.load(std::memory_order_relaxed);
    int32_t write_pos = shm->result_write_pos.load(std::memory_order_acquire);

    while (read_pos != write_pos) {
        auto& res = shm->result_queue[read_pos & (SharedState::CMD_QUEUE_SIZE - 1)];
        if (res.id == id && res.done) {
            if (err) *err = res.err;
            if (data && data_size > 0) {
                strncpy(data, res.data, data_size - 1);
                data[data_size - 1] = '\0';
            }
            shm->result_read_pos.store(read_pos + 1, std::memory_order_release);
            return true;
        }
        read_pos++;
    }
    return false;
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
