# WebRTC Integration Status - macemu-next

**Last Updated:** January 6, 2026
**Branch:** `phoenix-mac-planning`

## Overview

macemu-next uses an **in-process WebRTC architecture** - unlike the split-process IPC design of web-streaming, all components (CPU emulation, video/audio encoding, WebRTC signaling, HTTP server) run in a single process. This eliminates IPC overhead and simplifies deployment.

## Current Status: ✅ **WebRTC Streaming WORKING**

### Completed Features

#### ✅ WebRTC Connection Establishment
- **ICE negotiation** - Server and client successfully exchange ICE candidates
- **Offer/Answer flow** - Proper SDP exchange (server sends offer, client sends answer)
- **Track opening** - Video and audio tracks open successfully
- **Data channel** - Bidirectional control channel established
- **Connection states** - Full ICE state tracking (Checking → Connected → Completed)

**Key fixes applied:**
- Fixed answer handling - client doesn't send `peerId` in answer, look up by WebSocket
- Fixed ICE candidate format - use `mid` field instead of `sdpMid` to match client expectations
- Added `isOpen()` checks before sending media to prevent "Track is not open" errors
- Added detailed state logging for debugging

#### ✅ Video/Audio Encoding Pipeline
- **H.264 encoder** - libx264 integration for video encoding
- **Opus encoder** - libopus integration for audio encoding (48kHz, 2ch, 96kbps, 20ms frames)
- **RTP packetization** - H.264RtpPacketizer and OpusRtpPacketizer from libdatachannel
- **RTCP support** - Sender Report (SR) and NACK responder for audio quality
- **Encoder threads** - Separate threads for video and audio encoding
- **Frame buffering** - Triple buffer for video, ring buffer for audio

#### ✅ WebRTC Server
- **libdatachannel integration** - Lightweight C++ WebRTC library
- **WebSocket signaling** - Port 8090 for SDP/ICE exchange
- **HTTP server** - Port 8000 for web client and REST API
- **Multi-peer support** - Can handle multiple browser connections
- **Global server pointer** - Encoder threads access WebRTC via `webrtc::g_server`

#### ✅ Platform Drivers
- **WebRTC video driver** - `video_webrtc_*` functions for framebuffer output
- **WebRTC audio driver** - `audio_webrtc_*` functions for PCM audio output
- **Driver installation** - Registered in platform layer for CPU access

### Current Limitations

#### ⚠️ ROM Loading Workflow
**Status:** ROM must be provided at startup via command line

**Current behavior:**
- If started without ROM: Enters "webserver mode" - CPU not initialized
- If user clicks "Start" in web UI without ROM: Returns error message
- **Required workflow:** Start with ROM path: `./build/macemu-next <rom-path>`

**Web UI configuration works, but:**
- User can configure ROM in web UI
- Config is saved to `~/.config/macemu-next/config.json`
- But process must be **restarted** with ROM path for CPU to initialize

**Example:**
```bash
# Start with ROM directly
./build/macemu-next "/home/mick/macemu/web-streaming/storage/roms/1MB ROMs/1991-10 - 420DBFF3 - Quadra 700&900 & PB140&170.ROM"

# OR configure in web UI, then restart with ROM path
```

#### ⚠️ No Actual Mac Video/Audio Yet
**Status:** WebRTC pipeline works, but no Mac is running

**What works:**
- WebRTC connection establishes
- Tracks open successfully
- Encoder threads are ready to encode
- RTP packets would be sent if there was data

**What's missing:**
- ROM must be loaded for CPU to initialize
- Once ROM loads and Mac boots, video/audio will flow automatically
- Current audio stats show "underruns" because no Mac audio is being generated

#### 🔜 To Be Implemented
1. **Deferred CPU initialization** - Load ROM and init CPU when web UI says "Start"
2. **PPC platform support** - Currently only m68k (Basilisk II) architecture
3. **Mouse/keyboard input** - Capturing browser input and sending to Mac
4. **Dynamic resolution** - Changing Mac screen resolution without restart
5. **Codec selection** - Runtime switching between H.264/VP9/AV1/PNG

