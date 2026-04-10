#!/bin/bash
#
# test_command_bridge.sh - Test command bridge API endpoints
#
# Tests /api/app, /api/windows, /api/wait (passive + queue), /api/launch, /api/quit
#
set -euo pipefail

BACKEND="uae"
ARCH=""
TIMEOUT=15
ROM="${MACEMU_ROM:-$HOME/roms/quadra.rom}"
DISK="${MACEMU_DISK:-$HOME/storage/images/macos-7.5.5.img}"
PORT=18092
SIG_PORT=18093
BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix"

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --backend) BACKEND="$2"; shift 2 ;;
        --arch) ARCH="$2"; shift 2 ;;
        --port) PORT="$2"; SIG_PORT="$((PORT + 1))"; shift 2 ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
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

echo "=== Command Bridge Test: $BACKEND backend ==="

EXTRA_FLAGS=()
[[ -n "$ARCH" ]] && EXTRA_FLAGS+=(--arch "$ARCH")
[[ "$ARCH" == "ppc" ]] && EXTRA_FLAGS+=(--ram 128)

cleanup() {
    if [[ -n "${EMU_PID:-}" ]]; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# Start emulator (headless-http works for both m68k and PPC)
"$BINARY" --backend "$BACKEND" --timeout "$TIMEOUT" \
    --config /dev/null --dismiss-shutdown-dialog --headless-http \
    --port "$PORT" --signaling-port "$SIG_PORT" --disk "$DISK" \
    "${EXTRA_FLAGS[@]}" "$ROM" &>/dev/null &
EMU_PID=$!

# Wait for HTTP server
for i in $(seq 1 10); do
    if curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1; then break; fi
    sleep 1
done

# Start the CPU
curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null

# Wait for boot (check both boot_phase and CurApName for PPC compat)
echo -n "Waiting for boot..."
for i in $(seq 1 "$TIMEOUT"); do
    STATUS=$(curl -sf "http://localhost:$PORT/api/status" 2>/dev/null || echo "")
    PHASE=$(echo "$STATUS" | grep -oP '"boot_phase"\s*:\s*"\K[^"]+' || echo "")
    APP=$(echo "$STATUS" | grep -oP '"app"\s*:\s*"\K[^"]+' || echo "")
    if [[ "$PHASE" == "desktop" || "$PHASE" == "Finder" || "$APP" == "Finder" ]]; then
        echo " ${PHASE:-$APP} reached"
        break
    fi
    echo -n "."
    sleep 1
done

PASS=0
FAIL=0

check() {
    local desc="$1" got="$2" expected="$3"
    if echo "$got" | grep -q "$expected"; then
        echo "  OK: $desc"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $desc (expected '$expected', got '$got')"
        FAIL=$((FAIL + 1))
    fi
}

# Test 1: /api/app returns Finder (passive field from shared memory)
echo "Test 1: GET /api/app"
APP=$(curl -s "http://localhost:$PORT/api/app" || echo "CURL_FAILED")
check "/api/app returns JSON" "$APP" '"app"'
check "/api/app shows Finder" "$APP" 'Finder'

# Test 2: /api/windows returns window list (via command queue)
echo "Test 2: GET /api/windows"
WIN=$(curl -s "http://localhost:$PORT/api/windows" || echo "CURL_FAILED")
check "/api/windows returns JSON" "$WIN" '"windows"'

# Test 3: /api/wait with boot=Finder succeeds immediately (already booted)
echo "Test 3: POST /api/wait boot=Finder"
WAIT=$(curl -s -X POST "http://localhost:$PORT/api/wait" -d '{"condition": "boot=Finder", "timeout": 3}' || echo "CURL_FAILED")
check "/api/wait boot=Finder" "$WAIT" '"ok": true'

# Test 4: /api/wait with app=Finder succeeds (passive field)
echo "Test 4: POST /api/wait app=Finder"
WAIT_APP=$(curl -s -X POST "http://localhost:$PORT/api/wait" -d '{"condition": "app=Finder", "timeout": 3}' || echo "CURL_FAILED")
check "/api/wait app=Finder" "$WAIT_APP" '"ok": true'

# Test 5: /api/launch with nonexistent path returns graceful error
echo "Test 5: POST /api/launch (bad path)"
LAUNCH=$(curl -s --max-time 5 -X POST "http://localhost:$PORT/api/launch" -d '{"path": "Macintosh HD:NoSuchApp"}' || echo "CURL_FAILED")
check "/api/launch returns response" "$LAUNCH" '"success"'
check "/api/launch reports failure" "$LAUNCH" '"success": false'

echo ""
if [[ $FAIL -eq 0 ]]; then
    echo "PASS: Command bridge API ($PASS tests)"
    exit 0
else
    echo "FAIL: $FAIL of $((PASS + FAIL)) tests failed"
    exit 1
fi
