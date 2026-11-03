#!/bin/bash
#
# test_modal_idle_executor.sh — an alert left up idles the host, and its OK
# button still answers (Executor, Apple's 8.6 System).
#
# Executor's ModalDialog polled GetNextEvent without a break: with an alert
# up the emulator took a whole core (the live demo: AppleWorks on a locked
# disk, five "The disk is locked." alerts in a row, as on a G3, each one
# with its button bar's update coming straight back). ModalDialog now waits
# a tick between null events and after an update nobody drew.
#
# Selects Macintosh HD (the read-only 8.6 image) and picks Special > Erase
# Disk..., which answers with a stop alert ("could not be erased, because
# the disk is locked"); measures the emulator's CPU time over 3 seconds,
# and /api/status's cpu_busy (Executor's own measure), then clicks OK and
# checks the alert is gone. While the alert is up the
# menu bar is as a G3's: the application's menus disabled (File drawn
# grey, no Special title selected), Help as it was; after OK, File black
# again (ModalDialogMenuSetup). The coordinates are those of a fresh 8.6
# desktop on a 640x480 screen with no other disk.
#
# Usage:
#   tests/test_modal_idle_executor.sh [--timeout N] [--port N]
#
# KEEP_DIR=DIR keeps the screenshots (before, alert, after).
# Skips (exit 77) without the 8.6 image or PIL.
set -euo pipefail

TIMEOUT=90
PORT=18531
BINARY="${MACEMU_BINARY:-$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port)    PORT="$2";    shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done
source "$(dirname "$0")/lib/ports.sh"; PORT=$(mp_port "$PORT") || exit 1  # per-worktree (docs/Testing.md)

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"
STORAGE="${MACEMU_STORAGE:-$HOME/storage}"

if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
if [[ ! -f "$STORAGE/images/8.6.img" ]]; then echo "SKIP: $STORAGE/images/8.6.img not found"; exit 77; fi
if ! python3 -c 'import PIL' 2>/dev/null; then echo "SKIP: python3 PIL not installed"; exit 77; fi

WORK=$(mktemp -d ${TMPDIR:-/tmp}/macmodalidle.XXXXXX)
cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    if [[ -n "${KEEP_DIR:-}" ]]; then mkdir -p "$KEEP_DIR"; cp "$WORK"/*.png "$KEEP_DIR"/ 2>/dev/null || true; fi
    rm -rf "$WORK"
}
trap cleanup EXIT SIGTERM SIGINT

echo "=== An alert idles the host (Executor 8.6) ==="
"$BINARY" --backend executor --executor-system 8.6.img --executor-ppc \
    --config /dev/null --screen 640x480 --timeout "$((TIMEOUT + 10))" \
    --port "$PORT" &>"$WORK/emu.log" &
EMU_PID=$!

echo -n "HTTP up..."
for i in $(seq 1 40); do
    curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1 && { echo " ok"; break; }
    kill -0 "$EMU_PID" 2>/dev/null || { echo " emulator died"; tail -20 "$WORK/emu.log"; exit 1; }
    sleep 0.5
done
curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null

shot() { curl -sf "http://localhost:$PORT/api/screenshot" -o "$WORK/$1.png"; }
mouse() { curl -sf -X POST "http://localhost:$PORT/api/mouse" -d "$1" >/dev/null; sleep 0.3; }
down() { mouse '{"button":0,"down":true}'; }
up() { mouse '{"button":0,"down":false}'; }
click() { mouse "{\"x\":$1,\"y\":$2}"; down; up; }
# Pixels in a box of a screenshot (x0 y0 x1 y1) that are red (the stop icon)
red() {
    python3 - "$WORK/$1.png" "$2" "$3" "$4" "$5" <<'EOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
x0, y0, x1, y1 = map(int, sys.argv[2:6])
print(sum(1 for x in range(x0, x1) for y in range(y0, y1)
          if (lambda p: p[0] > 150 and p[1] < 80 and p[2] < 80)(im.getpixel((x, y)))))
EOF
}
# CPU ticks (user + system) of the emulator's process tree so far
ticks() {
    local t=0 p
    for p in $EMU_PID $(pgrep -P "$EMU_PID"); do
        [[ -r /proc/$p/stat ]] || continue
        t=$((t + $(awk '{print $14 + $15}' /proc/$p/stat)))
    done
    echo $t
}

# The desktop: Macintosh HD's icon at 577..607 x 108..122, its name below
echo -n "Desktop..."
for i in $(seq 1 $((TIMEOUT * 2))); do
    if grep -q 'boot_phase": "desktop' <(curl -sf "http://localhost:$PORT/api/status" 2>/dev/null); then echo " ok"; break; fi
    kill -0 "$EMU_PID" 2>/dev/null || { echo " emulator died"; tail -20 "$WORK/emu.log"; exit 1; }
    [[ $i -eq $((TIMEOUT * 2)) ]] && { echo " timeout"; exit 1; }
    sleep 0.5
done
sleep 5
click 591 130      # select Macintosh HD
sleep 1
shot before

# Special > Erase Disk... (the menu's third line)
mouse '{"x":180,"y":10}'
down
mouse '{"x":210,"y":65}'
up
sleep 3
shot alert
# (the stop icon at 155..190 x 98..134)
if [[ $(red alert 150 95 195 140) -lt 100 ]]; then
    echo "FAIL: no stop alert after Special > Erase Disk... (see the coordinates in this script)"; exit 1
fi

# Black pixels in a menu title (x0 x1, the bar's text rows)
black() {
    python3 - "$WORK/$1.png" "$2" "$3" <<'EOF2'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
x0, x1 = map(int, sys.argv[2:4])
print(sum(1 for x in range(x0, x1) for y in range(3, 17) if im.getpixel((x, y)) == (0, 0, 0)))
EOF2
}
# File at 40..66, Special 155..205, Help 215..250
if [[ $(black alert 40 66) -gt 0 || $(black alert 155 205) -gt 0 ]]; then
    echo "FAIL: File/Special not disabled under the alert"; exit 1
fi
if [[ $(black alert 215 250) -lt 50 ]]; then
    echo "FAIL: Help disabled under the alert (the system menus stay as they were)"; exit 1
fi

HZ=$(getconf CLK_TCK)
t0=$(ticks); sleep 3; t1=$(ticks)
pct=$(( (t1 - t0) * 100 / (3 * HZ) ))
echo "CPU with the alert up: ${pct}%"
# Executor's own measure (/api/status cpu_busy: its emulator thread's time
# out of its waits, the last half second): low too
busy=$(curl -sf "http://localhost:$PORT/api/status" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("cpu_busy", ""))')
echo "cpu_busy with the alert up: ${busy:-none}"
if [[ -z "$busy" ]] || ! python3 -c "import sys; sys.exit(0 if float('$busy') < 0.5 else 1)"; then
    echo "FAIL: /api/status cpu_busy ${busy:-missing} with an alert up"; exit 1
fi

# OK at 463,167
click 463 167
sleep 2
shot after
if [[ $(red after 150 95 195 140) -gt 0 ]]; then
    echo "FAIL: the alert is still up after clicking OK"; exit 1
fi
if [[ $(black after 40 66) -lt 50 ]]; then
    echo "FAIL: File still disabled after the alert"; exit 1
fi
if [[ $pct -ge 50 ]]; then
    echo "FAIL: the emulator took ${pct}% of a core with an alert up (ModalDialog spinning)"; exit 1
fi
echo "PASS: alert idles (${pct}%), menus disabled meanwhile, OK dismisses it"
