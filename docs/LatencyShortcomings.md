# Latency Shortcomings

Known sources of avoidable latency in the input and video paths. Issues are
grouped by path and tagged with a severity (high / medium / low) based on how
visible they are to a user interacting with the WebUI.

The healthy parts, for reference, are the WebRTC DataChannel → ADB ring-buffer
input fast-path and the in-process eventfd-driven triple buffer. Everything
below is a deviation from that baseline.

## Input path (WebUI → ADB)

### HIGH — 50 ms blocking press on legacy `/api/keypress`

`src/webserver/api_handlers.cpp:1250, 1254`

The legacy keypress code path (no discrete `down`/`up` in the request) sends
key-down, sleeps 50 ms on the HTTP worker, then sends key-up. Both the
IPC-subprocess branch and the in-process branch do this. Any other HTTP
request serialised behind that worker (mouse, screenshot, status) waits out
the full 50 ms per legacy keypress.

Clients that send discrete `down`/`up` events avoid this path entirely.

### MED — Bridge command result polling at 100 ms

`src/webserver/api_handlers.cpp:1331–1332`

`bridge_command()` (shutdown, restart, launch, quit) polls the reply file on
a 100 ms `sleep_for` loop for up to 10 s (30 iters in the helper, 100 iters
in `handle_launch`). Results become visible in 100 ms quantised steps, so
even a fast guest-side ack takes ~100 ms to surface. An inotify / file-event
watch would deliver in ≪ 1 ms.

### MED — `/api/wait` condition poll at 200 ms

`src/webserver/api_handlers.cpp:1434`

`/api/wait` re-checks `app=` / `boot=` conditions every 200 ms. Automation
and the UI's "app launched" feedback observe up to 200 ms jitter before a
transition is reported. Boot-phase and front-app state changes are already
published events internally; a condvar signalled on transitions would drop
this to effectively zero.

### LOW — No rate limit on discrete input

`src/webserver/api_handlers.cpp:1104–1173`, `src/webrtc/webrtc_server.cpp:155–260`

Each event triggers `TriggerInterrupt()` immediately. ADB's 16-key /
32-button ring buffers absorb bursts without drops, so this is a latency win
in practice — noted only as a thing to preserve.

## Video encoder path (framebuffer → codec → WebRTC)

### HIGH — Unconditional full-frame memcpy on every `submit_frame`

`src/drivers/video/video_output.cpp:100`

`submit_frame()` copies the entire framebuffer into the triple-buffer slot
on every call, regardless of dirty region. At 1920×1080×4 that is ~8 MB per
frame, i.e. ~480 MB/s at 60 FPS, on top of the encoder's subsequent read.
This saturates L3 and memory bandwidth for no functional gain — the
emulator already owns the source pixels; a ref-counted / zero-copy handoff
(or at minimum, copying only the submitted dirty rect) would reclaim this.

`snapshot_frame()` at lines 225 and 246 does a similar full copy for the
screenshot API, which is fine for an on-demand endpoint but worth knowing.

### MED — IPC SHM `shared_lock` held for the entire encode pass

`src/drivers/video/video_encoder_thread.cpp:340, 388`

The encoder thread takes `g_ipc_shm_mutex` before reading the SHM frame and
holds it across the full encode (H.264/VP9: 5–50 ms typical). Subprocess
lifecycle operations (connect/disconnect/restart) serialise against encode
time. Copying the frame under the lock and encoding afterwards would cut
the critical section to a memcpy.

### MED — Full-frame pixel compare for PNG/WebP dirty-rect

`src/drivers/video/video_encoder_thread.cpp:131–174`,
`src/webserver/api_handlers.cpp:832–863`

The PNG/WebP path scans the whole frame pixel-by-pixel to compute the
dirty region. At 1080p that's ~8M comparisons per frame, adding 5–10 ms
even when almost nothing changed. H.264/VP9 don't need this — for the
still-image codecs, a coarse tile hash or a quad-tree would cut it by
orders of magnitude.

### LOW — 1 ms sleep polling in the non-eventfd fallback

`src/drivers/video/video_output.cpp:185–202`,
`src/drivers/video/video_encoder_thread.cpp:418`

Linux uses eventfd and wakes immediately. The portable fallback polls on a
1 ms `sleep_for`, so a frame submitted at t=0 is observed at the next 1 ms
tick. Small; only relevant on non-Linux builds or if eventfd creation fails.

### LOW — Mode-switch reinit without in-flight flush

`src/drivers/video/video_encoder_thread.cpp:439–458`

A guest mode change runs `encoder->init()` without flushing in-flight
encoder state or dropping stale pre-switch frames explicitly. The next
keyframe is requested, but there's a brief (~50 ms) init stall around it.

## Video driver (framebuffer producer)

### MED — Mode switch shows black until new keyframe arrives

`src/drivers/video/video_webrtc.cpp:52–65`

`switch_to_current_mode()` memsets the framebuffer to zero and requests a
keyframe. The client renders black until the keyframe at the new resolution
finishes encoding and ships — perceptually ~500 ms. Holding the last frame
at old resolution until the new keyframe is ready, or cross-fading, would
hide this.

### LOW — No backpressure on over-production

`src/drivers/video/video_output.cpp:70–113` (`submit_frame`)

If the emulator submits faster than the encoder consumes, the middle
triple-buffer slot is silently overwritten — frames vanish with no signal
back to the producer. This is the intentional lock-free contract (the CPU
thread never blocks on video), noted here only so the "where did my frames
go" question has an answer.

## Rough priority for fixing

1. Remove the 50 ms `usleep` on legacy `/api/keypress` (cheap; big win for
   anyone still on that path).
2. Replace the 100 ms bridge result poll and the 200 ms `/api/wait` poll
   with event-driven waits.
3. Eliminate the unconditional full-frame memcpy in `submit_frame` — the
   single largest steady-state cost on the video path.
4. Shrink the IPC SHM critical section to a copy, encode outside the lock.
5. Replace full-frame dirty-rect scan for PNG/WebP with a coarse structure.
