# IPC Design for Web Streaming

Architecture for the standalone WebRTC server that communicates with BasiliskII/SheepShaver via IPC.

## Overview

```
┌─────────────────┐         IPC          ┌─────────────────┐
│   BasiliskII    │◄───────────────────►│  standalone     │
│  or SheepShaver │                      │     server      │
└─────────────────┘                      └─────────────────┘
        │                                        │
        ▼                                        ▼
   Mac OS Guest                            Web Browsers
```

## Implementation Status

- ✅ Shared memory for video frames (triple-buffered I420)
- ✅ Unix domain socket for input/control
- ✅ Standalone WebRTC server with H.264 encoding
- ✅ IPC video driver for emulator
- ✅ Automatic emulator discovery and connection

## IPC Protocol Version

Current version: **v3** (emulator-owned resources)

The emulator owns the shared memory and socket. The server discovers running emulators by scanning `/dev/shm/macemu-video-*` and connects to them.

## Shared Memory: Video Frames

**Location**: `/dev/shm/macemu-video-{pid}`

The emulator creates a POSIX shared memory segment for video frames. The format is I420 (YUV 4:2:0), already converted from the Mac framebuffer format.

### Structure

```c
#define MACEMU_VIDEO_MAGIC      0x4D454D55  // "MEMU"
#define MACEMU_IPC_VERSION      3
#define MACEMU_MAX_WIDTH        1920
#define MACEMU_MAX_HEIGHT       1200
#define MACEMU_I420_Y_SIZE      (MACEMU_MAX_WIDTH * MACEMU_MAX_HEIGHT)
#define MACEMU_I420_UV_SIZE     (MACEMU_MAX_WIDTH * MACEMU_MAX_HEIGHT / 4)
#define MACEMU_I420_FRAME_SIZE  (MACEMU_I420_Y_SIZE + MACEMU_I420_UV_SIZE * 2)

struct MacEmuVideoBuffer {
    // Header (64 bytes, cache-line aligned)
    uint32_t magic;           // MACEMU_VIDEO_MAGIC
    uint32_t version;         // MACEMU_IPC_VERSION (3)
    uint32_t owner_pid;       // Emulator's PID
    uint32_t width;           // Current frame width
    uint32_t height;          // Current frame height
    uint32_t state;           // Connection state
    uint32_t reserved[2];     // Future use

    // Atomic state (separate cache line)
    uint64_t frame_count;     // Total frames written (atomic)
    uint32_t write_index;     // Current write buffer 0-2 (atomic)
    uint32_t read_index;      // Current read buffer 0-2 (atomic)
    uint32_t padding[12];

    // Triple-buffered I420 frames
    // Each buffer: Y plane (MAX_WIDTH * MAX_HEIGHT) +
    //              U plane (MAX_WIDTH/2 * MAX_HEIGHT/2) +
    //              V plane (MAX_WIDTH/2 * MAX_HEIGHT/2)
    uint8_t buffers[3][MACEMU_I420_FRAME_SIZE];
};
```

### Triple Buffering Protocol

Lock-free producer/consumer with three buffers:

1. **Emulator (producer)**:
   - Gets next write buffer: `next = (write_index + 1) % 3`
   - Skips if `next == read_index` (server still reading)
   - Writes I420 frame to buffer
   - Atomically updates `write_index = next`
   - Increments `frame_count`

2. **Server (consumer)**:
   - Reads `write_index` to find latest complete frame
   - Sets `read_index` to claim buffer
   - Reads I420 data
   - No blocking, no tearing

### I420 Frame Layout

Each buffer contains planar YUV 4:2:0:

```
Offset 0:                    Y plane  (stride = MACEMU_MAX_WIDTH)
Offset Y_SIZE:               U plane  (stride = MACEMU_MAX_WIDTH / 2)
Offset Y_SIZE + UV_SIZE:     V plane  (stride = MACEMU_MAX_WIDTH / 2)
```

The actual frame dimensions may be smaller than the maximum. Use `width` and `height` from the header, but `MACEMU_MAX_WIDTH` for stride calculations.

## Unix Socket: Input and Control

**Location**: `/tmp/macemu-{pid}.sock`

The emulator creates a Unix stream socket for bidirectional communication. The server connects when it discovers the emulator.

### Binary Protocol

All messages are fixed-size 8-byte structures for simplicity and performance.

#### Input Messages (Server → Emulator)

```c
// Message types
#define MACEMU_INPUT_MOUSE  1
#define MACEMU_INPUT_KEY    2
#define MACEMU_CMD_RESET    10
#define MACEMU_CMD_QUIT     11
#define MACEMU_CMD_STOP     12

// Mouse input (8 bytes)
struct MacEmuMouseInput {
    uint8_t type;           // MACEMU_INPUT_MOUSE
    uint8_t flags;          // Reserved
    int16_t x;              // Absolute X coordinate
    int16_t y;              // Absolute Y coordinate
    uint8_t buttons;        // Button state bitmask (bit 0=left, 1=right, 2=middle)
    uint8_t reserved;
};

// Key input (8 bytes)
struct MacEmuKeyInput {
    uint8_t type;           // MACEMU_INPUT_KEY
    uint8_t flags;          // KEY_FLAG_DOWN (1) or KEY_FLAG_UP (2)
    uint8_t mac_keycode;    // ADB keycode
    uint8_t modifiers;      // Modifier state
    uint32_t reserved;
};

// Command (8 bytes)
struct MacEmuCommand {
    uint8_t type;           // MACEMU_CMD_*
    uint8_t reserved[7];
};
```

