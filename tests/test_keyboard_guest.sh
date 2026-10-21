#!/bin/bash
#
# test_keyboard_guest.sh — host→guest keystroke roundtrip.
#
# Boots the emulator, launches MacPhoenixSuite (tests/guest/suite) with
# only its keyboard section via BridgeAgent, waits for its "ready" marker,
# then POSTs Mac ADB scancodes via /api/keypress to type "hello 123\n". The
# app reads the keyDown events and writes the received character codes
# back. We assert host-sent vs guest-received match.
#
# Usage:
#   tests/test_keyboard_guest.sh [--timeout N] [--port N] [--rom PATH]
#                                [--disk PATH] [--backend NAME]
#
# Skips (exit 77) if ROM or disk image isn't available.
set -euo pipefail

TIMEOUT=60
PORT=18096
BACKEND="uae"
BINARY="${MACEMU_BINARY:-$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix}"
GUEST_DIR="$(cd "$(dirname "$0")" && pwd)/guest"
ROM_OVERRIDE=""
DISK_OVERRIDE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port)    PORT="$2";    shift 2 ;;
        --rom)     ROM_OVERRIDE="$2"; shift 2 ;;
        --disk)    DISK_OVERRIDE="$2"; shift 2 ;;
        --backend) BACKEND="$2"; shift 2 ;;   # uae | executor
        --core|--executor-cpu) CORE="$2"; shift 2 ;;   # CPU core: uae | mame-68k
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

ROM="${ROM_OVERRIDE:-${MACEMU_ROM:-$HOME/roms/quadra.rom}}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [[ -n "$DISK_OVERRIDE" ]]; then
    DISK="$DISK_OVERRIDE"
else
    DISK="${MACEMU_DISK:-$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" tests-7.5.5 $$ 2>/dev/null || true)}"
fi

DISK_FLAGS=(--disk "$DISK")
EXTRA_FLAGS=()
if [[ "$BACKEND" == "executor" ]]; then
    # No ROM: Executor runs on the test copy's System Folder and mounts the
    # copy itself, writable (see test_guest_suite.sh).
    ROM=""
    DISK_FLAGS=()
    EXTRA_FLAGS+=(--executor-system "$(basename "${DISK:-}")" --executor-writable-images)
fi

if [[ ! -x "$BINARY" ]];     then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
if [[ "$BACKEND" != "executor" && ! -f "$ROM" ]]; then echo "SKIP: ROM not found: $ROM"; exit 77; fi
if [[ -z "${DISK:-}" || ! -f "$DISK" ]]; then echo "SKIP: Disk not found: ${DISK:-<unset>}"; exit 77; fi

SUITE_APP="$GUEST_DIR/suite/MacPhoenixSuite.bin"
[[ -f "$SUITE_APP" ]] || { echo "SKIP: $SUITE_APP not built"; exit 77; }

EXTFS_DIR=$(mktemp -d ${TMPDIR:-/tmp}/mackbtest-extfs.XXXXXX)
python3 "$GUEST_DIR/install_macbinary.py" "$SUITE_APP" "$EXTFS_DIR" MacPhoenixSuite
printf 'test_results.txt\rkeyboard\r' > "$EXTFS_DIR/suite.cfg"

cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    rm -rf "$EXTFS_DIR"
}
trap cleanup EXIT SIGTERM SIGINT

echo "=== Keyboard Roundtrip Test ==="
echo "Backend: $BACKEND  ROM: $ROM  Disk: $DISK  Port: $PORT"

[[ -n "${CORE:-}" ]] && EXTRA_FLAGS+=(--core "$CORE")
"$BINARY" --backend "$BACKEND" --timeout "$((TIMEOUT + 10))" \
    --config /dev/null --dismiss-shutdown-dialog --headless-http \
    --port "$PORT" "${DISK_FLAGS[@]}" --extfs "$EXTFS_DIR" \
    "${EXTRA_FLAGS[@]}" ${ROM:+"$ROM"} &>${TMPDIR:-/tmp}/mackbtest_$$.log &
EMU_PID=$!

# Wait for HTTP
echo -n "HTTP up..."
for i in $(seq 1 20); do
    curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1 && { echo " ok"; break; }
    kill -0 "$EMU_PID" 2>/dev/null || { echo " emulator died"; tail -20 ${TMPDIR:-/tmp}/mackbtest_$$.log; exit 1; }
    sleep 0.5
done

curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null

