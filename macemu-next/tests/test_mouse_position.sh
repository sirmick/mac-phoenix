#!/bin/bash
#
# test_mouse_position.sh - Verify mouse position API works after boot
#
# Usage:
#   tests/test_mouse_position.sh [--backend uae] [--timeout 30] [--rom /path/to/rom]
#
# Boots to Finder, reads /api/mouse, sends mouse move via /api/emulator/start
# (mouse is set to 0,0 initially by Mac OS, then ADB polling updates it)
#
set -euo pipefail

BACKEND="${CPU_BACKEND:-uae}"
TIMEOUT=30
ROM="${MACEMU_ROM:-/home/mick/quadra.rom}"
PORT=18090
SIG_PORT=18091
BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/macemu-next"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --backend) BACKEND="$2"; shift 2 ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --port) PORT="$2"; SIG_PORT="$((PORT + 1))"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

if [[ ! -x "$BINARY" ]]; then
    echo "SKIP: Binary not found: $BINARY"
    exit 77
fi

if [[ ! -f "$ROM" ]]; then
    echo "SKIP: ROM not found: $ROM"
    exit 77
fi

echo "=== Mouse Position Test: $BACKEND backend ==="

# Start emulator
CPU_BACKEND="$BACKEND" \
EMULATOR_TIMEOUT="$((TIMEOUT + 10))" \
    "$BINARY" --port "$PORT" --signaling-port "$SIG_PORT" "$ROM" &>/tmp/macemu_mouse_test_$$.log &
EMU_PID=$!

cleanup() {
    kill "$EMU_PID" 2>/dev/null || true
    wait "$EMU_PID" 2>/dev/null || true
}
trap cleanup EXIT

# Wait for server
for i in $(seq 1 20); do
    curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1 && break
    sleep 0.5
done

# Start CPU
curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null 2>&1 || true

# Wait for boot to reach at least warm start
echo -n "Waiting for boot..."
START_TIME=$(date +%s)
while true; do
    ELAPSED=$(( $(date +%s) - START_TIME ))
    if [[ $ELAPSED -ge $TIMEOUT ]]; then
        echo " FAIL: Timeout"
        exit 1
    fi
    PHASE=$(curl -sf "http://localhost:$PORT/api/status" 2>/dev/null | grep -oP '"boot_phase"\s*:\s*"\K[^"]+' || echo "unknown")
    if [[ "$PHASE" == "Finder" || "$PHASE" == "desktop" ]]; then
        echo " $PHASE reached"
        break
    fi
    echo -n "."
    sleep 1
done

# Test 1: mouse API returns 200
echo -n "Test 1: GET /api/mouse returns JSON... "
MOUSE=$(curl -sf "http://localhost:$PORT/api/mouse" 2>/dev/null)
if [[ -z "$MOUSE" ]]; then
    echo "FAIL: empty response"
    exit 1
fi
echo "OK: $MOUSE"

# Test 2: response has x and y fields
echo -n "Test 2: Response contains x and y... "
X=$(echo "$MOUSE" | grep -oP '"x"\s*:\s*\K-?[0-9]+' || echo "")
Y=$(echo "$MOUSE" | grep -oP '"y"\s*:\s*\K-?[0-9]+' || echo "")
if [[ -z "$X" || -z "$Y" ]]; then
    echo "FAIL: missing x or y in: $MOUSE"
    exit 1
fi
echo "OK: x=$X, y=$Y"

echo "PASS: Mouse API working"
exit 0
