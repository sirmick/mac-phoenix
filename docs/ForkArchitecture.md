# Fork-Based CPU Process Architecture

## Overview

The CPU runs as a child process, forked from the parent server process. This gives automatic cleanup on stop (OS reclaims all memory, file handles, global state) and eliminates the impossible task of manually resetting ~250 global variables in the UAE interpreter.

```
Parent Process (persistent)              Child Process (ephemeral)
┌──────────────────────────────┐        ┌──────────────────────────────┐
│  HTTP server                 │        │  init_m68k()                 │
│  WebRTC signaling            │        │  cpu_execute_fast()          │
│  Video encoder thread        │        │  60Hz timer interrupt        │
│  Audio encoder thread        │        │  Disk/CDROM/SCSI I/O         │
│  API handlers                │        │  ROM patches & EmulOps       │
│                              │        │  ADB input polling           │
│  Reads: video, audio, status │        │  Writes: video, audio, status│
│  Writes: input events        │        │  Reads: input events         │
└──────────┬───────────────────┘        └──────────┬───────────────────┘
           │                                       │
           └───────── shared memory (mmap) ────────┘
```

## Lifecycle

```
User clicks Start:
  1. Parent serializes EmulatorConfig to shared memory
  2. Parent calls fork()
  3. Child: close inherited server sockets
  4. Child: deserialize config, call init_m68k(), cpu_execute_fast()
  5. Child: video_refresh() writes frames to shared memory
  6. Parent: encoder threads read frames from shared memory

User clicks Stop:
  1. Parent calls kill(child_pid, SIGKILL)
  2. Parent calls waitpid(child_pid)
  3. OS frees all child memory, file handles, globals — zero leaked state
  4. Shared memory persists (parent still holds mapping)

User clicks Reset:
  1. Stop (kill + waitpid)
  2. Start (fork again — fresh process, clean state)

Headless mode:
  No fork. init_m68k() + cpu_execute_fast() in the main process, same as today.
```

## Shared Memory Layout

Single `mmap(MAP_SHARED | MAP_ANONYMOUS)` region, inherited by child via fork. ~25MB total.

Based on the proven layout from `legacy/BasiliskII/src/IPC/ipc_protocol.h`, adapted for in-process fork (no PID discovery, no named SHM).

```c
struct SharedState {
    // ── Video: Triple-Buffered Frames (~24.9MB) ──
    //
    // Same triple-buffer protocol as current VideoOutput:
    // Child writes to buffers[write_index], publishes via ready_index.
    // Parent encoder reads from buffers[ready_index].
    //
    struct VideoFrame {
        uint8_t  pixels[1920 * 1080 * 4];  // BGRA, 8.3MB per buffer
        int32_t  width;
        int32_t  height;
        int32_t  pixel_format;              // ARGB or BGRA
        uint64_t sequence;                  // monotonic frame counter
        uint64_t timestamp_us;
    } video_buffers[3];

    _Atomic int32_t  video_write_index;     // child writes (0-2)
    _Atomic int32_t  video_ready_index;     // last completed frame
    int32_t          video_eventfd;         // eventfd, wakes encoder

    // ── Audio: Frame Ring Buffer (~12KB) ──
    //
    // SPSC ring buffer. Child produces 20ms frames, parent consumes.
    // No mutex — lock-free via atomic indices.
    //
    struct AudioFrame {
        int16_t  samples[960 * 2];          // 20ms @ 48kHz stereo = 3840 bytes
        uint32_t sample_rate;
        uint32_t channels;
        uint32_t num_samples;               // actual samples in this frame
        uint64_t timestamp_us;
    } audio_ring[4];                        // 4 frames = 80ms buffer

    _Atomic int32_t  audio_write_idx;
    _Atomic int32_t  audio_read_idx;
    int32_t          audio_eventfd;         // eventfd, wakes audio encoder

    // ── Input: Event Queue (~4KB) ──
    //
    // Parent writes mouse/keyboard events from WebRTC.
    // Child polls during 60Hz timer interrupt (ADBPollSharedInput).
    //
    struct InputEvent {
        uint8_t  type;                      // key, mouse_rel, mouse_abs, mouse_button, mouse_mode
        uint8_t  flags;
        int16_t  x, y;                      // position or delta
        uint8_t  keycode;
        uint8_t  buttons;
        uint16_t _pad;
    } input_queue[256];

    _Atomic int32_t  input_write_pos;       // parent writes
    _Atomic int32_t  input_read_pos;        // child reads

    // ── Status (child -> parent) ──
    //
    // Polled by /api/status. Written by child's boot_progress tracker.
    //
    _Atomic int32_t  child_state;           // 0=starting, 1=running, 2=stopped, 3=error
    _Atomic int32_t  boot_phase;            // BootPhase enum
    _Atomic uint32_t checkload_count;
    _Atomic int64_t  boot_start_us;

    // Mouse position (child reads Mac low-memory globals, writes here)
    _Atomic int16_t  mouse_x, mouse_y;
    _Atomic int16_t  raw_x, raw_y;
    _Atomic int16_t  mtemp_x, mtemp_y;

    char             error_msg[256];        // if child_state == error

    // ── Config Snapshot ──
    //
    // Parent serializes config JSON here before fork.
    // Child deserializes at startup.
    //
    char    config_json[8192];
    int32_t config_json_len;
};
```

## IPC Mechanisms

