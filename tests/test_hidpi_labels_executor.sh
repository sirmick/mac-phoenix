#!/bin/bash
#
# test_hidpi_labels_executor.sh — desktop labels stay sharp at 2x after a
# window has been over part of them.
#
# Executor on Mac OS 8.6 with --executor-scale 2 (Skia) and the MacAddict16
# CD: its Finder window dragged right until it covers the left of the
# desktop's right-hand labels ("Browse the Internet", "Get QuickTime Pro"),
# then closed (zooming into the CD's icon, in the labels' column). The
# Finder draws again only what the window covered: the labels' runs have
# to outlive being covered in part, or pieces of them show at 1x. Passes
# when the labels' area is as it was before (2x screenshots).
#
# Usage: tests/test_hidpi_labels_executor.sh [--timeout N] [--port N]
#
# Skips (exit 77) without the 8.6 image, the CD (macaddict-016-1997-12.iso)
# or a build with -DEXECUTOR_SKIA=ON.
set -euo pipefail

TIMEOUT=120
PORT=18544
BINARY="${MACEMU_BINARY:-$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port)    PORT="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done
source "$(dirname "$0")/lib/ports.sh"; PORT=$(mp_port "$PORT") || exit 1

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="${TMPDIR:-/tmp}/hidpi-labels-test-$$"

if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
DISK="$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" 8.6 $$ 2>/dev/null || true)"
if [[ -z "$DISK" || ! -f "$DISK" ]]; then echo "SKIP: no 8.6 image"; exit 77; fi
NAME="$(basename "$DISK")"
CD="${MACEMU_STORAGE:-$HOME/storage}/images/macaddict-016-1997-12.iso"
[[ -f "$CD" ]] || { echo "SKIP: no $CD"; exit 77; }

EXTFS_DIR=$(mktemp -d ${TMPDIR:-/tmp}/machidpilabels-extfs.XXXXXX)
mkdir -p "$OUT"
cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    rm -rf "$EXTFS_DIR"
    [[ "$NAME" == test-8.6-$$.img ]] && rm -f "$DISK"
}
trap cleanup EXIT SIGTERM SIGINT

api() { curl -sf --max-time 30 -X POST "http://localhost:$PORT/api/$1" -H 'Content-Type: application/json' -d "$2"; }
mouse() { api mouse "$1" >/dev/null; sleep 0.4; }

echo "=== Desktop labels at 2x after a window over part of them (8.6) ==="
"$BINARY" --config /dev/null --timeout "$((TIMEOUT + 60))" --bridge --network none \
    --backend executor --executor-ppc --executor-system "$NAME" --executor-scale 2 \
    --cdrom "$CD" --port "$PORT" --extfs "$EXTFS_DIR" &>"$OUT/emulator.log" &
EMU_PID=$!
for _ in $(seq 1 30); do
    curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1 && break
    kill -0 "$EMU_PID" 2>/dev/null || { echo "FAIL: emulator died"; tail -20 "$OUT/emulator.log"; exit 1; }
    sleep 0.5
done
grep -q "built without Skia" "$OUT/emulator.log" && { echo "SKIP: built without Skia"; exit 77; }
curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null
for _ in $(seq 1 "$TIMEOUT"); do
    curl -sf "http://localhost:$PORT/api/status" | grep -q '"bridge_agent_connected": *true' && break
    kill -0 "$EMU_PID" 2>/dev/null || { echo "FAIL: emulator died"; tail -20 "$OUT/emulator.log"; exit 1; }
    sleep 1
done
grep -q "built without Skia" "$OUT/emulator.log" && { echo "SKIP: built without Skia"; exit 77; }
sleep 3
curl -sf "http://localhost:$PORT/api/screenshot" > "$OUT/before.png"

api launch '{"path":"MacAddict16:"}' >/dev/null || { echo "FAIL: opening the CD's window"; exit 1; }
sleep 3
# its title bar (the window at 8..390) dragged 176 to the right (its edge at 566, mid-label)
mouse '{"x":120,"y":35}'
mouse '{"x":120,"y":35,"button":0,"down":true}'
mouse '{"x":200,"y":36}'
mouse '{"x":296,"y":36}'
mouse '{"x":296,"y":36,"button":0,"down":false}'
sleep 1.5
curl -sf "http://localhost:$PORT/api/screenshot" > "$OUT/over.png"
mouse '{"x":191,"y":36}'                     # its close box
mouse '{"x":191,"y":36,"button":0,"down":true}'
mouse '{"x":191,"y":36,"button":0,"down":false}'
sleep 2.5
curl -sf "http://localhost:$PORT/api/screenshot" > "$OUT/after.png"

# Pixels of the right-hand labels' area (2x) that differ, and how much the
# window covered of it (it has to have been over them).
read -r OVER DIFF < <(python3 - "$OUT" <<'EOF'
import sys
from PIL import Image, ImageChops
d = sys.argv[1]
box = (1060, 360, 1280, 540)
def count(a, b):
    diff = ImageChops.difference(a.convert('RGB').crop(box), b.convert('RGB').crop(box)).convert('L')
    return sum(1 for v in diff.getdata() if v > 24)
before = Image.open(f'{d}/before.png')
print(count(before, Image.open(f'{d}/over.png')), count(before, Image.open(f'{d}/after.png')))
EOF
)
echo "labels' area: $OVER pixels changed with the window over it, $DIFF after it closed"
echo "Screenshots: $OUT"
[[ "$OVER" -ge 1000 ]] || { echo "FAIL: the window wasn't over the labels"; exit 1; }
[[ "$DIFF" -le 100 ]] || { echo "FAIL: the labels aren't as they were (1x pieces)"; exit 1; }
echo "PASS: the labels are sharp again"
exit 0
