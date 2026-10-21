# Latency Shortcomings

Known sources of avoidable latency in the input and video paths. Issues are
grouped by path and tagged with a severity (high / medium / low) based on how
visible they are to a user interacting with the WebUI. Code locations are
given by function; line numbers drift.

The healthy parts, for reference, are the WebSocket → ADB ring-buffer
input fast-path and the QLocalSocket-notify-driven encoder wakeup
(IPC Phase 3b replaced the prior eventfd path with a Qt-wrapped Unix
domain socket: same kernel-level wakeup, no extra fd). Everything
below is a deviation from that baseline.

## Input path (WebUI → ADB)

### HIGH — 50 ms blocking press on legacy `/api/keypress`

`handle_keypress` in `src/webserver/api_handlers.cpp`

A keypress request without a `down` field sends key-down, sleeps 50 ms
(`usleep(50000)`) on the HTTP worker, then sends key-up. Both the
IPC-subprocess branch and the in-process branch do this. API handlers run
one at a time under the server's lock (`api_mutex_` in
`src/webserver/http_server.cpp`; only the bridge routes and `/api/wait` are
registered `allow_concurrent`), so any other API request (mouse, screenshot,
status) waits out the 50 ms.

Clients that send discrete `down`/`up` events avoid this path entirely.

### ~~MED — Bridge command result polling at 100 ms~~

Fixed: the bridge no longer goes through files (`CommandBridge.md`). The
host writes the request into the bridge slot and polls it every 2 ms
(`guest_bridge::request` in `src/common/include/guest_bridge.h`); the
agent's Time Manager task (50 ms period, `BridgeAgent/BridgeAgent.c`) wakes
the agent, which answers through the bridge trap `$ABE5`. What remains is
up to 50 ms before the agent notices a request.

### MED — `/api/wait` condition poll at 200 ms

`handle_wait` in `src/webserver/api_handlers.cpp`

`/api/wait` re-checks its `app=` / `boot=` / `text=` condition every
200 ms. Automation sees up to 200 ms of jitter before a transition is
reported. Boot-phase and front-app changes are already published
internally; a condvar signalled on transitions would drop this to
effectively zero. (`/api/wait` runs beside the other routes, so it does not
hold them up.)

### LOW — No rate limit on discrete input

`handle_mouse_move` / `handle_keypress` in `src/webserver/api_handlers.cpp`,
`process_input_message` in `src/webrtc/webrtc_server.cpp`

Each event triggers `TriggerInterrupt()` immediately (`src/core/adb.cpp`).
ADB's 16-key / 32-button ring buffers absorb bursts without drops, so this
is a latency win in practice; noted only as a thing to preserve.

## Video encoder path (framebuffer → codec → WebRTC)

### HIGH — Unconditional full-frame memcpy on every `submit_frame`

`VideoOutput::submit_frame_dirty` in `src/drivers/video/video_output.cpp`

`submit_frame_dirty()` (and `submit_frame()`, which calls it) copies the
entire framebuffer into the triple-buffer slot on every call, regardless of
the dirty region it is given. At 1920×1080×4 that is ~8 MB per frame, i.e.
~480 MB/s at 60 FPS, on top of the encoder's subsequent read. With the CPU
in a subprocess (the IPC path) it is worse: the encoder thread first copies
the frame out of shared memory, then calls `submit_frame()` with it so the
screenshot API has a frame, a second full copy. A ref-counted / zero-copy
handoff (or at minimum, copying only the dirty rect) would reclaim this.

`snapshot_frame()` / `snapshot_frame_ex()` do a similar full copy for the
screenshot API, which is fine for an on-demand endpoint but worth knowing.

### MED — IPC SHM `shared_lock` held for the entire encode pass

The encoder loop in `src/drivers/video/video_encoder_thread.cpp`

The encoder thread takes `g_ipc_shm_mutex` (shared) before reading the SHM
frame and keeps it across the full encode (H.264/VP9: 5–50 ms typical).
Subprocess lifecycle operations (connect/disconnect/restart) serialise
against encode time. The frame is already copied out of SHM into a private
buffer before encoding, so the lock could be released right after that
copy, cutting the critical section to a memcpy.

### MED — Full-frame pixel compare for PNG/WebP dirty-rect

`compute_dirty_rect` in `src/drivers/video/video_encoder_thread.cpp`
(WebSocket PNG/WebP), `frame_compute_dirty_rect` in
`src/webserver/api_handlers.cpp` (`/api/frame`), `compute_dirty_rect` in
`src/webserver/http_stream.cpp` (`/api/stream`)

The PNG/WebP paths scan the whole frame pixel-by-pixel against the previous
one to compute the dirty region. At 1080p that's ~8M comparisons per frame,
adding 5–10 ms even when almost nothing changed. H.264/VP9 don't need this.
For the still-image codecs, a coarse tile hash or a quad-tree would cut it
by orders of magnitude.

### LOW — 1 ms sleep polling in the non-notify fallback

The IPC branch of the encoder loop in
`src/drivers/video/video_encoder_thread.cpp`

On Linux the encoder polls the IPC notify fd (a QLocalSocket, 16 ms
timeout) and wakes immediately on a frame-ready byte. Before the notify fd
is known, and on non-Linux builds, it sleeps 1 ms between checks instead,
so a frame submitted at t=0 is seen at the next 1 ms tick. Small; mostly
visible during the few hundred ms before the child's IPC is up. (In-process,
`VideoOutput::wait_for_frame` blocks on a semaphore: no polling.)

### LOW — Mode-switch reinit without in-flight flush

The resolution-change check in the encoder loop,
`src/drivers/video/video_encoder_thread.cpp`

A change of frame size marks the encoder uninitialised and runs
`encoder->init()` on the next frame, without flushing in-flight encoder
state or dropping stale pre-switch frames explicitly. A keyframe is
requested, but there's a brief (~50 ms) init stall around it.

## Video driver (framebuffer producer)

### MED — Mode switch shows black until new keyframe arrives

`switch_to_current_mode` in `src/drivers/video/video_webrtc.cpp`

`switch_to_current_mode()` memsets the framebuffer to zero and requests a
keyframe. The client renders black until the keyframe at the new resolution
finishes encoding and ships, perceptually ~500 ms. Holding the last frame
at old resolution until the new keyframe is ready, or cross-fading, would
hide this.

### LOW — No backpressure on over-production

`VideoOutput::submit_frame_dirty` in `src/drivers/video/video_output.cpp`

If the emulator submits faster than the encoder consumes, the middle
triple-buffer slot is silently overwritten: frames vanish with no signal
back to the producer. This is the intentional lock-free contract (the CPU
thread never blocks on video), noted here only so the "where did my frames
go" question has an answer.

## Rough priority for fixing

1. Remove the 50 ms `usleep` on legacy `/api/keypress` (cheap; big win for
   anyone still on that path).
2. Replace the 200 ms `/api/wait` poll with an event-driven wait.
3. Eliminate the unconditional full-frame memcpy in `submit_frame` (and the
   second copy on the IPC path), the largest steady-state cost on the video
   path.
4. Release the IPC SHM lock once the frame is copied out, before encoding.
5. Replace full-frame dirty-rect scan for PNG/WebP with a coarse structure.
