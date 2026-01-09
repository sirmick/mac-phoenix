# One Frame Only - Diagnosis

## What We Know

### ✅ Working
- CPU executing at 60Hz
- video_refresh() called 60 times/sec
- Encoder encoding 60 FPS (381 frames in ~6 seconds)
- WebRTC sending frames
- **Browser decoding 62 frames**

### ❌ Not Working
- **Only ONE frame displays in browser**
- Despite 62 frames decoded, only first frame visible

## Key Evidence

From browser stats:
```json
{
  "packetsRecv": 1201,
  "packetsLost": 13,  // <-- Some packet loss, but minor
  "bytesRecv": 1021468,
  "framesRecv": 62,
  "framesDecoded": 62,  // <-- All received frames decoded!
  "framesDropped": 0,   // <-- None dropped!
  "keyFrames": 1,
  "decodeTime": "0.06s"
}
```

**381 frames encoded, 62 frames decoded** = Big gap (packet loss? or encoder/WebRTC issue)
**62 frames decoded, 1 frame displayed** = Browser rendering issue!

## Theories

### Theory 1: Video Element Not Updating
**Hypothesis**: Browser decodes frames but doesn't paint them to <video> element

**Test**: In browser console, check:
```javascript
const video = document.getElementById('emulator-video');
setInterval(() => {
    console.log('Video time:', video.currentTime, 'readyState:', video.readyState);
}, 1000);
```

If `currentTime` is stuck at 0.0, video isn't playing.

### Theory 2: Browser Paused/Waiting
**Hypothesis**: Video element is in 'waiting' state

**Check**: Video element should fire 'playing' event continuously, not just once

### Theory 3: Canvas Rendering (if PNG mode)
**Hypothesis**: PNG frames decoded but not drawn to canvas

**Only applies if using PNG codec** (you tried WebP, same issue)

### Theory 4: Mac Framebuffer Not Changing
**Hypothesis**: Mac screen genuinely isn't updating (boot stuck)

**Test**: Run `./check_framebuffer.sh` and watch if pixel values change over time:
```
[VideoRefresh] Frame 1: pixels[0]=0xaaaaaaaa [1023]=0xaaaaaaaa [center]=0xaaaaaaaa
[VideoRefresh] Frame 2: pixels[0]=0xaaaaaaaa [1023]=0xaaaaaaaa [center]=0xaaaaaaaa  <-- SAME?
[VideoRefresh] Frame 3: pixels[0]=0xbbbbbbbb [1023]=0xaaaaaaaa [center]=0xaaaaaaaa  <-- DIFFERENT!
```

If pixels stay EXACTLY the same → Mac screen isn't updating
If pixels change → Mac is drawing, browser not displaying

## What to Try

### 1. Check Video Element State (Browser Console)
```javascript
const video = document.querySelector('video');
console.log('Video state:', {
    paused: video.paused,
    ended: video.ended,
    currentTime: video.currentTime,
    readyState: video.readyState,
    videoWidth: video.videoWidth,
    videoHeight: video.videoHeight
});

// Watch for updates
video.ontimeupdate = () => console.log('Time update:', video.currentTime);
```

### 2. Force Video Play (Browser Console)
```javascript
const video = document.querySelector('video');
video.play().then(() => console.log('Playing')).catch(e => console.error('Play failed:', e));
```

### 3. Check If Frames Are Actually Different
Run: `./check_framebuffer.sh` (click Start, watch 20 frames)

Look for pixel values changing

### 4. Compare with Working BasiliskII
- Does BasiliskII use same WebRTC code?
- Or does it use a different video pipeline?
- Maybe it's not using WebRTC at all?

## Expected vs Actual

### Expected (Working System)
```
Frame 1: pixels[0]=0xaaaaaaaa → Browser displays grey dithered
Frame 2: pixels[0]=0xaaaaaaaa → Browser updates (same content)
Frame 3: pixels[0]=0xbbbbbbbb → Browser updates (content changed)
...continuous updates...
```

### Actual (macemu-next)
```
Frame 1: pixels[0]=0x???????? → Browser displays ONE frame
Frame 2-62: ??? → Browser decodes but doesn't display
```

## Next Steps

1. **Verify Mac framebuffer is changing**: `./check_framebuffer.sh`
2. **Check browser video element state**: Console commands above
3. **Compare packet loss**: 13 lost out of 1201 = 1% loss (acceptable for UDP)
4. **Check if this is H.264-specific**: Try with PNG codec
   - Edit `~/.config/macemu-next/config.json`
   - Change `"codec": "h264"` to `"codec": "png"`
   - Restart emulator

## Files to Check

- Client video handling: `/home/mick/macemu-dual-cpu/macemu-next/client/client.js` (lines 1258-1434)
- WebRTC track setup: Check if track.enabled is true
- Video element autoplay: Check if browser autoplay policy blocking updates
