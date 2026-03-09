#!/bin/bash
#
# test_boot_to_finder.sh - Verify Mac OS boots to Finder desktop
#
# Usage:
#   tests/test_boot_to_finder.sh [--backend uae|unicorn] [--timeout 30] [--rom /path/to/rom]
#
# Starts the emulator, polls /api/status until boot_phase reaches "Finder",
# then exits 0 (pass) or 1 (timeout/failure).
#
set -euo pipefail

BACKEND="uae"
TIMEOUT=30
ROM="${MACEMU_ROM:-/home/mick/quadra.rom}"
DISK="${MACEMU_DISK:-/home/mick/storage/images/7.6.img}"
PORT=18090        # Use non-default port to avoid conflicts
SIG_PORT=18091    # WebRTC signaling port
BINARY="$(dirname "$0")/../build/mac-phoenix"

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --backend) BACKEND="$2"; shift 2 ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --port) PORT="$2"; SIG_PORT="$((PORT + 1))"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

# Resolve binary path
BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix"

if [[ ! -x "$BINARY" ]]; then
    echo "SKIP: Binary not found: $BINARY (run 'ninja -C build' first)"
    exit 77  # meson skip code
fi

if [[ ! -f "$ROM" ]]; then
    echo "SKIP: ROM not found: $ROM (set MACEMU_ROM env var)"
    exit 77
fi

echo "=== Boot Test: $BACKEND backend, timeout=${TIMEOUT}s ==="
echo "ROM: $ROM"
echo "Port: $PORT"

# Start emulator in background
"$BINARY" --backend "$BACKEND" --timeout "$((TIMEOUT + 5))" \
    --config /dev/null --port "$PORT" --signaling-port "$SIG_PORT" \
    --disk "$DISK" "$ROM" &>/tmp/macemu_test_$$.log &
EMU_PID=$!

cleanup() {
    if kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# Wait for HTTP server to be ready
echo -n "Waiting for server..."
for i in $(seq 1 20); do
    if curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1; then
        echo " ready"
        break
    fi
    if ! kill -0 "$EMU_PID" 2>/dev/null; then
        echo " FAIL: emulator exited early"
        cat /tmp/macemu_test_$$.log | tail -20
        exit 1
    fi
    echo -n "."
    sleep 0.5
done

# Start emulator CPU
echo "Starting CPU..."
curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null 2>&1 || true

# Poll boot_phase until "Finder" or timeout
echo -n "Booting..."
START_TIME=$(date +%s)
LAST_PHASE=""
while true; do
    ELAPSED=$(( $(date +%s) - START_TIME ))
    if [[ $ELAPSED -ge $TIMEOUT ]]; then
        echo ""
        echo "FAIL: Timeout after ${TIMEOUT}s (last phase: $LAST_PHASE)"
        # Print last few lines of emulator output for diagnosis
        echo "--- emulator log tail ---"
        tail -10 /tmp/macemu_test_$$.log
        exit 1
    fi

    STATUS=$(curl -sf "http://localhost:$PORT/api/status" 2>/dev/null || echo "{}")
    PHASE=$(echo "$STATUS" | grep -oP '"boot_phase"\s*:\s*"\K[^"]+' || echo "unknown")
    CHECKLOADS=$(echo "$STATUS" | grep -oP '"checkload_count"\s*:\s*\K[0-9]+' || echo "0")

    if [[ "$PHASE" != "$LAST_PHASE" ]]; then
        echo ""
        echo -n "  [$PHASE @ ${ELAPSED}s, ${CHECKLOADS} resources]"
        LAST_PHASE="$PHASE"
    else
        echo -n "."
    fi

    if [[ "$PHASE" == "Finder" || "$PHASE" == "desktop" ]]; then
        echo ""
        echo "PASS: Reached $PHASE in ${ELAPSED}s ($CHECKLOADS resources loaded)"
        exit 0
    fi

    sleep 1
done
