# Threading & process model

Two processes in webserver mode: the parent serves HTTP, WebSocket and
WebRTC and encodes; the child runs the emulator, with the CPU on its main
thread.

## Topology

```
parent (mac-phoenix)
├── main thread                — starts the rest, joins the HTTP and WebRTC threads
├── HTTP server                — http_server_main(): a QThread accept loop
│                                 (POSIX accept on --port) and 16 worker
│                                 QThreads; REST API under one lock, except
│                                 allow_concurrent routes (bridge, /api/wait)
│   ├── WebSocket threads      — one per /ws connection (fd dup'd off a
│   │                             worker): signaling JSON, input, PNG/WebP
│   │                             frames, Opus packets for those codecs
│   └── stream threads         — one per /api/stream long-poll (same handoff)
├── WebRTC thread              — webrtc_server_main(): keep-alive only;
│                                 libdatachannel runs its own threads for
│                                 peer connections and RTP
├── video encoder thread       — video_encoder_main(): waits on the notify
│                                 socket fd, copies the ready frame out of
│                                 the SHM, submits it to the VideoOutput
│                                 triple buffer (screenshots), encodes
│                                 H.264 / VP9 / WebP / PNG, sends via RTP
│                                 or /ws
├── audio IPC reader thread    — every 20 ms asks the child for a frame,
│                                 reads it from the SHM ring, byte-swaps,
│                                 writes the AudioOutput ring
├── audio encoder thread       — AudioOutput ring → Opus → RTP track or /ws
├── child watchers             — restart the child after a guest restart
│                                 (IPC_EXIT_GUEST_RESTART); exit with the
│                                 guest under Executor --headless-http
└── timeout thread             — with --timeout

child (mac-phoenix --ipc --no-webserver, started with QProcess)
├── main thread                — the CPU: cpu_execute_fast() (ROM machines)
│                                 or executor_child_main() (Executor)
├── control IPC worker         — control_ipc_child.cpp: owns the control and
│                                 notify QLocalServers; input → ADB queue,
│                                 commands (stop, reset, debugger, snapshot),
│                                 audio requests
├── tick thread                — 60.15 Hz: video refresh into the SHM,
│                                 cursor / app / Mac state export, interrupt
│                                 flags (timer_interrupt.cpp on 68k; the
│                                 PPC installers have their own)
├── Time Manager thread        — PRECISE_TIMING_POSIX (src/core/timer.cpp)
├── audio thread               — audio_direct.cpp: on the parent's request,
│                                 runs the Apple Mixer and writes PCM to the SHM
├── Ethernet RX thread         — with --network socket (ether_socket.cpp)
└── Qt WebEngine thread        — with --browser (experimental MacBrowser)
```

`--no-webserver` runs the emulator in the parent process, no child. With
`--headless-http` the HTTP server runs beside it and the bridge slot lives
in process memory (Executor is the exception: `--headless-http` runs it as
a child behind the normal web server, because it reports status only
through the SHM).

## Why two processes

- **Crash isolation.** A SIGSEGV in a CPU core or JIT can't take down the
  HTTP listener. The parent notices the child exit and the UI reports it.
- **Stop and reset are process operations.** Stop is SIGTERM to the child,
  reset is stop + start; the guest's memory and every global in the
  emulator start fresh each time.
- **Separate address spaces.** The child maps the guest memory (up to a
  4 GB reservation for `mame-ppc`, fixed low addresses for Kheperix); the parent stays small.

## IPC

Defined in `src/ipc/ipc_protocol.h`.

- **QSharedMemory `macemu-video-<pid>`** (`IPCBuffer`) — triple-buffered
  frames, boot phase and CHECKLOAD count, `cur_app_name`, cursor state, a
  JSON Mac-state snapshot, the `/api/ui` and `/api/text` request/answer
  areas, the bridge slot (`IPCBridgeSlot`) and the audio frame ring.
- **QLocalServer `macemu-control-<pid>`** — parent → child: key and mouse
  events, commands (`IPC_CMD_STOP`, `RESET`, `INVOKE_DEBUG`, `SNAPSHOT`),
  audio requests.
- **QLocalServer `macemu-notify-<pid>`** — child → parent: one byte per
  published frame; the encoder thread polls this fd.

## Inter-thread channels (parent)

- **VideoOutput triple buffer** — lock-free, two atomic indices. The
  encoder writes (from the SHM copy; in-process modes, the video driver),
  `/api/screenshot` reads. Frames may be dropped when a reader is behind.
- **AudioOutput ring** — mutex + condvar; audio IPC reader writes, audio
  encoder reads.
- **Config** — `EmulatorConfig::instance()`, a singleton.

## CPU loop

The child calls `init_m68k` or `init_ppc` (`src/core/cpu_context.cpp`)
after the backend's installer, starts the control worker, installs the
SIGSEGV handler and runs `platform->cpu_execute_fast()` on its main thread:

- **ROM 68k** (`cpu_m68k.cpp`) — `core->run()` until a requested stop.
  Interrupts arrive as `request_attention()` from the tick thread and are
  taken at an instruction boundary.
- **PPC** — `cpu_ppc_kpx.cpp` / `cpu_ppc_mame.cpp` enter the ROM at
  `ROMBase + 0x310000` (the nanokernel), with their own tick thread.
- **Executor** — `executor_child_main()` runs the Toolbox on its 68k core
  and publishes frames itself (`src/executor/host/executor_child.cpp`).

## Boundaries between threads

- **Frames**: the guest draws into its frame buffer; the child's tick
  thread copies it into the SHM triple buffer and pokes the notify socket;
  the parent's encoder copies it out. No locks across the process boundary,
  only the atomic indices.
- **Input**: `/ws` → WebSocket thread → `IPCClient` → control socket →
  child control worker → `ADBMouseMoved` / `ADBKeyDown` … → the guest's ADB
  interrupt picks them up.
- **Bridge action commands**: `POST /api/launch` → HTTP worker →
  `guest_bridge::request()` writes the slot and waits → BridgeAgent's Time
  Manager task sees it pending (`AGENT_PENDING`) and wakes the agent →
  `NEXT`, run, `REPLY` through the trap `$ABE5` → the worker returns.
  Details in `CommandBridge.md`.
- **Read commands** (`/api/app`, `/api/status`) read fields the child
  exports to the SHM on each tick.

## Latency

Rough numbers on a quiet workstation; see
[`LatencyShortcomings.md`](LatencyShortcomings.md) for the cliffs.

| Stage | Typical |
|-------|---------|
| Guest draw → SHM | up to one 60 Hz tick |
| Encoder | 5–15 ms per H.264 / VP9 frame |
| WebRTC / network | 10–50 ms |
| Browser decode + display | 5–25 ms |
| Total click-to-pixel | ~40–90 ms |

## Shutdown

The parent's SIGINT/SIGTERM/SIGHUP handler calls
`EmulatorSubprocess::stop()` (SIGTERM via `QProcess::terminate()`, SIGKILL
after a second) and `_exit(0)`s. The child's SIGTERM handler unlinks the SHM
and sockets and `_exit(0)`s; it also has `PR_SET_PDEATHSIG` so it dies with
the parent. A guest shutdown exits the child from `ShutdownEmulator()`.

## What this doc deliberately doesn't cover

- The CPU cores' execution loops — `src/cpu/core/`, `src/cpu/kpx/`,
  [`cpu/PLAN.md`](cpu/PLAN.md).
- BridgeAgent internals — [`CommandBridge.md`](CommandBridge.md).
- MacBrowser host pipeline — [`MacBrowser.md`](MacBrowser.md).