# Wait for boot
echo -n "Booting..."
START=$(date +%s)
while true; do
    ELAPSED=$(( $(date +%s) - START ))
    [[ $ELAPSED -ge $((TIMEOUT / 2)) ]] && { echo " boot timeout"; exit 1; }
    PHASE=$(curl -sf "http://localhost:$PORT/api/status" 2>/dev/null \
        | grep -oP '"boot_phase"\s*:\s*"\K[^"]+' || echo unknown)
    [[ "$PHASE" == "Finder" || "$PHASE" == "desktop" ]] && { echo " $PHASE (${ELAPSED}s)"; break; }
    sleep 1
done
sleep 2  # Finder settle

# BridgeAgent says hello once it is running (Startup Items); the
# boot phase can say Finder before that (Executor reports it as Finder
# starts), so wait for the agent before launching.
for i in $(seq 1 $((TIMEOUT / 2))); do
    curl -sf "http://localhost:$PORT/api/status" | grep -q "\"bridge_agent_connected\": true" && break
    sleep 1
done

# Launch the suite (keyboard section only): it opens a window in front and
# reads keyDown events; ADB keystrokes from /api/keypress land there.
echo "Launching MacPhoenixSuite (keyboard)..."
LAUNCH=$(curl -sf --max-time 30 -X POST "http://localhost:$PORT/api/launch" \
    -H "Content-Type: application/json" -d '{"path":"Host:MacPhoenixSuite"}' || echo '{}')
echo "$LAUNCH" | grep -q '"success": true' || { echo "FAIL: launch"; echo "$LAUNCH"; exit 1; }

# Wait for the app to write the "ready" marker file. Until that's
# there, the app is still launching and any keys we send would go to
# the Finder instead.
echo -n "Waiting for guest ready..."
READY="$EXTFS_DIR/test_keyboard_ready.txt"
for i in $(seq 1 60); do
    [[ -f "$READY" ]] && { echo " ready (${i}s)"; break; }
    sleep 1
done
[[ -f "$READY" ]] || { echo " timeout"; exit 1; }
sleep 1  # ensure <STDIN> is fully blocking before we type

# Send the keystrokes: "hello 123" + Return.
# Mac ADB scancodes (classic, NOT modern HIToolbox) — see the table in
# client.js EVENT_CODE_TO_MAC. The expected ASCII-byte output is what
# Sioux's text-input layer hands to <STDIN>.
#
#   key       scancode  ASCII
#   ---       --------  -----
#   h         0x04      104
#   e         0x0e      101
#   l         0x25      108
#   l         0x25      108
#   o         0x1f      111
#   space     0x31       32
#   1         0x12       49
#   2         0x13       50
#   3         0x14       51
#   Return    0x24      (line terminator, not in payload)
EXPECTED="104,101,108,108,111,32,49,50,51"
SCANCODES=(0x04 0x0e 0x25 0x25 0x1f 0x31 0x12 0x13 0x14 0x24)

press_key() {
    local code=$1
    curl -sf -X POST "http://localhost:$PORT/api/keypress" \
        -H 'Content-Type: application/json' \
        -d "{\"key\": $code, \"down\": true}"  >/dev/null
    sleep 0.04
    curl -sf -X POST "http://localhost:$PORT/api/keypress" \
        -H 'Content-Type: application/json' \
        -d "{\"key\": $code, \"down\": false}" >/dev/null
    sleep 0.04
}

echo "Sending ${#SCANCODES[@]} keystrokes..."
for code in "${SCANCODES[@]}"; do press_key $((code)); done

# Wait for the script to write its result file.
echo -n "Waiting for guest result..."
RECV="$EXTFS_DIR/test_keyboard_recv.txt"
for i in $(seq 1 30); do
    [[ -f "$RECV" ]] && { echo " got it (${i}s)"; break; }
    sleep 1
done
if [[ ! -f "$RECV" ]]; then
    echo " timeout"
    echo "--- ExtFS contents at timeout ---"
    ls -la "$EXTFS_DIR"
    exit 1
fi

ACTUAL=$(tr -d '\r\n' < "$RECV")
echo "  expected: $EXPECTED"
echo "  actual:   $ACTUAL"

# Graceful guest shutdown
curl -sf --max-time 10 -X POST "http://localhost:$PORT/api/shutdown" -d '{}' >/dev/null 2>&1 || true
for i in $(seq 1 15); do kill -0 "$EMU_PID" 2>/dev/null || { EMU_PID=""; break; }; sleep 1; done

if [[ "$ACTUAL" == "$EXPECTED" ]]; then
    echo "PASS: Keyboard roundtrip ok"
    exit 0
else
    echo "FAIL: byte mismatch"
    exit 1
fi
