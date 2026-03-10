# Codec Full-Stack Testing Notes

## Test: `tests/e2e/codec-fullstack.spec.ts`

Verifies complete video pipeline for all 4 codecs:
- H264/VP9: encoder → RTP video track → browser native decode
- PNG/WebP: encoder → DataChannel binary → JS decode

## Key Findings

### 1. Headless Chromium Cannot Decode WebRTC Video
- **Symptom**: `framesDecoded: 0` despite `bytesReceived: 3-4MB`
- **Root cause**: Chrome's headless mode doesn't initialize the video decoder pipeline
- **Fix**: Use `xvfb-run` with `headless: false` in playwright.config.ts
- **Affects**: H264 AND VP9 (both need a display surface for WebRTC decode)
- SwiftShader (`--use-gl=angle --use-angle=swiftshader`) doesn't help reliably

### 2. H264 framesDecoded: 0 Despite Valid RTP Flow (CURRENT BLOCKER)
- **Symptom**: Server creates H264 video track, sends frames, browser receives bytes but `framesDecoded: 0`, `framesReceived: 0`, `codecName: null`
- **Observed with**: xvfb-run + headed mode + --config /dev/null (fresh emulator)
- **Works when**: Stale emulator already in H264 mode (no codec switch needed)
- **Detailed server-side trace**:
  - `[WebRTC] Client connect request - creating peer connection for client-1 (codec: h264)` - server creates H264 track
  - `[WebRTC] Sent offer to client-1 (sdp length=1316)` - SDP has H264 codec
  - `[WebRTC] Peer client-1 ICE state: Checking` → connecting → should complete
  - H264 encoder producing frames: `IDR=6 P=984, avg_p=89 bytes`
  - H264 IDR keyframe size: **1,258,579 bytes (1.2MB)** at 640x480
- **HYPOTHESIS**: The 1.2MB H264 IDR keyframe might exceed RTP/SCTP limits, causing Chrome to fail to assemble the first frame → no decode starts

### 3. VP9 Works Correctly with xvfb
- `framesDecoded: 51, framesReceived: 51, codec: video/VP9, resolution: 1920x1080`
- VP9 IDR keyframe at 1920x1080: ~290KB (much smaller than H264)
- VP9 P-frames: ~2-4KB

### 4. PNG DataChannel "Message size exceeds limit"
- **Symptom**: `[WebRTC] Error sending video frame: Message size exceeds limit`
- **Root cause**: PNG frames at 1280x1024 are ~460KB each, exceeds datachannel SCTP limit
- **Fix**: Use `--screen 640x480` (PNG frames ~460KB at 640x480 still large)
- At 640x480 PNG frames are 460,831 bytes (450KB) -- still too large for some SCTP configs
- Need to investigate datachannel maxMessageSize negotiation

### 5. Mac Resolution Mode Switch
- `[SHM Video] Mode switch to 1920x1080x32` happens during boot
- Even with `--screen 640x480`, Mac switches to 1920x1080 after boot
- This causes video encoder to switch to 1920x1080 too
- VP9 stats show `resolution: 1920x1080` even when started with 640x480

## Server-Side Flow (Codec Switch)

```
1. POST /api/codec {"codec":"h264"}
2. api_handlers.cpp: updates ctx_->server_codec + ctx_->config->codec
3. Calls notify_codec_change_fn(new_codec)
4. webrtc_server.cpp: sends {"type":"reconnect","codec":"h264"} to all peers via WS
5. Closes all existing PeerConnections
6. Client receives "reconnect" → calls reconnectPeerConnection()
7. Client sends {"type":"connect","codec":"h264"} on existing WS
8. Server creates new PeerConnection with H264 video track + H264RtpPacketizer
9. SDP offer sent → answer received → ICE → DTLS → media
```

## Client-Side Codec Negotiation

```javascript
// client.js:898 - determines codec for connect message
const codec = this.codecType || serverUIConfig.webcodec || 'h264';
```

Where `serverUIConfig.webcodec` comes from:
- Embedded config in HTML (server-rendered `<script id="server-config">`)
- Or fetched from `/api/config` as fallback

After codec POST + page reload:
- Server re-renders index.html with updated codec in embedded config
- Client reads new codec and sends correct connect request

## Fixture Configuration

```
--config /dev/null     # Ignore saved config
--screen 640x480       # Start with small resolution (Mac may switch later)
--ram 128              # Enough for boot to Finder
--dismiss-shutdown-dialog
--disk macos-7.5.5.img
```

## Root Cause: Mac Mode Switch to 1920x1080

The Mac ALWAYS switches to the highest available resolution during boot (around `warm start`).
`video_webrtc.cpp` exposes 6 modes up to 1920x1080. The `--screen` flag only sets the
initial mode — Mac OS immediately switches to 1920x1080.

This causes:
- H264 IDR keyframes: 1.2-1.4MB (at 1920x1080 with QP 48-51)
- VP9 keyframes: ~290KB (much better at 1920x1080)
- PNG frames: ~460KB (at 640x480), much larger at 1920x1080
- P-frame overflows: H264 P-frames reach 500-600KB with dithered content

## H264 Decode Failure Analysis

**Symptom**: `bytesReceived: 3-4MB, framesReceived: 0, framesDecoded: 0`
- Server creates H264 video track, H264RtpPacketizer with LongStartSequence
- SDP offer includes H264 (profile 42e01f, packetization-mode 1)
- ICE connects, video track OPEN event fires
- Encoder sends ~60fps, IDR every 5s (~300 frames), P-frames ~90-100 bytes

**NOT a codec switching issue**: Fails even when emulator starts with H264 default
(`--config test-config-h264.json`)

**VP9 works**: Same pipeline, same resolution, same browser. VP9 keyframes are 290KB vs
H264's 1.2MB. Chrome decodes VP9 fine.

**HYPOTHESIS**: H264 1.2MB IDR keyframe → ~1000 RTP packets → Chrome can't reassemble
reliably. VP9 290KB keyframe → ~240 packets → works. The issue might be in the
H264RtpPacketizer's FU-A fragmentation or Chrome's jitter buffer can't handle 1000-packet
frames at localhost latency (packets arrive too fast).

## DataChannel Size Limit (FIXED)

- Chrome negotiates `maxMessageSize: 262144` (256KB)
- PNG frames at 640x480: ~460KB → exceeds limit
- Added pre-send check in `webrtc_server.cpp` with clear error message
- Suggests using H264/VP9 for high resolutions

## Next Steps

1. **H264**: Test with very small resolution (320x240?) to see if smaller IDR frames decode
2. **H264**: Try increasing RTP packet size or reducing IDR frame size
3. **PNG/WebP**: Consider fragmenting frames into <256KB chunks
4. **Mode switch**: Consider limiting available modes based on codec
   - RTP codecs (H264/VP9): all modes ok
   - DC codecs (PNG/WebP): limit to 640x480 or lower

## Commands

```bash
# Single codec test
kill $(lsof -t -i:18094); xvfb-run npx playwright test tests/e2e/codec-fullstack.spec.ts -g "vp9: full pipeline"

# All codec tests
kill $(lsof -t -i:18094); xvfb-run npx playwright test tests/e2e/codec-fullstack.spec.ts

# With debug output
DEBUG_EMULATOR=1 xvfb-run npx playwright test tests/e2e/codec-fullstack.spec.ts -g "h264" --reporter=line
```
