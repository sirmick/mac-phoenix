#!/bin/bash
#
# test_boot_se.sh - Verify Mac SE boots successfully
#
# Usage:
#   tests/test_boot_se.sh [--timeout 30] [--rom /path/to/rom] [--port 18100]
#
# Starts the emulator with a Mac SE ROM, polls /api/status until
# boot_phase reaches "Finder"/"desktop" or checkload_count > 10,
# then exits 0 (pass) or 1 (timeout/failure).
#
set -euo pipefail

TIMEOUT=30
ROM="${MACEMU_SE_ROM:-$HOME/mac-phoenix/Mac_ROMs/68k/256k/1987-03 - B2E362A8 - Mac SE.ROM}"
DISK="${MACEMU_SE_DISK:-$HOME/storage/images/system-6.0.8.img}"
PORT=18100        # Use non-default port to avoid conflicts
SIG_PORT=18101    # WebRTC signaling port
BINARY="$(dirname "$0")/../build/mac-phoenix"

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --port) PORT="$2"; SIG_PORT="$((PORT + 1))"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

# Resolve binary path
BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix"

if [[ ! -x "$BINARY" ]]; then
    echo "SKIP: Binary not found: $BINARY (run 'cmake --build build' first)"
    exit 77  # ctest skip code
fi

if [[ ! -f "$ROM" ]]; then
    echo "SKIP: SE ROM not found: $ROM (set MACEMU_SE_ROM env var)"
    exit 77
fi

if [[ ! -f "$DISK" ]]; then
    echo "SKIP: SE disk not found: $DISK (set MACEMU_SE_DISK env var)"
    exit 77
fi

echo "=== SE Boot Test: timeout=${TIMEOUT}s ==="
echo "ROM: $ROM"
echo "Disk: $DISK"
echo "Port: $PORT"

# Start emulator in background with SE-appropriate flags
"$BINARY" --backend uae --timeout "$((TIMEOUT + 5))" \
    --config /dev/null --dismiss-shutdown-dialog \
    --port "$PORT" --signaling-port "$SIG_PORT" \
    --ram 4 --screen 512x342 \
    --disk "$DISK" "$ROM" &>/tmp/macemu_test_se_$$.log &
EMU_PID=$!

cleanup() {
    if kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT SIGTERM SIGINT

# Wait for HTTP server to be ready
echo -n "Waiting for server..."
for i in $(seq 1 20); do
    if curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1; then
        echo " ready"
        break
    fi
    if ! kill -0 "$EMU_PID" 2>/dev/null; then
        echo " FAIL: emulator exited early"
        tail -20 /tmp/macemu_test_se_$$.log
        exit 1
    fi
    echo -n "."
    sleep 0.5
done

# Start emulator CPU
echo "Starting CPU..."
curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null 2>&1 || true

# Poll boot_phase until "Finder"/"desktop" or checkload_count > 10
echo -n "Booting..."
START_TIME=$(date +%s)
LAST_PHASE=""
while true; do
    ELAPSED=$(( $(date +%s) - START_TIME ))
    if [[ $ELAPSED -ge $TIMEOUT ]]; then
        echo ""
        echo "FAIL: Timeout after ${TIMEOUT}s (last phase: $LAST_PHASE)"
        echo "--- emulator log tail ---"
        tail -10 /tmp/macemu_test_se_$$.log
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

    # SE boots successfully if we reach Finder/desktop
    if [[ "$PHASE" == "Finder" || "$PHASE" == "desktop" ]]; then
        echo ""
        echo "PASS: Reached $PHASE in ${ELAPSED}s ($CHECKLOADS resources loaded)"
        exit 0
    fi

    # SE loads 10+ resources when booting successfully;
    # phase detection may not reach "Finder" so accept high checkload count
    if [[ "$CHECKLOADS" -gt 10 ]]; then
        echo ""
        echo "PASS: Loaded $CHECKLOADS resources in ${ELAPSED}s (phase: $PHASE)"
        exit 0
    fi

    sleep 1
done
