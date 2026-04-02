# Video Latency Tracing

**Note**: Latency measurement is currently **only supported in PNG codec mode**. H.264 uses RTP video track which doesn't support custom metadata headers for ping timestamps.

## Overview

The system uses two complementary latency measurement mechanisms:

1. **Frame Latency Tracking**: Measures video frame journey from emulator → browser
2. **Ping/Pong RTT Measurement**: Active probing to measure round-trip time with detailed breakdown

## 1. Frame Latency Tracking (PNG Mode Only)

The video frame pipeline has the following stages, each with a timestamp:

```
[Emulator] -> [Server SHM Read] -> [Server Encode] -> [Server Send] -> [Browser Recv] -> [Browser Draw]
    T1             T2                   T3               T4               T5               T6
```

### Timestamps

All timestamps are in **milliseconds since Unix epoch** (compatible with `Date.now()` in JavaScript).

1. **T1 - Frame Ready**: Emulator completes frame write to shared memory
   - Stored in: `MacEmuVideoBuffer.timestamp_us` (converted to ms)
   - Measured by: Emulator

2. **T2 - Server Pickup**: Server wakes from epoll and starts processing frame
   - Captured in: Video loop right after epoll_wait
   - Measured by: Server

3. **T3 - Encode Complete**: Server finishes PNG encoding
   - Captured in: After `png_encoder.encode_bgra_rect()`
   - Measured by: Server

4. **T4 - Send Start**: Server sends frame to DataChannel
   - Captured in: `send_png_frame()` right before transmission
   - Measured by: Server

5. **T5 - Browser Receive**: Browser receives frame from DataChannel
   - Captured in: `handleData()` at function entry
   - Measured by: Browser

6. **T6 - Draw Complete**: Browser finishes decoding PNG and drawing to canvas
   - Captured in: After `ctx.drawImage()`
   - Measured by: Browser

### Latency Calculations

- **SHM Latency**: T2 - T1 (epoll wakeup time)
- **Encode Latency**: T3 - T2 (PNG encoding time)
- **Network Latency**: T5 - T4 (WebRTC transmission + queuing)
- **Decode Latency**: T6 - T5 (PNG decode + canvas draw)
- **Total E2E Latency**: T6 - T1 (complete pipeline)

### Frame Header Format

Extended to 40 bytes to include all timestamps:

```
Offset  Size  Type     Description
------  ----  -------  -----------
0       8     uint64   T1 - Frame ready timestamp (ms since epoch)
8       4     uint32   Dirty rect X
12      4     uint32   Dirty rect Y
16      4     uint32   Dirty rect width
20      4     uint32   Dirty rect height
24      4     uint32   Full frame width
28      4     uint32   Full frame height
32      8     uint64   T4 - Send timestamp (ms since epoch)
```

All values are little-endian.

### Stats Output

Stats will show the latency breakdown:

```
[Server] fps=25 | latency: shm=0.1ms enc=0.5ms | ...
[Browser] fps=25 | latency: net=2.5ms draw=1.2ms e2e=4.3ms
```

## 2. Ping/Pong RTT Measurement (PNG Mode Only)

Active latency measurement system that sends periodic ping messages and measures round-trip time with detailed breakdown at each layer.

### Architecture

The ping/pong system uses a 6-timestamp measurement protocol:

```
[Browser] ---ping(t1)---> [Server] ---ping(t1,t2)---> [Emulator]
                             ↓                            ↓
                          adds t2                   adds t3, waits for frame
                                                          ↓
                                                    sets t4 on frame complete
                                                          ↓
[Browser] <--frame(t1-t5)-- [Server] <-----(reads t1-t4 from SHM)
    ↓
  adds t6, calculates RTT
```

### Timestamp Flow

1. **browser_send_ms**: Browser sends ping with `performance.now()` timestamp (milliseconds)
2. **server_recv_us**: Server receives ping, adds `CLOCK_REALTIME` timestamp (microseconds)
3. **emulator_recv_us**: Emulator receives ping via IPC, adds `CLOCK_REALTIME` timestamp (microseconds)
4. **frame_ready_us**: Emulator sets timestamp when next frame completes (microseconds)
5. **server_send_us**: Server sends frame with all timestamps embedded, adds send timestamp (microseconds)
6. **browser_recv_ms**: Browser receives frame, adds `performance.now()` timestamp (milliseconds)

