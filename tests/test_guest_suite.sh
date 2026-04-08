#!/bin/bash
#
# test_guest_suite.sh - Run the guest-side Mac test suite
#
# Boots the emulator, launches MacTestSuite via ExtFS, waits for it to
# finish, then reads results from the shared folder.
#
# Usage:
#   tests/test_guest_suite.sh [--timeout 60] [--port 18094] [--disk path]
#                             [--os-version 7.5.5|7.6|8.x|9.x]
#
# Prerequisites:
#   - MacTestSuite binary in tests/guest/ (built with Retro68)
#   - ROM and disk image available
#
set -euo pipefail

TIMEOUT=60
ROM="${MACEMU_ROM:-$HOME/roms/quadra.rom}"
DISK="${MACEMU_DISK:-$HOME/storage/images/macos-7.5.5.img}"
PORT=18094
SIG_PORT=18095
BACKEND="uae"
BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix"
GUEST_DIR="$(cd "$(dirname "$0")" && pwd)/guest"
EXTFS_DIR=""
OS_VERSION=""
EXTRA_FLAGS=()

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port) PORT="$2"; SIG_PORT="$((PORT + 1))"; shift 2 ;;
        --disk) DISK="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --backend) BACKEND="$2"; shift 2 ;;
        --os-version) OS_VERSION="$2"; shift 2 ;;
        --network) EXTRA_FLAGS+=(--network "$2"); shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

# --- Preflight checks ---

if [[ ! -x "$BINARY" ]]; then
    echo "SKIP: Binary not found: $BINARY"
    exit 77
fi

if [[ ! -f "$ROM" ]]; then
    echo "SKIP: ROM not found: $ROM"
    exit 77
fi

if [[ ! -f "$DISK" ]]; then
    echo "SKIP: Disk image not found: $DISK"
    exit 77
fi

GUEST_MACBIN="$GUEST_DIR/MacTestSuite.bin"
GUEST_BINARY="$GUEST_DIR/MacTestSuite"
MACBIN_SCRIPT="$GUEST_DIR/macbin_to_extfs.py"

if [[ ! -f "$GUEST_MACBIN" && ! -f "$GUEST_BINARY" ]]; then
    echo "SKIP: Guest binary not found (build with Retro68 first)"
    echo "  Expected: $GUEST_MACBIN"
    exit 77
fi

# --- Setup ExtFS shared folder ---

EXTFS_DIR=$(mktemp -d /tmp/mactest-extfs.XXXXXX)

if [[ -f "$GUEST_MACBIN" ]]; then
    # MacBinary format — split into data fork + .rsrc/ + .finf/ for ExtFS
    python3 "$MACBIN_SCRIPT" "$GUEST_MACBIN" "$EXTFS_DIR" MacTestSuite
else
    echo "WARNING: Using raw binary without resource fork (launch may fail)"
    cp "$GUEST_BINARY" "$EXTFS_DIR/MacTestSuite"
fi

cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    # Preserve results on failure
    if [[ -f "$EXTFS_DIR/test_results.txt" && ${EXIT_CODE:-1} -ne 0 ]]; then
        echo "--- guest results ---"
        cat "$EXTFS_DIR/test_results.txt"
    fi
    rm -rf "$EXTFS_DIR"
}
trap cleanup EXIT SIGTERM SIGINT

echo "=== Guest Test Suite ==="
[[ -n "$OS_VERSION" ]] && echo "OS: $OS_VERSION"
echo "ROM: $ROM"
echo "Disk: $DISK"
echo "ExtFS: $EXTFS_DIR"
echo "Port: $PORT"

# --- Boot emulator ---

"$BINARY" --backend "$BACKEND" --timeout "$((TIMEOUT + 10))" \
    --config /dev/null --dismiss-shutdown-dialog --headless-http \
    --port "$PORT" --signaling-port "$SIG_PORT" \
    --disk "$DISK" --extfs "$EXTFS_DIR" \
    "${EXTRA_FLAGS[@]}" "$ROM" &>/tmp/mactest_guest_$$.log &
EMU_PID=$!

