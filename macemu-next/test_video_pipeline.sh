#!/bin/bash
# Test video pipeline - launch emulator and monitor frame flow

echo "=== Testing macemu-next Video Pipeline ==="
echo ""
echo "Instructions:"
echo "1. Open http://localhost:8000 in your browser"
echo "2. Click 'Start' button"
echo "3. Watch for frames below"
echo ""
echo "Looking for:"
echo "  - [VideoRefresh] Frame N: pixels=... (Mac framebuffer contents)"
echo "  - [VideoEncoder] Encoded frame #N: ... (Encoder output)"
echo "  - [VideoEncoder] Sent frame #N to WebRTC (WebRTC transmission)"
echo ""
echo "Press Ctrl+C when done"
echo ""

# Run with unbuffered output, filter for video pipeline messages
stdbuf -o0 -e0 ./build/macemu-next 2>&1 | stdbuf -o0 grep --line-buffered -E \
    "VideoRefresh.*Frame|VideoEncoder.*Encoded frame|VideoEncoder.*Sent frame|WebRTC.*video|pixels=0x|Stats.*FPS"
