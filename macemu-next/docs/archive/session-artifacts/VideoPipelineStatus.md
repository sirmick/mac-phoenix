# Video Pipeline Status

**Last Updated:** 2026-01-08

## Current Status: 🟡 PARTIALLY WORKING

### ✅ What's Working

1. **CPU Execution**: CPU starts when user clicks "Start" button in WebUI
2. **Timer Interrupts**: 60Hz timer firing correctly via timerfd
3. **Video Refresh**: `video_webrtc_refresh()` called 60 times per second
4. **Frame Submission**: Frames submitted from Mac framebuffer to VideoOutput triple buffer
5. **First Frame Rendering**: Browser displays initial frame (grey dithered background + mouse cursor)

### ❌ What's Not Working

1. **Frame Updates**: Only first frame displays, subsequent frames don't appear to update
   - Unknown if this is encoder, WebRTC, or Mac boot issue

## Investigation Needed

### Hypothesis 1: Mac Boot is Slow
- **Symptom**: Grey dithered background is early boot screen
- **Cause**: Mac ROM boot sequence progressing slowly
- **Test**: Wait 30+ seconds, see if screen changes
- **Evidence**: Dithered grey + cursor is normal early Mac boot

### Hypothesis 2: Encoder Not Sending Subsequent Frames
- **Symptom**: Only first frame encoded/sent
- **Cause**: H.264 encoder only sending keyframes, or throttling
- **Test**: Check encoder logs for "Encoded frame #2, #3, ..." messages
- **Evidence**: TBD - need to run with logs

### Hypothesis 3: WebRTC Not Transmitting Updates
- **Symptom**: Frames encoded but not reaching browser
- **Cause**: WebRTC track issue, RTP packetization problem
- **Test**: Check browser console for RTP packets
- **Evidence**: TBD - need browser devtools

### Hypothesis 4: VideoOutput Triple Buffer Issue
- **Symptom**: Frames submitted but not picked up by encoder
- **Cause**: `wait_for_frame()` timing out, or buffer coordination issue
- **Test**: Check for "No frame available (timeout)" messages
- **Evidence**: We saw these before CPU started, but should check after

## Debugging Tools Added

### 1. Framebuffer Pixel Sampling
**Location:** `src/drivers/video/video_webrtc.cpp:191-198`

Samples 3 pixels from framebuffer on first 5 frames:
```
[VideoRefresh] Frame N: pixels[0]=0x... [1023]=0x... [center]=0x...
```

**What to look for:**
- `0x00000000` = Black
- `0xFFFFFFFF` = White
- `0xFFFF00FF` = Yellow (ARGB format issue - should be magenta)
- Changing values = Screen is updating

### 2. Encoder Frame Logging
**Location:** `src/drivers/video/video_encoder_thread.cpp:215-221`

Logs first 10 encoded frames:
```
[VideoEncoder] Encoded frame #N: SIZE bytes, keyframe=0/1, took MS ms
```

**What to look for:**
- Frame count increasing = Encoder is working
- Size > 0 = Encoding successful
- keyframe=1 every ~60 frames = Normal I-frame cadence
- Stalls = Encoder blocked or no input frames

### 3. WebRTC Send Logging
**Location:** `src/drivers/video/video_encoder_thread.cpp:80-82`

Logs every 60th frame sent to WebRTC:
```
[VideoEncoder] Sent frame #N to WebRTC: SIZE bytes, keyframe=0/1
```

**What to look for:**
- Messages appearing = Frames reaching WebRTC
- No messages = send_video_frame() not being called

## Testing Procedure

### Quick Test (Automated)
```bash
cd /home/mick/macemu-dual-cpu/macemu-next
./test_video_pipeline.sh
```

Then in browser:
1. Open http://localhost:8000
2. Click "Start" button
3. Watch terminal for frame flow

