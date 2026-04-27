#!/bin/bash
#
# test_keyboard_guest.sh — host→guest keystroke roundtrip.
#
# Boots the emulator, launches MacKeyboardTest.pl in the guest (via
# BridgeAgent), waits for its "ready" marker, then POSTs Mac ADB
# scancodes via /api/keypress to type "hello 123\n". The script reads
# the line from <STDIN> and writes the received ASCII bytes back. We
# assert host-sent vs guest-received match.
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
BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix"
GUEST_DIR="$(cd "$(dirname "$0")" && pwd)/guest"
ROM_OVERRIDE=""
DISK_OVERRIDE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port)    PORT="$2";    shift 2 ;;
        --rom)     ROM_OVERRIDE="$2"; shift 2 ;;
        --disk)    DISK_OVERRIDE="$2"; shift 2 ;;
        --backend) BACKEND="$2"; shift 2 ;;
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
    DISK="${MACEMU_DISK:-$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" macos-7.5.5 2>/dev/null || true)}"
fi

if [[ ! -x "$BINARY" ]];     then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
if [[ ! -f "$ROM" ]];        then echo "SKIP: ROM not found: $ROM"; exit 77; fi
if [[ -z "${DISK:-}" || ! -f "$DISK" ]]; then echo "SKIP: Disk not found: ${DISK:-<unset>}"; exit 77; fi

PERL_SCRIPT="$GUEST_DIR/MacKeyboardTest.pl"
PERL_INSTALL="$GUEST_DIR/install_perl_test.py"
[[ -f "$PERL_SCRIPT" ]] || { echo "SKIP: Perl script missing"; exit 77; }

EXTFS_DIR=$(mktemp -d /tmp/mackbtest-extfs.XXXXXX)
python3 "$PERL_INSTALL" "$PERL_SCRIPT" "$EXTFS_DIR" "MacKeyboardTest.pl"

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

"$BINARY" --backend "$BACKEND" --timeout "$((TIMEOUT + 10))" \
    --config /dev/null --dismiss-shutdown-dialog --headless-http \
    --port "$PORT" --disk "$DISK" --extfs "$EXTFS_DIR" \
    "$ROM" &>/tmp/mackbtest_$$.log &
EMU_PID=$!

# Wait for HTTP
echo -n "HTTP up..."
for i in $(seq 1 20); do
    curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1 && { echo " ok"; break; }
    kill -0 "$EMU_PID" 2>/dev/null || { echo " emulator died"; tail -20 /tmp/mackbtest_$$.log; exit 1; }
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

# Launch the test script — opens MacPerl with our .pl, which becomes front
# and starts blocking on <STDIN>. ADB keystrokes from /api/keypress will
# land in MacPerl's Sioux input area while it's frontmost.
echo "Launching MacKeyboardTest.pl..."
LAUNCH=$(curl -sf --max-time 15 -X POST "http://localhost:$PORT/api/launch" \
    -d '{"path":"Host:MacKeyboardTest.pl","open":true}' || echo '{}')
echo "$LAUNCH" | grep -q '"success": true' || { echo "FAIL: launch"; echo "$LAUNCH"; exit 1; }

# Wait for the script to print the "ready" marker file. Until that's
# there, MacPerl is still launching and any keys we send would go to
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
