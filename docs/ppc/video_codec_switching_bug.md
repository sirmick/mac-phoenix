# Bug: H264/VP9 WebRTC codecs produce no video frames

## Problem

When switching to H264 or VP9 codec in the web UI, the WebRTC connection establishes successfully (ICE connected, video track opens), but the browser never receives decodable video frames. The `<video>` element shows `videoWidth: 0, videoHeight: 0` and `receiverMuted: true` indefinitely. PNG and httpstream codecs work correctly.

## What works

- **httpstream**: Client polls `/api/frame`, server PNG-encodes from framebuffer snapshot. Works.
- **PNG over WebRTC**: Encoder thread encodes PNG strips, sends via DataChannel. Works.
- **WebP over WebRTC**: Same DataChannel path as PNG. Works.

## What doesn't work

- **H264 over WebRTC**: Encoder thread creates H264 encoder, WebRTC peer has RTP video track with H264RtpPacketizer. Track opens. Zero frames delivered.
- **VP9 over WebRTC**: Same symptoms. Encoder creates VP9 encoder, track opens, zero frames.

## Root cause identified

The VideoEncoder thread (`src/drivers/video/video_encoder_thread.cpp`) calls `video_output->wait_for_frame(100)` to get frames from the `VideoOutput` triple buffer. **During H264/VP9 periods, `wait_for_frame` always returns null (100ms timeout).** The encoder never initializes, never encodes, never calls `send_video_frame`.

During PNG/WebP periods, `wait_for_frame` returns frames normally.

## Key evidence from debug sessions

```
# PNG works — encoder gets frames, sends them:
[VideoEncoder] Initialized 800x600 @ 60 FPS
[WebRTC] Frame #1: sent=1 ... codec=3 (PNG)
[WebRTC] Frame #2: sent=1 ... codec=3

# Switch to H264 — encoder creates but never initializes (no frames):
[VideoEncoder] Codec change detected: 3 -> 0
[VideoEncoder] Creating H.264 encoder
[WebRTC] Video track OPEN for client-3 - ready to send frames!
# ... NOTHING — no "Initialized", no "Encoded frame", no "Frame #N" ...

# Switch back to PNG — frames resume immediately:
[VideoEncoder] Codec change detected: 0 -> 3
[VideoEncoder] Initialized 800x600 @ 60 FPS
[WebRTC] Frame #125: sent=1 ... codec=3
```

## Frame pipeline architecture

```
60Hz timer (timer_interrupt.cpp:86)
  → g_platform.video_refresh()
    → video_webrtc_refresh() (video_webrtc.cpp:208)
      → video::g_video_output->submit_frame(pixels, w, h, PIXFMT_ARGB)
        → increments sequence number, sets ready_index, notifies via eventfd

VideoEncoder thread (video_encoder_thread.cpp:238)
  → video_output->wait_for_frame(100)  // blocks on eventfd, checks sequence > last_read
    → if frame: encode + send_encoded_frame() → webrtc::g_server->send_video_frame()
    → if null: timeout, continue loop

VideoOutput::wait_for_frame (video_output.cpp:135)
  → checks: buffers[ready_index].sequence > last_read_sequence
  → if no new frame: poll(eventfd, 100ms) then recheck
  → returns null on timeout

VideoOutput::release_frame (video_output.cpp:205)
  → sets last_read_sequence = buffers[ready_index].sequence
```

## Where to investigate

1. **Why does `wait_for_frame` timeout during H264/VP9?**
   - The 60Hz timer calls `submit_frame` continuously (verified: it runs independently of codec)
   - `submit_frame` increments `frame_count` and sets `buf->sequence = frame_count + 1`
   - `wait_for_frame` checks `buf->sequence > last_read_sequence`
   - Hypothesis: the `ready_index` or `sequence` state becomes inconsistent after the codec change causes the encoder thread to skip or misprocess a frame

2. **Codec change resets `encoder_initialized = false` and `have_prev_frame = false`** (line 292-294). The encoder must get a frame to call `init(w, h, fps)`. If wait_for_frame never returns, init never happens.

3. **The DC codec path (PNG/WebP) processes frames differently** — it has a dirty-rect skip path (line 340-367) that calls `release_frame()` even when no encoding happens. The H264/VP9 path (line 410-443) only reaches `release_frame()` after encoding. If there's a stuck frame that was acquired but never released during the codec transition, `last_read_sequence` could be stale.

4. **Check the codec change transition**: When `new_codec != current_codec` is detected (line 287), the encoder is replaced. But the current frame iteration may have already called `wait_for_frame` and gotten a frame that's now being processed under the OLD codec path. If this frame isn't properly released, the triple buffer stalls.

5. **Static counters**: `encoded_count` at line 434 is static — H264 frames won't log "Encoded frame #N" if PNG already encoded 10+ frames. Don't be misled by missing logs.

## Key files

| File | What |
|------|------|
| `src/drivers/video/video_encoder_thread.cpp` | Encoder thread main loop, codec change detection, frame encoding |
| `src/drivers/video/video_output.cpp` | Triple buffer: submit_frame, wait_for_frame, release_frame |
| `src/drivers/video/video_output.h` | VideoOutput class, FrameBuffer struct |
| `src/drivers/video/video_webrtc.cpp` | video_webrtc_refresh (frame source), encoder thread startup |
| `src/webrtc/webrtc_server.cpp:736` | send_video_frame — RTP path (line 840-876) vs DC path (line 790-839) |
| `src/drivers/video/encoders/h264_encoder.cpp` | OpenH264 encoder (init, encode_bgra, encode_argb) |
| `src/drivers/video/encoders/vp9_encoder.cpp` | libvpx VP9 encoder |
| `client/client.js:3803` | changeCodec() — client-side codec switch flow |

## Suggested approach

1. Add a frame submission counter in `submit_frame()` that logs every 60th call with the current sequence number
2. Add a log in `wait_for_frame()` when `buf->sequence <= last_read_sequence` (frame available but stale)
3. Add a log in the codec change block (line 287) showing `last_read_sequence` and current `ready_index` buffer sequence
4. Check if `release_frame()` is called for the last frame before the codec switch — if the DC path's dirty-rect skip left a frame unreleased, `last_read_sequence` would be behind
5. Consider resetting `last_read_sequence` on codec change to force the next frame through