| Data | Mechanism | Direction | Why |
|------|-----------|-----------|-----|
| Framebuffer | Shared mmap triple buffer + eventfd | Child -> Parent | Zero-copy, same algorithm as current VideoOutput |
| Audio | Shared mmap SPSC ring + eventfd | Child -> Parent | Lock-free, eventfd wakes encoder without polling |
| Input events | Shared mmap ring buffer | Parent -> Child | Zero-syscall, child polls at 60Hz in timer interrupt |
| Boot status | Shared mmap atomics | Child -> Parent | Polled by /api/status every ~1s |
| Config | Serialized JSON in shared mem | Parent -> Child | Read once at fork, no ongoing sync |
| Start | fork() | Parent -> Child | Child begins init immediately |
| Stop | kill(SIGKILL) + waitpid() | Parent -> Child | Instant, OS cleans everything |
| Child crash | SIGCHLD + waitpid(WNOHANG) | Child -> Parent | Detect unexpected deaths |

### Why eventfd (not polling)?

The legacy code used eventfd and it works well:
- `write(eventfd, &val, 8)` from child after frame complete
- Parent encoder uses `epoll_wait(eventfd)` — sleeps until frame ready
- Kernel provides memory barrier: all writes before `write(eventfd)` are visible after parent's `read(eventfd)`
- Replaces both the condition variable (current video) and mutex (current audio)

### Why shared mmap ring (not pipe) for input?

Input events are tiny (~12 bytes) at human rates (~100/sec). A pipe would work but adds syscall overhead per event. The shared memory ring is zero-syscall — the child polls it during the existing 60Hz ADB interrupt handler.

## File Changes

### New Files

| File | Purpose |
|------|---------|
| `src/core/shared_state.h` | SharedState struct (POD, C-compatible), mmap helpers |
| `src/core/cpu_process.cpp` | fork/kill/waitpid logic, `cpu_child_main()` |
| `src/drivers/video/video_shm.cpp` | Child-side video driver: copies Mac framebuffer to shared memory |
| `src/drivers/audio/audio_shm.cpp` | Child-side audio driver: writes samples to shared ring buffer |

### Modified Files

| File | Change |
|------|--------|
| `src/main.cpp` | Replace CPU thread with fork-based process management. Create shared memory before starting server threads. |
| `src/core/adb.cpp` | Add `ADBPollSharedInput(SharedState*)` — drain input queue during 60Hz interrupt |
| `src/core/boot_progress.cpp` | Write boot phase/checkload to shared memory (if SharedState* is set) |
| `src/webrtc/webrtc_server.cpp` | `process_input_message()` writes to shared memory input queue instead of calling ADB directly |
| `src/webserver/api_handlers.cpp` | Start/Stop/Reset use fork/kill. Status reads from shared memory. Mouse position reads from shared memory. |
| `src/drivers/platform/timer_interrupt.cpp` | Call `ADBPollSharedInput()` at start of 60Hz handler |
| `src/drivers/video/video_output.h` | Add constructor accepting external shared-memory pixel buffers |

### Unchanged

| File | Why |
|------|-----|
| `src/core/cpu_context.cpp` | Called by child process — same init_m68k() path, no changes needed |
| `src/core/emulator_init.cpp` | Called by child process — same init_mac_subsystems() path |
| `src/config/emulator_config.cpp` | Config serialized to shared mem by parent, deserialized by child |
| All UAE interpreter files | Run in child process, never need cleanup — OS handles it |

## Video Encoder Integration

The parent's video encoder thread currently reads from `VideoOutput` (in-process triple buffer). With fork, it reads from `SharedState::video_buffers[]` instead.

Two options:
1. **New SharedVideoOutput class** wrapping shared memory — encoder code unchanged
2. **Modify VideoOutput** to accept external memory pointers — constructor takes `SharedState*`

Option 2 is simpler. Add a `VideoOutput(SharedState* shm)` constructor that points the internal buffer pointers at shared memory instead of allocating.

The child process creates a video driver (`video_shm.cpp`) that:
- Allocates the Mac framebuffer in child heap (as today)
- In `video_refresh()`, converts Mac framebuffer to BGRA and writes to `shm->video_buffers[write_index]`
- Calls `write(shm->video_eventfd, ...)` to wake parent encoder

## Headless Mode

No fork. The current architecture stays: `init_m68k()` in main thread, `cpu_execute_fast()` blocks until timeout. No shared memory needed. Selected by `--no-webserver` flag.

## Edge Cases

**Child crashes**: Parent installs `SIGCHLD` handler. On child death, `waitpid()` reaps zombie, sets status to error. Web UI shows error state.

**Fork safety**: Parent's HTTP and WebRTC threads are already running when fork happens. This is fine — `fork()` only duplicates the calling thread. Child must close inherited server sockets immediately after fork (or parent opens them with `SOCK_CLOEXEC`).

**Disk I/O in child**: Disk files are opened by `DiskInit()`/`CDROMInit()` which run in the child after fork. No file handle sharing issues.

**Multiple starts without stop**: Parent must check if child is already running and reject duplicate starts, or kill the existing child first.

**Config changes during run**: Config changes via POST /api/config update the parent's `EmulatorConfig`. They take effect on next Start (next fork). No live config changes to running child.

## Implementation Order

1. `shared_state.h` — struct definition, mmap create/destroy
2. `cpu_process.cpp` — fork/kill lifecycle, child main function
3. `video_shm.cpp` — child-side video driver writing to shared memory
4. `audio_shm.cpp` — child-side audio driver writing to shared ring
5. Modify `adb.cpp` — add shared input polling
6. Modify `boot_progress.cpp` — shared memory status writes
7. Modify `webrtc_server.cpp` — input queue writes
8. Modify `api_handlers.cpp` — fork/kill for start/stop, status from shared mem
9. Modify `main.cpp` — orchestration: create shm, wire everything up
10. Modify `video_output.h` — shared memory constructor for parent encoder
