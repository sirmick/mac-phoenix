#!/bin/bash
#
# test_desktop_labels_executor.sh — the 8.6 Finder's desktop labels survive
# an update that covers only part of them (Executor, Apple's 8.6 System).
#
# Inside its desktop update the Finder erases each icon's whole label
# rectangle (DMDrawDesktopRect), then blends the label's box and draws its
# name clipped to the update. The desktop must be painted clipped the same
# way, or the part of a label outside the update is wiped and never drawn
# again (the live demo: a strip of the 'executor' label after AppleWorks,
# whose windows had left a gap over it, quit).
#
# Opens the Macintosh HD window, drags it so its right edge splits the
# "Browse the Internet" and "Get QuickTime Pro" labels, closes it, and
# compares the icon column with the screen before the window opened: it
# must be the same, pixel for pixel. The coordinates are those of a fresh
# 8.6 desktop on a 640x480 screen with no other disk; the steps check that
# the window went where it should.
#
# Usage:
#   tests/test_desktop_labels_executor.sh [--timeout N] [--port N]
#
# KEEP_DIR=DIR keeps the screenshots (before, moved, after).
# Skips (exit 77) without the 8.6 image or PIL.
set -euo pipefail

TIMEOUT=90
PORT=18516
BINARY="${MACEMU_BINARY:-$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port)    PORT="$2";    shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done
source "$(dirname "$0")/lib/ports.sh"; PORT=$(mp_port "$PORT") || exit 1  # a free port (docs/Testing.md "Ports")

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"
STORAGE="${MACEMU_STORAGE:-$HOME/storage}"

if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
if [[ ! -f "$STORAGE/images/8.6.img" ]]; then echo "SKIP: $STORAGE/images/8.6.img not found"; exit 77; fi
if ! python3 -c 'import PIL' 2>/dev/null; then echo "SKIP: python3 PIL not installed"; exit 77; fi

WORK=$(mktemp -d ${TMPDIR:-/tmp}/macdesklabels.XXXXXX)
cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    if [[ -n "${KEEP_DIR:-}" ]]; then mkdir -p "$KEEP_DIR"; cp "$WORK"/*.png "$KEEP_DIR"/ 2>/dev/null || true; fi
    rm -rf "$WORK"
}
trap cleanup EXIT SIGTERM SIGINT

echo "=== Desktop labels after a partial update (Executor 8.6) ==="
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
dblclick() {
    mouse "{\"x\":$1,\"y\":$2}"
    for i in 1 2; do
        curl -sf -X POST "http://localhost:$PORT/api/mouse" -d '{"button":0,"down":true}' >/dev/null; sleep 0.06
        curl -sf -X POST "http://localhost:$PORT/api/mouse" -d '{"button":0,"down":false}' >/dev/null; sleep 0.06
    done
}
# Pixels in a box of a screenshot (x0 y0 x1 y1): dark (a name's) or white
count() {
    python3 - "$WORK/$1.png" "$2" "$3" "$4" "$5" "$6" <<'EOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
x0, y0, x1, y1 = map(int, sys.argv[2:6])
want = (lambda p: sum(p) < 150) if sys.argv[6] == 'dark' else (lambda p: p == (255, 255, 255))
print(sum(1 for x in range(x0, x1) for y in range(y0, y1) if want(im.getpixel((x, y)))))
EOF
}

# The desktop's labels drawn: "Browse the Internet" at 541..643 x 188..201
echo -n "Desktop..."
for i in $(seq 1 $((TIMEOUT * 2))); do
    if shot before 2>/dev/null && [[ $(count before 541 188 640 201 dark) -gt 100 ]]; then echo " ok"; break; fi
    kill -0 "$EMU_PID" 2>/dev/null || { echo " emulator died"; tail -20 "$WORK/emu.log"; exit 1; }
    [[ $i -eq $((TIMEOUT * 2)) ]] && { echo " timeout"; exit 1; }
    sleep 0.5
done
sleep 3
click 300 300      # nothing selected
sleep 1
shot before

# Open Macintosh HD (its window comes up at 8..424 x 27..270)
dblclick 590 115
sleep 3
# Drag it by its title bar 170 right, 90 down: right edge at x 594
mouse '{"x":200,"y":35}'
down
for i in 1 2 3 4 5; do mouse "{\"x\":$((200 + 34 * i)),\"y\":$((35 + 18 * i))}"; done
up
sleep 2
shot moved
# (the window's white content over the labels' left halves)
if [[ $(count moved 545 188 565 201 white) -lt 200 ]]; then
    echo "FAIL: the window did not move over the labels (see the coordinates in this script)"; exit 1
fi
# Close it (its close box is now at 187,125)
click 187 125
sleep 3
shot after

python3 - "$WORK/before.png" "$WORK/after.png" <<'EOF'
import sys
from PIL import Image
a = Image.open(sys.argv[1]).convert('RGB')
b = Image.open(sys.argv[2]).convert('RGB')
box = (541, 150, 640, 330)   # the icons and labels the window had half covered
diff = [(x, y) for x in range(box[0], box[2]) for y in range(box[1], box[3])
        if a.getpixel((x, y)) != b.getpixel((x, y))]
if diff:
    xs = [p[0] for p in diff]; ys = [p[1] for p in diff]
    print(f"FAIL: {len(diff)} pixels of the icon column changed, "
          f"x {min(xs)}..{max(xs)} y {min(ys)}..{max(ys)}")
    sys.exit(1)
print("PASS: labels whole after the window closed")
EOF