### Optimized Implementation

The ping system uses an **optimized atomic write-release pattern** to minimize overhead:

- **Only `ping_sequence` is atomic** - acts as a "ready flag"
- **All timestamps stored in regular struct** - no atomic operations needed
- **Write-release semantics**: Emulator writes timestamps, then atomically stores sequence number
- **Read-acquire semantics**: Server reads sequence atomically, guaranteeing visibility of all timestamps
- **Reduces atomic operations by ~80%** (from 5 atomics to 1 per ping)

### Multi-Frame Echo

To handle packet loss, each ping is **echoed in 5 consecutive frames**:

- Emulator tracks `last_echoed_ping_seq` globally (not in shared memory)
- After setting t4, emulator echoes ping in next 5 frames
- Browser matches first echo, ignores duplicates
- Ensures reliable delivery even with DataChannel packet loss

### Latency Breakdown

The browser calculates latency components:

```javascript
// Total RTT (browser clock only - accurate!)
total_rtt_ms = browser_recv_ms - browser_send_ms

// Server-side latencies (same clock - accurate!)
ipc_latency_ms = (emulator_recv_us - server_recv_us) / 1000.0    // Server → Emulator IPC
frame_wait_ms = (frame_ready_us - emulator_recv_us) / 1000.0     // Wait for next frame
encode_send_ms = (server_send_us - frame_ready_us) / 1000.0      // PNG encode + send

// Network latency (estimated as remainder)
network_ms = total_rtt_ms - (ipc_latency_ms + frame_wait_ms + encode_send_ms)
```

### Ping Data Structures

**Shared Memory (MacEmuVideoBuffer)**:
```c
struct {
    uint64_t browser_send_ms;    // Browser send time (ms)
    uint64_t server_recv_us;     // Server receive time (μs)
    uint64_t emulator_recv_us;   // Emulator receive time (μs)
    uint64_t frame_ready_us;     // Frame ready time (μs)
} ping_timestamps;               // Regular struct - no atomics

ATOMIC_UINT32 ping_sequence;     // Only this is atomic (write-release/read-acquire)
```

**Frame Header (bytes 40-83)**:
```
Offset  Size  Type     Description
------  ----  -------  -----------
40      4     uint32   Ping sequence number (0 if no ping)
44      8     uint64   browser_send_ms - Browser send time (ms)
52      8     uint64   server_recv_us - Server receive time (μs)
60      8     uint64   emulator_recv_us - Emulator receive time (μs)
68      8     uint64   frame_ready_us - Frame ready time (μs)
76      8     uint64   server_send_us - Server send time (μs)
```

### Debug Logging

Comprehensive logging at each step:

```
[Browser] Ping #1 sent (browser_send=12345.6ms)
[Server] Ping #1 (browser_send=12345.0ms) forwarded to emulator: OK
[Emulator] Ping #1 ready (frame_ready_us=1766467973491956μs)
[Ping] New echo #1 (browser_send=12.3ms)
[Browser] Ping #1 RTT 15.0ms: net=1.0ms ipc=0.5ms wait=10.0ms enc=3.5ms | avg=15.0ms
```

### Stats Panel Display

The browser stats panel shows:
- **RTT**: Average round-trip time
- **Network**: Browser ↔ Server network latency
- **IPC**: Server → Emulator IPC latency
- **Frame Wait**: Time waiting for emulator to complete a frame
- **Encode**: PNG encoding + transmission time

### Why PNG Only?

**PNG/RAW codecs** use DataChannel with custom metadata headers:
- 84-byte header includes ping timestamps
- Full control over frame format

**H.264 codec** uses RTP video track:
- Raw H.264 NAL units sent via RTP
- No mechanism for custom metadata
- Browser's native WebRTC stack handles decoding
- Would require RTP header extensions or separate DataChannel for ping data

### Future Work

To support ping/pong in H.264 mode:
1. Send ping echo messages via DataChannel (separate from video)
2. Use RTP header extensions (requires libdatachannel support)
3. Implement WebRTC Stats API-based latency measurement
