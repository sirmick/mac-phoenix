#!/bin/bash
# Check if Mac framebuffer is actually changing over time

echo "=== Framebuffer Change Detection ==="
echo "Starting emulator..."
echo "1. Open http://localhost:8000"
echo "2. Click 'Start' button"
echo "3. Watch pixel values below - they should CHANGE if Mac is drawing"
echo ""

stdbuf -o0 -e0 ./build/macemu-next 2>&1 | \
    grep --line-buffered "VideoRefresh.*Frame.*pixels" | \
    head -20
