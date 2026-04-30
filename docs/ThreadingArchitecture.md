# Threading & process model

Two processes, four threads in the parent, one CPU thread in the child.

## Topology

```
parent (mac-phoenix)
├── Thread 1: WEB SERVER       — HTTP listener on --port, /ws upgrade,
│                                 WebRTC signaling, REST API, file scanner
├── Thread 2: VIDEO ENCODER    — reads IPC SHM frame, encodes
│                                 H.264 / VP9 / WebP / PNG, ships via
│                                 RTP (libdatachannel) or /ws binary
├── Thread 3: VIDEO RELAY      — epoll on eventfd, copies SHM frame into
│                                 VideoOutput triple buffer for screenshots
├── Thread 4: BRIDGE WATCHDOG  — waits for Finder + BridgeAgent heartbeat,
│                                 logs warnings if missing
└── (MacBrowser supervisor + Xvfb/Firefox children if --browser)

CPU subprocess (mac-phoenix --ipc)
└── CPU/MAIN thread            — runs UAE / Unicorn / KPX / DualCPU
                                  inside one process; shared SHM with parent

PPC also has:
  - PPC tick thread            — 60 Hz IRQ generator (nanosleep)
  - PRECISE_TIMING thread       — virtual-clock PrimeTime scheduling
```

`--no-webserver` collapses to a single process; the bridge still uses
files because the agent inside the guest is a separate Mac OS process.

## Why two processes

- **Crash isolation.** A backend SIGSEGV inside JIT code can't take down
  the HTTP listener. The parent reaps the child and the user sees an
  error in the UI instead of a dead port.
- **Separate address spaces.** Backends `mmap` huge regions
  (32 MB to 512 MB depending on machine profile). The parent doesn't
  carry that footprint when it's just serving HTTP.
- **Forking after stack setup is fragile.** Doing all backend init in a
  child means the parent's memory layout stays clean.

## IPC

- **SHM (`/tmp/mac-phoenix-<pid>`)** — framebuffer, mouse position, boot
  phase, `cur_app_name`, audio (when enabled), `command_bridge` mailbox
  for backward compat (passive read-only on the parent side).
- **Unix socket** — input events from `/ws` to the CPU thread, lifecycle
  control, codec / mode-switch requests.
- **eventfd** — wakes the video relay thread on each new frame.
- **Disk files** in `cfg.bridge_dir` — the BridgeAgent transport
  (`_bridge_cmd`, `_bridge_result`, `bridge_heartbeat`,
  `_bridge_clipboard`). Both processes see the same path.

## Inter-thread channels (parent)

- **VideoOutput triple buffer** — lock-free SPSC via two `std::atomic<int>`
  indices. CPU thread writes (via the relay or direct in `--no-webserver`),
  encoder reads, screenshot API reads. Frames may be silently dropped
  when the encoder is behind — intentional.
- **Encoded-frame queues** — SPSC ring per codec sink; encoder pushes,
  WebRTC / WebSocket consumer pops.
- **Audio ring buffer** — mutex + condvar. Currently unused
  (`audio_null.cpp`).
- **Config** — `EmulatorConfig` is a singleton with internal mutex; reads
  are infrequent enough that mutex cost doesn't matter.

## CPU subprocess

The actual emulation runs in a forked child invoked with `--ipc`. It
calls `init_m68k` or `init_ppc`, installs the chosen backend, opens the
SHM and Unix socket, then enters the per-backend main loop:

- **UAE** — `uae_cpu_execute_one()` in a tight loop with `SPCFLAGS`
  checks; 60 Hz timer polled every 100 instructions inside that loop.
- **Unicorn-m68k** — `unicorn_execute_with_interrupts(cpu, N)` from
  `unicorn_exec_loop.c`. `UC_HOOK_BLOCK` polls the timer and applies
  deferred register updates.
- **KPX** — `cpu_ppc_kpx_install`'s `cpu_execute_fast` enters
  `emul_ppc(ROMBase + 0x310000)`. Has its own tick thread inside the
  child (60 Hz) and a PRECISE_TIMING thread for PrimeTime.
- **Unicorn-PPC** — outer `uc_emu_start` loop with stall watchdogs and
  hot-skip detection. Tick thread sets `g_pending_irq`; a `UC_HOOK_BLOCK`
  drains it at TB boundaries (in-place, no `uc_emu_stop` per IRQ — see
  `ppc/UnicornPpcStatus.md`).

## Boundaries between threads

- **CPU writes the framebuffer** (m68k via `g_platform.video.update_screen`
  → `video_output.submit_frame`; PPC via `VideoVBL` and the DR emulator).
  Encoder reads through the triple buffer. No locks.
- **Input events** flow `/ws` → web server thread → Unix socket → CPU
  thread. The CPU thread injects them into ADB on the next 60 Hz tick.
- **Bridge action commands** flow `POST /api/launch` → bridge_command →
  write `_bridge_cmd` file → BridgeAgent on next WaitNextEvent tick →
  write `_bridge_result` → poll picks it up. No EmulOp injection, no
  IPC — just disk files cross the parent/child boundary for free.

## Latency

Numbers are best-case on a quiet workstation. See
[`LatencyShortcomings.md`](LatencyShortcomings.md) for the cliffs.

| Stage | Typical |
|-------|---------|
| CPU → framebuffer | < 1 ms (in-process write) |
| Encoder | 5–15 ms per H.264 / VP9 frame |
| WebRTC / network | 10–50 ms |
| Browser decode + display | 5–25 ms |
| Total click-to-pixel | ~40–90 ms |

## Shutdown

Parent receives SIGINT/SIGTERM, sets the shutdown flag, joins the encoder
+ relay + watchdog threads, then sends SIGTERM to the CPU child and
waits for it to exit. The CPU child closes its SHM and socket and
`return 0`s.

## What this doc deliberately doesn't cover

- The UAE / Unicorn / KPX execution loops in detail — see the per-backend
  files in `src/cpu/`.
- Hook ordering and deferred-update semantics in Unicorn-m68k —
  [`deepdive/cpu/UnicornQuirks.md`](deepdive/cpu/UnicornQuirks.md) and
  [`deepdive/cpu/ALineAndFLineStatus.md`](deepdive/cpu/ALineAndFLineStatus.md).
- BridgeAgent internals — [`CommandBridge.md`](CommandBridge.md).
- MacBrowser host pipeline — [`MacBrowser.md`](MacBrowser.md).
