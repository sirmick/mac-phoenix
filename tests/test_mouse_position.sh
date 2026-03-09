#!/bin/bash
#
# test_mouse_position.sh - Verify mouse position API works after boot
#
# Usage:
#   tests/test_mouse_position.sh [--backend uae] [--timeout 30] [--rom /path/to/rom]
#
# Boots to Finder, tests absolute and relative mouse movement via POST /api/mouse,
# verifies Mac OS low-memory globals reflect the changes.
#
set -euo pipefail

BACKEND="uae"
TIMEOUT=30
ROM="${MACEMU_ROM:-/home/mick/quadra.rom}"
DISK="${MACEMU_DISK:-/home/mick/storage/images/7.6.img}"
PORT=18090
SIG_PORT=18091
BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix"

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
"$BINARY" --backend "$BACKEND" --timeout "$((TIMEOUT + 10))" \
    --config /dev/null --port "$PORT" --signaling-port "$SIG_PORT" \
    --disk "$DISK" "$ROM" &>/tmp/macemu_mouse_test_$$.log &
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

# Wait for boot to reach Finder
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

# --- Absolute mouse tests ---

# Test 3: POST /api/mouse moves cursor to (200, 150)
echo -n "Test 3: Absolute move to (200, 150)... "
MOVE_RESP=$(curl -sf -X POST -H "Content-Type: application/json" \
    -d '{"x": 200, "y": 150}' "http://localhost:$PORT/api/mouse" 2>/dev/null)
if ! echo "$MOVE_RESP" | grep -q '"absolute"'; then
    echo "FAIL: expected absolute mode in response: $MOVE_RESP"
    exit 1
fi
echo "OK: $MOVE_RESP"

# Test 4: verify Mac OS reflects position
echo -n "Test 4: Mac OS reflects absolute position... "
sleep 0.5
MOUSE2=$(curl -sf "http://localhost:$PORT/api/mouse" 2>/dev/null)
X2=$(echo "$MOUSE2" | grep -oP '"x"\s*:\s*\K-?[0-9]+' || echo "")
Y2=$(echo "$MOUSE2" | grep -oP '"y"\s*:\s*\K-?[0-9]+' || echo "")
if [[ "$X2" != "200" || "$Y2" != "150" ]]; then
    echo "FAIL: expected x=200,y=150 got x=$X2,y=$Y2 (full: $MOUSE2)"
    exit 1
fi
echo "OK: x=$X2, y=$Y2"

# Test 5: second absolute move to (400, 300)
echo -n "Test 5: Second absolute move to (400, 300)... "
curl -sf -X POST -H "Content-Type: application/json" \
    -d '{"x": 400, "y": 300}' "http://localhost:$PORT/api/mouse" >/dev/null 2>&1
sleep 0.5
MOUSE3=$(curl -sf "http://localhost:$PORT/api/mouse" 2>/dev/null)
X3=$(echo "$MOUSE3" | grep -oP '"x"\s*:\s*\K-?[0-9]+' || echo "")
Y3=$(echo "$MOUSE3" | grep -oP '"y"\s*:\s*\K-?[0-9]+' || echo "")
if [[ "$X3" != "400" || "$Y3" != "300" ]]; then
    echo "FAIL: expected x=400,y=300 got x=$X3,y=$Y3 (full: $MOUSE3)"
    exit 1
fi
echo "OK: x=$X3, y=$Y3"

# --- Relative mouse tests ---
# Note: Mac OS applies mouse acceleration to relative deltas via the ADB
# handler, so we can't predict exact pixel positions. Instead we verify
# the cursor moved in the correct direction.

# Test 6: set baseline via absolute, then switch to relative
echo -n "Test 6: Set baseline at (200, 200) for relative test... "
curl -sf -X POST -H "Content-Type: application/json" \
    -d '{"x": 200, "y": 200}' "http://localhost:$PORT/api/mouse" >/dev/null 2>&1
sleep 0.5
MOUSE4=$(curl -sf "http://localhost:$PORT/api/mouse" 2>/dev/null)
X4=$(echo "$MOUSE4" | grep -oP '"x"\s*:\s*\K-?[0-9]+' || echo "")
Y4=$(echo "$MOUSE4" | grep -oP '"y"\s*:\s*\K-?[0-9]+' || echo "")
if [[ "$X4" != "200" || "$Y4" != "200" ]]; then
    echo "FAIL: expected x=200,y=200 got x=$X4,y=$Y4"
    exit 1
fi
echo "OK: x=$X4, y=$Y4"

# Test 7: relative move with positive deltas — cursor should move right and down
echo -n "Test 7: Relative move (+20, +20) moves cursor right/down... "
MOVE_REL=$(curl -sf -X POST -H "Content-Type: application/json" \
    -d '{"dx": 20, "dy": 20}' "http://localhost:$PORT/api/mouse" 2>/dev/null)
if ! echo "$MOVE_REL" | grep -q '"relative"'; then
    echo "FAIL: expected relative mode in response: $MOVE_REL"
    exit 1
fi
sleep 0.5
MOUSE5=$(curl -sf "http://localhost:$PORT/api/mouse" 2>/dev/null)
X5=$(echo "$MOUSE5" | grep -oP '"x"\s*:\s*\K-?[0-9]+' || echo "")
Y5=$(echo "$MOUSE5" | grep -oP '"y"\s*:\s*\K-?[0-9]+' || echo "")
if [[ "$X5" -le "$X4" || "$Y5" -le "$Y4" ]]; then
    echo "FAIL: cursor didn't move right/down: was ($X4,$Y4), now ($X5,$Y5)"
    exit 1
fi
echo "OK: ($X4,$Y4) -> ($X5,$Y5)"

# Test 8: relative move with negative deltas — cursor should move left and up
echo -n "Test 8: Relative move (-20, -20) moves cursor left/up... "
X_BEFORE=$X5
Y_BEFORE=$Y5
curl -sf -X POST -H "Content-Type: application/json" \
    -d '{"dx": -20, "dy": -20}' "http://localhost:$PORT/api/mouse" >/dev/null 2>&1
sleep 0.5
MOUSE6=$(curl -sf "http://localhost:$PORT/api/mouse" 2>/dev/null)
X6=$(echo "$MOUSE6" | grep -oP '"x"\s*:\s*\K-?[0-9]+' || echo "")
Y6=$(echo "$MOUSE6" | grep -oP '"y"\s*:\s*\K-?[0-9]+' || echo "")
if [[ "$X6" -ge "$X_BEFORE" || "$Y6" -ge "$Y_BEFORE" ]]; then
    echo "FAIL: cursor didn't move left/up: was ($X_BEFORE,$Y_BEFORE), now ($X6,$Y6)"
    exit 1
fi
echo "OK: ($X_BEFORE,$Y_BEFORE) -> ($X6,$Y6)"

echo "PASS: Mouse API working (8 tests: absolute + relative)"
exit 0