## Architecture

```
┌────────────────────────────────────────────────────────────────┐
│                      macemu-next Process                        │
│                                                                 │
│  ┌──────────────┐     ┌─────────────────────────────────────┐ │
│  │ Mac CPU      │────►│ Platform Video Driver               │ │
│  │ (UAE/Unicorn)│     │ video_webrtc_refresh()              │ │
│  │              │     │ - Write to triple buffer            │ │
│  │              │     │ - Signal video encoder thread       │ │
│  └──────────────┘     └─────────────────────────────────────┘ │
│         │             ┌─────────────────────────────────────┐ │
│         │             │ Platform Audio Driver               │ │
│         └────────────►│ audio_webrtc_*()                    │ │
│                       │ - Write to ring buffer              │ │
│                       │ - Signal audio encoder thread       │ │
│                       └─────────────────────────────────────┘ │
│                                      │                         │
│                                      v                         │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │ Video Encoder Thread                                     │ │
│  │ - Read from triple buffer                                │ │
│  │ - Encode to H.264 (libx264)                             │ │
│  │ - Call webrtc::g_server->send_video_frame()            │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                      │                         │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │ Audio Encoder Thread                                     │ │
│  │ - Read from ring buffer (20ms frames)                   │ │
│  │ - Encode to Opus (libopus, 48kHz)                       │ │
│  │ - Call webrtc::g_server->send_audio_frame()            │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                      │                         │
│                                      v                         │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │ WebRTC Server (libdatachannel)                          │ │
│  │ - RTP packetization (H.264RtpPacketizer, OpusRtp...)   │ │
│  │ - RTCP SR (Sender Report) for sync                      │ │
│  │ - RTCP NACK for packet loss recovery                    │ │
│  │ - ICE connectivity (STUN optional for LAN)              │ │
│  │ - Track management (check isOpen() before send)         │ │
│  │ - Multi-peer distribution                               │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                      │                         │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │ HTTP Server (port 8000)                                  │ │
│  │ - Static files (client HTML/JS/CSS)                     │ │
│  │ - REST API (/api/config, /api/status, etc.)            │ │
│  │ - Config injection (embed config in HTML)               │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                      │                         │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │ WebSocket Signaling Server (port 8090)                   │ │
│  │ - Handle 'connect' messages from browser                │ │
│  │ - Send SDP offers to browser                            │ │
│  │ - Receive SDP answers from browser                      │ │
│  │ - Exchange ICE candidates (mid field!)                  │ │
│  └─────────────────────────────────────────────────────────┘ │
└────────────────────────────────────────────────────────────────┘
                             │
                   WebRTC + WebSocket
                             v
┌────────────────────────────────────────────────────────────────┐
│                       Web Browser                               │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │ client.js                                                 │ │
│  │ - WebSocket connection to :8090                          │ │
│  │ - RTCPeerConnection with STUN server                     │ │
│  │ - Receive SDP offer, create answer                       │ │
│  │ - Wait for ICE gathering complete, send answer          │ │
│  │ - All ICE candidates in SDP (not trickle ICE)           │ │
│  └──────────────────────────────────────────────────────────┘ │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │ Media Elements                                            │ │
│  │ - <video> - H.264 decode (hardware accelerated)          │ │
│  │ - <audio> - Opus decode                                  │ │
│  │ - Track state: muted → unmuted when data arrives        │ │
│  └──────────────────────────────────────────────────────────┘ │
└────────────────────────────────────────────────────────────────┘
```

## Key Design Decisions

### 1. In-Process vs IPC
**Choice:** All components in single process

**Rationale:**
- Simpler deployment (single binary)
- No IPC synchronization overhead
- Direct function calls between components
- Easier debugging (single process to attach)