### Manual Test (Detailed)
```bash
cd /home/mick/macemu-dual-cpu/macemu-next
./build/macemu-next 2>&1 | tee /tmp/video_debug.log
```

Then in browser:
1. Open http://localhost:8000
2. Click "Start"
3. Wait 30 seconds
4. Press Ctrl+C in terminal

Analyze log:
```bash
# Check if frames are being refreshed
grep "VideoRefresh.*Frame" /tmp/video_debug.log | head -20

# Check if encoder is receiving frames
grep "VideoEncoder.*Encoded frame" /tmp/video_debug.log | head -20

# Check if WebRTC is sending
grep "Sent frame.*WebRTC" /tmp/video_debug.log | head -20

# Check for errors
grep -i "error\|warning\|timeout" /tmp/video_debug.log
```

## Expected Output (Working System)

```
[VideoRefresh] Frame 1: pixels[0]=0xaaaaaaaa [1023]=0xaaaaaaaa [center]=0xaaaaaaaa
[VideoRefresh] Frame 2: pixels[0]=0xaaaaaaaa [1023]=0xaaaaaaaa [center]=0xaaaaaaaa
...
[VideoEncoder] Encoded frame #1: 45678 bytes, keyframe=1, took 12 ms
[VideoEncoder] Encoded frame #2: 1234 bytes, keyframe=0, took 3 ms
[VideoEncoder] Encoded frame #3: 1145 bytes, keyframe=0, took 3 ms
...
[VideoEncoder] Stats: 59.8 FPS, 180 encoded, 0 dropped, encode: 3 ms
[VideoEncoder] Stats: 60.1 FPS, 360 encoded, 0 dropped, encode: 3 ms
```

## Color Format Reference

Mac framebuffer is **ARGB** (big-endian on Mac, little-endian in our host memory view):

| Pixel Value | Color (ARGB) | What It Means |
|-------------|--------------|---------------|
| `0x00000000` | Transparent Black | Cleared framebuffer |
| `0xFFFFFFFF` | Opaque White | White pixel |
| `0xFF808080` | Opaque Grey | Grey (50%) |
| `0xFFAAAAAAAA` | Opaque Grey Dither | Dithered pattern (Mac desktop) |
| `0xFFFF0000` | Opaque Red | Red pixel |
| `0xFF00FF00` | Opaque Green | Green pixel |
| `0xFF0000FF` | Opaque Blue | Blue pixel |
| `0xFFFFFF00` | Opaque Yellow | Yellow pixel |

If colors are wrong (e.g., yellow screen when it should be grey), we have an ARGB/BGRA mixup.

## Next Steps

1. **Run test script** and observe frame flow
2. **Check if pixels[] values are changing** over time
3. **Verify encoder is encoding multiple frames** (not just first)
4. **Check WebRTC send count** matches encoder count
5. **If Mac is stuck in boot**: Add ROM/CPU boot debugging
6. **If encoder stalls**: Check VideoOutput wait_for_frame() logic
7. **If WebRTC stalls**: Check send_video_frame() implementation

## Related Files

- **Video Refresh:** `src/drivers/video/video_webrtc.cpp`
- **Encoder Thread:** `src/drivers/video/video_encoder_thread.cpp`
- **Timer Interrupts:** `src/drivers/platform/timer_interrupt.cpp`
- **WebRTC Server:** `src/webrtc/webrtc_server.cpp`
- **VideoOutput:** `src/drivers/video/video_output.h`

## Known Good Behavior

From working web-streaming implementation, we expect:
- 60 FPS encoding
- Keyframe every ~60 frames (1 second)
- H.264 frames: 1-5 KB for P-frames, 40-60 KB for I-frames (1024x768)
- Frame encoding: 2-5ms with hardware, 10-20ms software
- No dropped frames under normal operation

## Git Commits

- `4dcea574` - Fix WebRTC video streaming - auto-start CPU (REVERTED)
- `584bf893` - Add comprehensive video pipeline debugging (CURRENT)
