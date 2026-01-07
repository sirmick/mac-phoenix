# WebRTC Integration Status

**Last Updated:** 2026-01-06

## Current State

### ✅ Working Components

1. **Core Infrastructure**
   - HTTP server on port 8000
   - WebSocket signaling server on port 8090
   - CPU thread execution (starts stopped, user must click "Start")
   - Video encoder thread (H.264/VP9/AV1)
   - Audio encoder thread (Opus)
   - API endpoints: `/api/status`, `/api/config`, `/api/emulator/start`, `/api/emulator/stop`

2. **WebRTC Signaling**
   - WebSocket connection established ✓
   - "connect" message received and processed ✓
   - Peer connection created ✓
   - Video track added with H.264RtpPacketizer ✓
   - Audio track added with OpusRtpPacketizer ✓
   - SSRC assigned to tracks ✓
   - "connected" acknowledgment sent ✓
   - ICE gathering completes (state: 2) ✓

### ❌ Not Working

1. **SDP Offer Not Being Sent**
   - `onLocalDescription` callback is registered but **NOT FIRING**
   - Browser stuck on "Receiving offer..."
   - Gathering state reaches 2 (complete) but no SDP generated

## Root Cause Analysis

The `onLocalDescription` callback should fire automatically when libdatachannel generates the SDP after tracks are added. The callback IS registered correctly and matches web-streaming implementation.

**Possible causes:**
1. Callback registered after tracks are added (timing issue)?
2. Missing some libdatachannel initialization?
3. Need to explicitly call something to trigger offer generation?
4. PeerConnection configuration missing something?

## Comparison with web-streaming

### What web-streaming does (WORKING):
```cpp
// server.cpp line 1514-1610
1. Receive "connect" message
2. Create PeerConnection with config
3. Store ws_to_peer_id mapping
4. Store peer
5. Send "connected" ack
6. Register onLocalDescription callback
7. Register onLocalCandidate callback
8. Add tracks (setup_h264_track, setup_audio_track)
   - Each track adds RTP packetizer
   - Each track adds SSRC
9. onLocalDescription fires automatically → sends SDP offer
```

### What macemu-next does (NOT WORKING):
```cpp
// webrtc_server.cpp
1. Receive "connect" message ✓
2. Store ws_to_peer_id mapping ✓
3. Call create_peer_connection() which:
   - Creates PeerConnection ✓
   - Registers onLocalDescription callback ✓
   - Registers onLocalCandidate callback ✓
   - Adds tracks with RTP packetizers ✓
   - Adds SSRC ✓
4. Store peer ✓
5. Send "connected" ack ✓
6. onLocalDescription NEVER FIRES ❌
```

## Key Differences

1. **Callback Registration Timing**
   - web-streaming: Registers callbacks BEFORE adding tracks
   - macemu-next: Registers callbacks in same function AS adding tracks
   - **This might matter!** libdatachannel might generate offer synchronously

2. **PeerConnection Configuration**
   - web-streaming: Sets `maxMessageSize = 16MB`
   - macemu-next: Uses default config
   - Unlikely to affect offer generation

3. **Track Setup**
   - Both use same pattern (addTrack → setMediaHandler → RTP packetizer)
   - Both add SSRC
   - Should be identical

## Next Debugging Steps

### 1. Check Callback Registration Order
Try moving `onLocalDescription` callback registration to BEFORE `create_peer_connection()` is called.

### 2. Add More Logging
```cpp
peer->pc->onLocalDescription([...](rtc::Description desc) {
    fprintf(stderr, "[WebRTC] !!! onLocalDescription FIRED !!!\n");  // Add this
    // ... rest of callback
});
```

### 3. Check if DataChannel Matters
web-streaming creates DataChannel separately. We create it in `create_peer_connection()`. Try:
- Creating DataChannel before tracks?
- Creating DataChannel after tracks?
- Not creating DataChannel at all?

### 4. Compare libdatachannel Versions
```bash
cd macemu-next/subprojects/libdatachannel
git log -1  # Check commit
cd /home/mick/macemu-dual-cpu/web-streaming/libdatachannel
git log -1  # Compare
```

### 5. Try Explicit Offer Generation
libdatachannel might require explicit call:
```cpp
// After adding all tracks, try:
peer->pc->setLocalDescription();  // Force offer generation?
```

### 6. Check State Transitions
Log all state changes:
```cpp
peer->pc->onSignalingStateChange([](rtc::PeerConnection::SignalingState state) {
    fprintf(stderr, "[WebRTC] Signaling state: %d\n", (int)state);
});
```

## Files to Reference

- **Working implementation:** `/home/mick/macemu-dual-cpu/web-streaming/server/server.cpp`
  - Lines 1511-1610: `process_signaling()` with "connect" handler
  - Lines 1586-1602: Track setup functions

- **Our implementation:** `/home/mick/macemu-dual-cpu/macemu-next/src/webrtc/webrtc_server.cpp`
  - Lines 107-167: "connect" message handler
  - Lines 206-354: `create_peer_connection()` function

## Commits Made This Session

1. `1470818c` - Reorganize submodules to subprojects/
2. `ffd63b65` - Fix CPU execution in WebRTC mode + test framework
3. `e0475808` - Implement CPU start/stop state management with condition variable
4. `359cd233` - Fix WebSocket mapping order bug
5. `da3ded24` - Add RTP packetizers (H.264/Opus) from web-streaming
6. `c2325c58` - Fix offer/answer type + add 'connected' ack

## Testing Commands

```bash
# Run with timeout
EMULATOR_TIMEOUT=10 ./build/macemu-next ~/quadra.rom

# Watch WebRTC logs
./build/macemu-next 2>&1 | grep -E "(WebRTC|offer|answer|Local)"

# Test with Python framework
./scripts/test_emulator.py --timeout 5 --save-logs /tmp/test.log
```

## Success Criteria

- [x] WebSocket connection established
- [x] "connect" message processed
- [x] Peer connection created
- [x] Tracks added with RTP packetizers
- [x] "connected" ack sent
- [ ] **onLocalDescription fires and sends SDP offer** ← BLOCKED HERE
- [ ] Browser receives offer
- [ ] Browser sends answer
- [ ] ICE candidates exchanged
- [ ] Connection established
- [ ] Video/audio streaming works