**Trade-offs:**
- Crash in one component crashes entire emulator
- More complex thread management
- Harder to hot-swap components

### 2. Server-Initiated Offer
**Choice:** Server creates and sends SDP offer

**Rationale:**
- Server knows available codecs and tracks
- Matches web-streaming pattern
- Browser creates answer (simpler client code)

### 3. Non-Trickle ICE
**Choice:** Browser bundles all ICE candidates in SDP answer

**Rationale:**
- Simpler signaling protocol
- No need for individual candidate messages from browser
- Server still sends individual candidates (for browser consumption)
- Works well for LAN deployments

### 4. Global WebRTC Server Pointer
**Choice:** `webrtc::g_server` global for encoder thread access

**Rationale:**
- Encoder threads created before WebRTC server
- Avoids complex dependency injection
- Simple, direct access pattern

**Alternative considered:** Dependency injection via platform layer

## File Structure

```
macemu-next/
├── src/
│   ├── main.cpp                      # Entry point, initialization
│   ├── webrtc/
│   │   ├── webrtc_server.h/cpp       # WebRTC server (libdatachannel)
│   │   └── webrtc_server_thread.cpp  # Server thread management
│   ├── webserver/
│   │   ├── http_server.h/cpp         # HTTP server
│   │   ├── api_handlers.h/cpp        # REST API endpoints
│   │   ├── static_files.h/cpp        # Static file serving
│   │   └── file_scanner.h/cpp        # ROM/disk scanning
│   ├── drivers/
│   │   ├── video/
│   │   │   ├── video_webrtc.cpp      # WebRTC video driver
│   │   │   ├── video_encoder_thread.cpp  # H.264 encoding
│   │   │   ├── triple_buffer.h       # Lock-free triple buffer
│   │   │   └── encoders/
│   │   │       └── h264_encoder.h/cpp # libx264 wrapper
│   │   └── audio/
│   │       ├── audio_webrtc.cpp      # WebRTC audio driver
│   │       ├── audio_encoder_thread.cpp  # Opus encoding
│   │       ├── audio_output.h        # Ring buffer for audio
│   │       └── encoders/
│   │           ├── opus_encoder.h/cpp # libopus wrapper
│   │           └── audio_config.h     # Audio constants
│   ├── config/
│   │   ├── config_manager.h/cpp      # JSON config loading/saving
│   │   └── json_utils.h/cpp          # JSON helper functions
│   └── cpu/
│       ├── uae/                      # UAE CPU backend (m68k)
│       └── unicorn/                  # Unicorn CPU backend
├── client/                           # Web client files
│   ├── index.html
│   ├── client.js                     # WebRTC client logic
│   └── styles.css
└── subprojects/
    └── libdatachannel/               # WebRTC library
```

## Configuration Files

### macemu-next Config
**Location:** `~/.config/macemu-next/config.json`

**Structure:**
```json
{
  "common": {
    "ram": 32,
    "screen": "1024x768",
    "sound": true,
    "extfs": "./storage"
  },
  "m68k": {
    "rom": "1MB ROMs/1991-10 - 420DBFF3 - Quadra 700&900 & PB140&170.ROM",
    "cpu": 4,
    "fpu": true,
    "disks": ["7.6.img"],
    "cdroms": [],
    ...
  },
  "ppc": { ... },
  "web": {
    "emulator": "m68k",
    "codec": "h264",
    "mousemode": "relative",
    "http_port": 8000,
    "storage_dir": "/home/mick/macemu/web-streaming/storage/"
  }
}
```

**ROM path resolution:**
- Full path: `{storage_dir}/roms/{m68k.rom}`
- Example: `/home/mick/macemu/web-streaming/storage/roms/1MB ROMs/1991-10...ROM`

## API Endpoints

### Configuration
- `GET /api/config` - Get current configuration
- `POST /api/config` - Save configuration
- `GET /api/storage` - List ROMs and disk images

