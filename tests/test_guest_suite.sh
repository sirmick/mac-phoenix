#!/bin/bash
#
# test_guest_suite.sh - Run the guest-side Mac test suite
#
# Boots the emulator, dispatches MacTestSuite.pl to MacPerl via the
# BridgeAgent (Startup Items app), waits for it to finish, then reads
# results from the shared folder.
#
# Usage:
#   tests/test_guest_suite.sh [--timeout 60] [--port 18094] [--disk path]
#                             [--rom path] [--os-version 7.5.5|7.6]
#                             [--network MODE] [--backend uae|kpx|executor]
#                             [--script NAME.pl] [--results FILE]
#
# --script picks another guest script from tests/guest (default
# MacTestSuite.pl); --results is the file it writes on Host: (default
# test_results.txt). Same report format: PASS/FAIL/SKIP lines, then "---".
#
# --backend executor runs on the System Folder of the test copy
# <storage>/images/test-macos-<os-version>.img (no ROM; the copy is mounted
# writable beside it, MacPerl included).
#
# Prerequisites:
#   - Disk image with MacPerl installed and BridgeAgent in Startup Items
#     (provisioning/install_bridge_agent.sh)
#   - ROM and disk image available
#
set -euo pipefail

TIMEOUT=60
PORT=18094
SIG_PORT=18095
BACKEND="uae"
ARCH=""
BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix"
GUEST_DIR="$(cd "$(dirname "$0")" && pwd)/guest"
EXTFS_DIR=""
OS_VERSION=""
ROM_OVERRIDE=""
EXTRA_FLAGS=()
DISMISS=1
NETWORK="socket"
SCRIPT_NAME="MacTestSuite.pl"
RESULTS_NAME="test_results.txt"

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port) PORT="$2"; SIG_PORT="$((PORT + 1))"; shift 2 ;;
        --disk) DISK="$2"; shift 2 ;;
        --rom) ROM_OVERRIDE="$2"; shift 2 ;;
        --backend) BACKEND="$2"; shift 2 ;;
        --executor-cpu) EXEC_CPU="$2"; shift 2 ;;   # uae | musashi (executor only)
        --arch) ARCH="$2"; shift 2 ;;
        --os-version) OS_VERSION="$2"; shift 2 ;;
        --network) NETWORK="$2"; shift 2 ;;
        --no-dismiss) DISMISS=0; shift ;;
        --script) SCRIPT_NAME="$2"; shift 2 ;;
        --results) RESULTS_NAME="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

# --arch was deprecated; backend determines arch. Derive ARCH from BACKEND for
# the ROM/RAM/disk selection logic only (not passed to the binary).
[[ -z "$ARCH" && "$BACKEND" == "kpx" ]] && ARCH=ppc
[[ "$ARCH" == "ppc" ]] && EXTRA_FLAGS+=(--ram 128)
[[ -n "$NETWORK" && "$NETWORK" != "none" ]] && EXTRA_FLAGS+=(--network "$NETWORK")

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

# ROM and disk default by architecture (MacPerl runs on both 68k and PPC;
# our 68k BridgeAgent runs under PPC's built-in 68k emulator).
if [[ -n "$ROM_OVERRIDE" ]]; then
    ROM="$ROM_OVERRIDE"
elif [[ "$ARCH" == "ppc" ]]; then
    ROM="${MACEMU_ROM:-$HOME/storage/roms/g3.rom}"
else
    ROM="${MACEMU_ROM:-$HOME/roms/quadra.rom}"
fi
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [[ "$BACKEND" == "executor" ]]; then
    # No ROM: Executor runs on the System Folder of the test copy of the
    # image (extracted once into <storage>/executor-systems, refreshed each
    # run) and mounts the copy beside it, writable, as the ROM backends'
    # boot disk is.
    [[ -z "${DISK:-}" ]] && DISK="${MACEMU_DISK:-$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" "macos-${OS_VERSION:-7.5.5}")}"
    EXEC_SYSTEM="$(basename "$DISK")"
    ROM=""
    EXTRA_FLAGS+=(--executor-system "$EXEC_SYSTEM" --executor-writable-images)
    EXTRA_FLAGS+=(--executor-cpu "${EXEC_CPU:-uae}")
elif [[ -z "${DISK:-}" ]]; then
    if [[ -n "$OS_VERSION" ]]; then
        IMG_BASE="macos-${OS_VERSION}"
    elif [[ "$ARCH" == "ppc" ]]; then
        IMG_BASE="macos-9.0.4"
    else
        IMG_BASE="macos-7.5.5"
    fi
    DISK="${MACEMU_DISK:-$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" "$IMG_BASE")}"
fi

# --- Preflight checks ---

if [[ ! -x "$BINARY" ]]; then
    echo "SKIP: Binary not found: $BINARY"
    exit 77
fi

if [[ "$BACKEND" != "executor" && ! -f "$ROM" ]]; then
    echo "SKIP: ROM not found: $ROM"
    exit 77
fi

if [[ ! -f "$DISK" ]]; then
    echo "SKIP: Disk image not found: $DISK"
    exit 77
fi

PERL_SCRIPT="$GUEST_DIR/$SCRIPT_NAME"
PERL_INSTALL="$GUEST_DIR/install_perl_test.py"

if [[ ! -f "$PERL_SCRIPT" ]]; then
    echo "SKIP: Perl test script not found: $PERL_SCRIPT"
    exit 77
fi

# --- Setup ExtFS shared folder ---

EXTFS_DIR=$(mktemp -d /tmp/mactest-extfs.XXXXXX)
python3 "$PERL_INSTALL" "$PERL_SCRIPT" "$EXTFS_DIR" "$SCRIPT_NAME"

cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    if [[ -f "$EXTFS_DIR/$RESULTS_NAME" && ${EXIT_CODE:-1} -ne 0 ]]; then
        echo "--- guest results ---"
        cat "$EXTFS_DIR/$RESULTS_NAME"
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
echo "Network: ${NETWORK:-none}"

# --- Boot emulator ---

DISK_FLAGS=(--disk "$DISK")
[[ "$BACKEND" == "executor" ]] && DISK_FLAGS=()  # the System's image is mounted itself

DISMISS_FLAG=()
if [[ $DISMISS -eq 1 ]]; then
    DISMISS_FLAG=(--dismiss-shutdown-dialog)
else
    DISMISS_FLAG=(--no-dismiss-shutdown-dialog)
fi

"$BINARY" --backend "$BACKEND" --timeout "$((TIMEOUT + 10))" \
    --config /dev/null "${DISMISS_FLAG[@]}" --headless-http \
    --port "$PORT" \
    "${DISK_FLAGS[@]}" --extfs "$EXTFS_DIR" \
    "${EXTRA_FLAGS[@]}" ${ROM:+"$ROM"} &>/tmp/mactest_guest_$$.log &
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

sleep 2  # Finder settle

# BridgeAgent writes its heartbeat once it is running (Startup Items); the
# boot phase can say Finder before that (Executor reports it at the first
# frame), so wait for the heartbeat before dispatching.
echo -n "Waiting for BridgeAgent..."
for i in $(seq 1 "$BOOT_TIMEOUT"); do
    if compgen -G "$EXTFS_DIR/MacPhoenix/*/bridge_heartbeat" >/dev/null; then
        echo " up (${i}s)"
        break
    fi
    if [[ $i -eq $BOOT_TIMEOUT ]]; then
        echo " no heartbeat after ${BOOT_TIMEOUT}s (dispatching anyway)"
    fi
    sleep 1
done

# --- Dispatch test script via bridge ---
#
# Use /api/script (generic 'misc'/'dosc') instead of /api/launch with open=true.
# The "Perl path" is just the eval-from-file stub composed here — BridgeAgent
# stays language-agnostic. Stub does the \r→\n fix that MacPerl needs to
# install sub defs from a slurped Mac-text-mode file.
PERL_STUB='open(R,"<Host:'"$SCRIPT_NAME"'")||die "open: $!";local $/;$c=<R>;close R;$c=~tr/\r/\n/;eval $c;die $@ if $@;'
PAYLOAD=$(python3 -c '
import json, sys
print(json.dumps({"creator": "McPL", "script": sys.argv[1]}))
' "$PERL_STUB")

echo "Dispatching $SCRIPT_NAME via MacPerl..."
LAUNCH=$(curl -sf --max-time 15 -X POST "http://localhost:$PORT/api/script" \
    -H "Content-Type: application/json" \
    -d "$PAYLOAD" || echo '{"success":false}')

if ! echo "$LAUNCH" | grep -q '"success": true'; then
    echo "FAIL: Could not dispatch $SCRIPT_NAME to MacPerl"
    echo "  Response: $LAUNCH"
    exit 1
fi

# --- Wait for test app to run and finish ---

echo -n "Waiting for tests..."
TEST_START=$(date +%s)
TEST_TIMEOUT=$((TIMEOUT - (TEST_START - START_TIME)))
[[ $TEST_TIMEOUT -lt 10 ]] && TEST_TIMEOUT=10

RESULTS_FILE="$EXTFS_DIR/$RESULTS_NAME"
while true; do
    ELAPSED=$(( $(date +%s) - TEST_START ))
    if [[ $ELAPSED -ge $TEST_TIMEOUT ]]; then
        echo ""
        echo "FAIL: Test execution timeout after ${TEST_TIMEOUT}s"
        exit 1
    fi

    if [[ -f "$RESULTS_FILE" ]] && grep -q '\-\-\-' "$RESULTS_FILE" 2>/dev/null; then
        echo " done (${ELAPSED}s)"
        break
    fi

    echo -n "."
    sleep 1
done

# --- Collect & parse results ---

echo ""
echo "--- Results ---"
cat "$RESULTS_FILE"
echo ""

tr '\r' '\n' < "$RESULTS_FILE" > "${RESULTS_FILE}.unix"
RESULTS_UNIX="${RESULTS_FILE}.unix"

FAIL_COUNT=$(grep -c "^FAIL " "$RESULTS_UNIX" || true)
PASS_COUNT=$(grep -c "^PASS " "$RESULTS_UNIX" || true)
SKIP_COUNT=$(grep -c "^SKIP " "$RESULTS_UNIX" || true)

echo "$PASS_COUNT passed, $FAIL_COUNT failed, $SKIP_COUNT skipped"

# --- Graceful shutdown via BridgeAgent (ShutDwnPower) ---

echo -n "Shutting down..."
SHUT_RESP=$(curl -sf --max-time 10 -X POST "http://localhost:$PORT/api/shutdown" \
    -d '{}' 2>/dev/null || echo '{"success":false}')
if echo "$SHUT_RESP" | grep -q '"success": true'; then
    for i in $(seq 1 20); do
        if ! kill -0 "$EMU_PID" 2>/dev/null; then
            echo " done (${i}s)"
            EMU_PID=""
            break
        fi
        sleep 1
    done
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        echo " timeout, falling back to kill"
    fi
else
    echo " dispatch failed: $SHUT_RESP"
fi

if [[ $FAIL_COUNT -gt 0 ]]; then
    EXIT_CODE=1
    echo "FAIL: $FAIL_COUNT test(s) failed"
    exit 1
else
    EXIT_CODE=0
    echo "PASS: All tests passed"
    exit 0
fi
