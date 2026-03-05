#!/bin/bash
# Boot-to-dialog test: boots the emulator, waits for Finder, kills it,
# boots again, and captures the "not shut down cleanly" dialog.
#
# Usage: ./tests/e2e/boot-to-dialog.sh [uae|unicorn]

set -euo pipefail

BACKEND="${1:-uae}"
PORT=8000
BUILD_DIR="$(dirname "$0")/../../build"
BINARY="$BUILD_DIR/macemu-next"
ROM="/home/mick/quadra.rom"
SCREENSHOT_DIR="/tmp/macemu_boot_test_${BACKEND}"

mkdir -p "$SCREENSHOT_DIR"

echo "=== Boot-to-dialog test: $BACKEND backend ==="

take_screenshot() {
    local name="$1"
    local path="$SCREENSHOT_DIR/${name}.png"
    curl -s -o "$path" -w "%{http_code}" "http://localhost:$PORT/api/screenshot" 2>/dev/null
    echo "$path"
}

wait_for_server() {
    for i in $(seq 1 30); do
        if curl -s "http://localhost:$PORT/api/status" >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
    done
    echo "ERROR: Server didn't start" >&2
    return 1
}

start_emulator() {
    CPU_BACKEND="$BACKEND" "$BINARY" "$ROM" &>/tmp/macemu_${BACKEND}.log &
    EMUPID=$!
    echo "$EMUPID"
}

start_cpu() {
    curl -s -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null
}

stop_emulator() {
    local pid="$1"
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
}

# ---- PASS 1: Boot to Finder, then hard kill ----
echo ""
echo "--- Pass 1: Boot to Finder (dirty shutdown) ---"
EMUPID=$(start_emulator)
echo "PID: $EMUPID"

wait_for_server
start_cpu
echo "CPU started, waiting for boot..."

# Take screenshots every 5 seconds until we see enough progress
for i in $(seq 1 60); do
    sleep 5
    code=$(curl -s -o "$SCREENSHOT_DIR/pass1_${i}.png" -w "%{http_code}" "http://localhost:$PORT/api/screenshot" 2>/dev/null)
    echo "  Screenshot pass1_${i}: HTTP $code (t=${i}x5s)"
    if [ "$code" != "200" ]; then
        continue
    fi
done

echo "Pass 1 complete (${BACKEND}). Hard killing PID $EMUPID..."
stop_emulator "$EMUPID"
sleep 2

# ---- PASS 2: Reboot - should show "not shut down cleanly" dialog ----
echo ""
echo "--- Pass 2: Reboot (expect 'not shut down cleanly' dialog) ---"
EMUPID=$(start_emulator)
echo "PID: $EMUPID"

wait_for_server
start_cpu
echo "CPU started, waiting for dialog..."

for i in $(seq 1 60); do
    sleep 5
    code=$(curl -s -o "$SCREENSHOT_DIR/pass2_${i}.png" -w "%{http_code}" "http://localhost:$PORT/api/screenshot" 2>/dev/null)
    echo "  Screenshot pass2_${i}: HTTP $code (t=${i}x5s)"
    if [ "$code" != "200" ]; then
        continue
    fi
done

echo "Pass 2 complete. Cleaning up..."
stop_emulator "$EMUPID"

echo ""
echo "Screenshots saved to: $SCREENSHOT_DIR"
echo "Done."