# Wait for HTTP server
echo -n "Waiting for server..."
for i in $(seq 1 20); do
    if curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1; then
        echo " ready"
        break
    fi
    if ! kill -0 "$EMU_PID" 2>/dev/null; then
        echo " FAIL: emulator exited early"
        tail -20 /tmp/mactest_guest_$$.log
        exit 1
    fi
    echo -n "."
    sleep 0.5
done

# Start CPU
curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null

# --- Wait for boot to desktop ---

echo -n "Booting..."
START_TIME=$(date +%s)
BOOT_TIMEOUT=$((TIMEOUT / 2 > 30 ? TIMEOUT / 2 : 30))
while true; do
    ELAPSED=$(( $(date +%s) - START_TIME ))
    if [[ $ELAPSED -ge $BOOT_TIMEOUT ]]; then
        echo ""
        echo "FAIL: Boot timeout after ${BOOT_TIMEOUT}s"
        tail -10 /tmp/mactest_guest_$$.log
        exit 1
    fi

    PHASE=$(curl -sf "http://localhost:$PORT/api/status" 2>/dev/null \
        | grep -oP '"boot_phase"\s*:\s*"\K[^"]+' || echo "unknown")

    if [[ "$PHASE" == "Finder" || "$PHASE" == "desktop" ]]; then
        echo " $PHASE (${ELAPSED}s)"
        break
    fi
    echo -n "."
    sleep 1
done

# Extra settle time for Finder to fully initialize
sleep 2

# --- Launch guest test app ---

echo "Launching MacTestSuite..."
LAUNCH=$(curl -sf --max-time 10 -X POST "http://localhost:$PORT/api/launch" \
    -d '{"path":"Host:MacTestSuite"}' || echo '{"success":false}')

if ! echo "$LAUNCH" | grep -q '"success": true'; then
    echo "FAIL: Could not launch MacTestSuite"
    echo "  Response: $LAUNCH"
    exit 1
fi

# --- Wait for test app to run and finish ---
#
# The app may run and exit faster than polling can detect CurApName changing.
# So we check BOTH: (a) CurApName != Finder, and (b) results file exists.

echo -n "Waiting for tests..."
TEST_START=$(date +%s)
TEST_TIMEOUT=$((TIMEOUT - (TEST_START - START_TIME)))
[[ $TEST_TIMEOUT -lt 10 ]] && TEST_TIMEOUT=10

RESULTS_FILE="$EXTFS_DIR/test_results.txt"
while true; do
    ELAPSED=$(( $(date +%s) - TEST_START ))
    if [[ $ELAPSED -ge $TEST_TIMEOUT ]]; then
        echo ""
        echo "FAIL: Test execution timeout after ${TEST_TIMEOUT}s"
        exit 1
    fi

    # Check if results file appeared (app finished and wrote results)
    if [[ -f "$RESULTS_FILE" ]]; then
        echo " done (${ELAPSED}s)"
        break
    fi

    echo -n "."
    sleep 1
done

# --- Collect results ---

RESULTS_FILE="$EXTFS_DIR/test_results.txt"
if [[ ! -f "$RESULTS_FILE" ]]; then
    echo "FAIL: No results file found at $RESULTS_FILE"
    echo "  (Test app may have crashed before writing results)"
    exit 1
fi

echo ""
echo "--- Results ---"
cat "$RESULTS_FILE"
echo ""

# --- Parse results ---

# Convert Mac line endings (CR) to Unix (LF) for grep
tr '\r' '\n' < "$RESULTS_FILE" > "${RESULTS_FILE}.unix"
RESULTS_UNIX="${RESULTS_FILE}.unix"

FAIL_COUNT=$(grep -c "^FAIL " "$RESULTS_UNIX" || true)
PASS_COUNT=$(grep -c "^PASS " "$RESULTS_UNIX" || true)
SKIP_COUNT=$(grep -c "^SKIP " "$RESULTS_UNIX" || true)

echo "$PASS_COUNT passed, $FAIL_COUNT failed, $SKIP_COUNT skipped"

if [[ $FAIL_COUNT -gt 0 ]]; then
    EXIT_CODE=1
    echo "FAIL: $FAIL_COUNT test(s) failed"
    exit 1
else
    EXIT_CODE=0
    echo "PASS: All tests passed"
    exit 0
fi
