#!/bin/bash
#
# test_drag_executor.sh — drag and drop across processes on Executor.
#
# Boots Executor on Apple's 7.5.5 System, launches SimpleText through the
# bridge, types a word, selects it with a double-click and drags it onto
# the Finder's desktop with the mouse API. Apple's Drag Manager (the
# System's 'proc' pieces, drag.cpp) tracks the drag in SimpleText, runs
# Finder's drop handlers in Finder's process (CallProcessRoutine) and
# Finder makes a clipping file in the Desktop Folder, which is a host
# directory here: that file is the assertion. Then the clipping is dragged
# back into SimpleText's window (Finder's own drag, SimpleText's receive
# handler), and the emulator must still be running.
#
# Usage:
#   tests/test_drag_executor.sh [--timeout N] [--port N] [--disk PATH]
#                               [--core mame-68k]
#
# Skips (exit 77) if the disk image isn't available.
set -euo pipefail

TIMEOUT=90
PORT=18124
BINARY="${MACEMU_BINARY:-$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix}"
DISK_OVERRIDE=""
CORE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port)    PORT="$2";    shift 2 ;;
        --disk)    DISK_OVERRIDE="$2"; shift 2 ;;
        --core|--executor-cpu) CORE="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
STORAGE="${MACEMU_STORAGE:-$HOME/storage}"

if [[ -n "$DISK_OVERRIDE" ]]; then
    DISK="$DISK_OVERRIDE"
else
    DISK="${MACEMU_DISK:-$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" tests-7.5.5 $$ 2>/dev/null || true)}"
fi

if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
if [[ -z "${DISK:-}" || ! -f "$DISK" ]]; then echo "SKIP: Disk not found: ${DISK:-<unset>}"; exit 77; fi

# The System's working copy: its Desktop Folder is where the clipping goes.
DESKTOP="$STORAGE/executor-systems/$(basename "$DISK")/Desktop Folder"
CLIPPING="$DESKTOP/text clipping"

EXTFS_DIR=$(mktemp -d ${TMPDIR:-/tmp}/macdragtest-extfs.XXXXXX)
cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    rm -rf "$EXTFS_DIR"
}
trap cleanup EXIT SIGTERM SIGINT

api() { curl -sf --max-time 5 -X POST "http://localhost:$PORT/api/$1" -H 'Content-Type: application/json' -d "$2"; }
mouse() { api mouse "$1" >/dev/null; }
press_key() {
    api keypress "{\"key\": $1, \"down\": true}" >/dev/null; sleep 0.04
    api keypress "{\"key\": $1, \"down\": false}" >/dev/null; sleep 0.04
}
# A drag with the left button from one point to another, in ten steps.
drag() {
    local x0=$1 y0=$2 x1=$3 y1=$4
    mouse "{\"x\":$x0,\"y\":$y0}"; sleep 0.3
    mouse '{"button":0,"down":true}'; sleep 0.5
    for i in 1 2 3 4 5 6 7 8 9 10; do
        mouse "{\"x\":$((x0 + (x1 - x0) * i / 10)),\"y\":$((y0 + (y1 - y0) * i / 10))}"
        sleep 0.15
    done
    sleep 0.4
    mouse '{"button":0,"down":false}'
}

echo "=== Drag and Drop Test (Executor) ==="
echo "Disk: $DISK  Port: $PORT  Desktop: $DESKTOP"

EXTRA_FLAGS=(--executor-system "$(basename "$DISK")" --executor-writable-images)
[[ -n "$CORE" ]] && EXTRA_FLAGS+=(--core "$CORE")
"$BINARY" --backend executor --timeout "$((TIMEOUT + 10))" \
    --config /dev/null --dismiss-shutdown-dialog --headless-http \
    --port "$PORT" --extfs "$EXTFS_DIR" "${EXTRA_FLAGS[@]}" &>${TMPDIR:-/tmp}/macdragtest_$$.log &
EMU_PID=$!

echo -n "HTTP up..."
for i in $(seq 1 20); do
    curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1 && { echo " ok"; break; }
    kill -0 "$EMU_PID" 2>/dev/null || { echo " emulator died"; tail -20 ${TMPDIR:-/tmp}/macdragtest_$$.log; exit 1; }
    sleep 0.5
done
curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null

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
for i in $(seq 1 $((TIMEOUT / 2))); do
    curl -sf "http://localhost:$PORT/api/status" | grep -q "\"bridge_agent_connected\": true" && break
    sleep 1
done
sleep 2

[[ -e "$CLIPPING" ]] && { echo "FAIL: a clipping is there before the drag: $CLIPPING"; exit 1; }

echo "Launching SimpleText..."
LAUNCH=$(api launch '{"path":"Macintosh HD:SimpleText"}' || echo '{}')
echo "$LAUNCH" | grep -q '"success": true' || { echo "FAIL: launch"; echo "$LAUNCH"; exit 1; }
for i in $(seq 1 30); do
    APP=$(curl -sf "http://localhost:$PORT/api/app" | grep -oP '"app"\s*:\s*"\K[^"]+' || true)
    [[ "$APP" == "SimpleText" ]] && break
    sleep 1
done
[[ "${APP:-}" == "SimpleText" ]] || { echo "FAIL: SimpleText not in front (${APP:-none})"; exit 1; }
sleep 3

# "hello" (ADB scancodes), then a double-click on it selects the word.
for code in 0x04 0x0e 0x25 0x25 0x1f; do press_key $((code)); done
sleep 1
mouse '{"x":20,"y":52}'; sleep 0.2
mouse '{"button":0,"down":true}'; sleep 0.08; mouse '{"button":0,"down":false}'; sleep 0.08
mouse '{"button":0,"down":true}'; sleep 0.08; mouse '{"button":0,"down":false}'
sleep 2

# The selection to the desktop: the strip below SimpleText's window.
echo "Dragging the selection to the desktop..."
drag 20 52 100 470
echo -n "Waiting for the clipping..."
for i in $(seq 1 15); do
    [[ -e "$CLIPPING" ]] && { echo " there (${i}s)"; break; }
    sleep 1
done
if [[ ! -e "$CLIPPING" ]]; then
    echo " not made"
    ls -la "$DESKTOP" 2>/dev/null || true
    grep -i "drag\|FATAL" ${TMPDIR:-/tmp}/macdragtest_$$.log | tail -10
    exit 1
fi

# And back: Finder's drag of the clipping into SimpleText's window.
echo "Dragging the clipping back into SimpleText..."
drag 100 462 300 250
sleep 4
STATUS=$(curl -sf "http://localhost:$PORT/api/status" || echo '')
echo "$STATUS" | grep -q '"emulator_running": true' || { echo "FAIL: emulator not running after the drop"; exit 1; }
grep -q "FATAL CRASH" ${TMPDIR:-/tmp}/macdragtest_$$.log && { echo "FAIL: crash"; grep -A3 "FATAL" ${TMPDIR:-/tmp}/macdragtest_$$.log | head; exit 1; }

echo "PASS: clipping made on the desktop and dragged back"
curl -sf --max-time 10 -X POST "http://localhost:$PORT/api/shutdown" -d '{}' >/dev/null 2>&1 || true
for i in $(seq 1 15); do kill -0 "$EMU_PID" 2>/dev/null || { EMU_PID=""; break; }; sleep 1; done
exit 0