### Emulator Control
- `POST /api/emulator/start` - Start CPU (requires ROM loaded)
- `POST /api/emulator/stop` - Stop CPU
- `POST /api/emulator/restart` - Restart (not implemented)
- `POST /api/emulator/reset` - Reset (not implemented)

### Status
- `GET /api/status` - Get emulator status (running/stopped, ROM loaded, etc.)

### Logging
- `POST /api/log` - Browser log forwarding
- `POST /api/error` - Browser error reporting

## Testing

### Manual Testing Steps

1. **Build:**
   ```bash
   cd /home/mick/macemu-dual-cpu/macemu-next
   meson compile -C build
   ```

2. **Run with ROM:**
   ```bash
   ./build/macemu-next "/path/to/rom.ROM"
   ```

3. **Open browser:**
   ```
   http://localhost:8000
   ```

4. **Verify WebRTC:**
   - Check browser console (F12) for connection logs
   - Should see: "ICE gathering complete", "Audio track unmuted"
   - Check terminal for: "Video track opened", "Audio track opened"

5. **Check logs:**
   ```
   [WebRTC] Peer client-0 ICE state: Connected
   [WebRTC] Peer client-0 ICE state: Completed
   [WebRTC] Video track opened for client-0
   [WebRTC] Audio track opened for client-0
   [WebRTC] Peer client-0 state: Connected
   [WebRTC] Data channel opened for peer client-0
   ```

### Known Test ROMs
```bash
# Quadra 700/900
/home/mick/macemu/web-streaming/storage/roms/1MB ROMs/1991-10 - 420DBFF3 - Quadra 700&900 & PB140&170.ROM

# Quadra 950
/home/mick/macemu/web-streaming/storage/roms/1MB ROMs/1992-03 - 3DC27823 - Quadra 950.ROM
```

## Recent Commits (Session Summary)

### WebRTC Connection Fixes (Jan 6, 2026)
1. `ae823b1a` - Connect encoder threads to WebRTC server for media transmission
2. `5f0d8d31` - Fix "Track is not open" errors by checking isOpen() before sending
3. `fe144745` - Fix ICE candidate format to use 'mid' instead of 'sdpMid'
4. `89f01aec` - Add detailed WebRTC connection state logging
5. `44723671` - **CRITICAL:** Fix answer handling - client doesn't send peerId
6. `2053abcf` - Add RTCP SR and NACK handlers for audio
7. `33929dfe` - Add ROM check to /api/emulator/start with helpful error

### Key Breakthrough
**Commit `44723671`** was the showstopper fix - the browser's SDP answer wasn't being processed because we tried to look up the peer by `peerId` (which the client doesn't send in answers). Changed to look up by WebSocket mapping instead, allowing `setRemoteDescription()` to be called, which enabled ICE to complete.

## Next Session Plan

See [NEXT_SESSION_PLAN.md](./NEXT_SESSION_PLAN.md) for detailed implementation plan for deferred CPU initialization.

## Dependencies

### Runtime
- **libdatachannel** - WebRTC library
- **libx264** - H.264 video encoder
- **libopus** - Opus audio encoder
- **libsrtp2** - SRTP for libdatachannel
- **nlohmann-json** - JSON parsing

### Build
- **meson** - Build system
- **ninja** - Build backend
- **pkg-config** - Dependency detection

## References

- [libdatachannel](https://github.com/paullouisageneau/libdatachannel) - WebRTC library
- [WebRTC API](https://developer.mozilla.org/en-US/docs/Web/API/WebRTC_API) - Browser WebRTC
- [web-streaming](../web-streaming/) - Reference IPC-based implementation
- [RFC 6184](https://datatracker.ietf.org/doc/html/rfc6184) - H.264 RTP Payload Format
- [RFC 7587](https://datatracker.ietf.org/doc/html/rfc7587) - Opus RTP Payload Format