#### Key Flags

```c
#define KEY_FLAG_DOWN  1
#define KEY_FLAG_UP    2
```

### WebRTC DataChannel Protocol

The web client sends text messages over DataChannel:

| Message | Format | Description |
|---------|--------|-------------|
| Mouse move | `M{dx},{dy}` | Relative mouse movement |
| Mouse down | `D{button}` | Button pressed (0=left, 1=middle, 2=right) |
| Mouse up | `U{button}` | Button released |
| Key down | `K{keycode}` | Key pressed (browser keycode) |
| Key up | `k{keycode}` | Key released |

The server converts browser keycodes to Mac ADB scancodes.

## Color Space Conversion

The emulator converts the Mac framebuffer to I420 before writing to shared memory. This is done using libyuv for SIMD acceleration.

### Supported Formats

| Mac Format | Description | Conversion |
|------------|-------------|------------|
| 32-bit | Big-endian ARGB | `BGRAToI420` |
| 16-bit | Big-endian RGB555 | Manual byte-swap + `ARGBToI420` |
| 8-bit | Indexed (256 colors) | Palette lookup + `ARGBToI420` |
| 4-bit | Indexed (16 colors) | Palette lookup + `ARGBToI420` |
| 2-bit | Indexed (4 colors) | Palette lookup + `ARGBToI420` |
| 1-bit | Indexed (B&W) | Palette lookup + `ARGBToI420` |

### 32-bit Format Note

Mac 32-bit is big-endian ARGB, which means bytes in memory are: A, R, G, B.

libyuv's `BGRAToI420` expects this exact memory layout (despite the confusing name - BGRA refers to the little-endian register representation).

## Server Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    WebRTC Server                             │
│  ┌───────────────┐  ┌───────────────┐  ┌───────────────┐   │
│  │ HTTP Server   │  │ WS Signaling  │  │ Emulator Mgr  │   │
│  │ (port 8000)   │  │ (port 8090)   │  │ (auto-start)  │   │
│  └───────────────┘  └───────────────┘  └───────────────┘   │
│           │                 │                  │             │
│           ▼                 ▼                  ▼             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │                    Main Loop                         │   │
│  │  - Scan for emulators (SHM discovery)               │   │
│  │  - Read I420 frames from SHM                         │   │
│  │  - Apply blur filter (optional, for dithered modes) │   │
│  │  - Encode H.264 with OpenH264                        │   │
│  │  - Send via WebRTC (libdatachannel)                 │   │
│  │  - Forward input from DataChannel to Unix socket    │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

## Building

### Server

```bash
cd web-streaming
make
```

Dependencies: libdatachannel (built as submodule), OpenH264, libyuv, OpenSSL

### Emulator with IPC

```bash
cd BasiliskII/src/Unix
./configure --enable-ipc-video
make
```

## Running

### Option 1: Server manages emulator (recommended)

```bash
cd web-streaming
./build/macemu-webrtc
```

Server auto-discovers emulator in PATH, starts it, manages lifecycle.

### Option 2: Manual startup

Terminal 1:
```bash
cd web-streaming
./build/macemu-webrtc --no-auto-start
```

Terminal 2:
```bash
./BasiliskII --config myprefs
# Emulator creates SHM and socket, server auto-connects
```

## Configuration

### Emulator Prefs

```
screen ipc/640/480    # Use IPC video driver at 640x480
```

### Server Command Line

| Option | Default | Description |
|--------|---------|-------------|
| `-p, --http-port` | 8000 | HTTP server port |
| `-s, --signaling` | 8090 | WebSocket signaling port |
| `-e, --emulator` | auto | Path to emulator executable |
| `-P, --prefs` | basilisk_ii.prefs | Prefs file path |
| `-n, --no-auto-start` | false | Don't auto-start emulator |
| `-t, --test-pattern` | false | Generate test pattern (no emulator) |
| `--pid` | auto | Connect to specific emulator PID |

## Benefits

1. **Process isolation**: Server can restart without affecting emulator
2. **Shared codebase**: Same server works with BasiliskII and SheepShaver
3. **Resource management**: Encoding load is separate from emulation
4. **Zero-copy video**: I420 written directly to shared memory
5. **Automatic discovery**: Server finds running emulators automatically
6. **Clean protocol**: Binary messages, no parsing overhead

## Video Pipeline

```
┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
│ Emulator │───►│ Shared   │───►│ Blur     │───►│ H.264    │───►│ WebRTC │
│ (RGB→I420)    │ Memory   │    │ Filter   │    │ Encoder  │    │ RTP    │
└──────────┘    └──────────┘    └──────────┘    └──────────┘    └────────┘
      │                                               │
   libyuv                                        OpenH264
   conversion                                    ~36KB IDR
                                                 ~3KB P
```

1. Emulator converts Mac framebuffer to I420 (libyuv)
2. Writes I420 to shared memory (triple-buffered)
3. Server reads I420, applies optional blur
4. H.264 encoder compresses frame
5. RTP packetizer sends over WebRTC

## See Also

- [WEBRTC_STREAMING.md](WEBRTC_STREAMING.md) - User-facing documentation
- [web-streaming/server/ipc_protocol.h](../web-streaming/server/ipc_protocol.h) - Protocol header
- [BasiliskII/src/IPC/video_ipc.cpp](../BasiliskII/src/IPC/video_ipc.cpp) - Emulator driver
