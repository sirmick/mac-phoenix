#!/bin/bash
#
# test_platinum_86_executor.sh — 8.6's platinum chrome on Executor (Apple's
# 8.6 System), pixel for pixel where the G3 (8.6, mame-ppc) was measured:
#
#  1. The menu bar's apple: the colour apple's rows 2..16 of the 20-high
#     bar, a pixel above the system menus' icons (it was drawn at 3..17,
#     "right at the bottom of the menu bar").
#  2. A Finder window's scroll bar thumb: fixed size with its grip, one
#     black line between it and the pages (it was proportional, plain, and
#     two lines thick against them).
#  3. The desktop's disk icons: 8.6's hard disk icon (they were small white
#     bars: Executor's drive icon, kept for 7.x).
#  4. A short click on a scroll bar's page part pages once (it scrolled to
#     the end: TrackControl repeated without a delay).
#
# Usage:
#   tests/test_platinum_86_executor.sh [--timeout N] [--port N]
#
# KEEP_DIR=DIR keeps the screenshots. Skips (exit 77) without the 8.6
# image or PIL.
set -euo pipefail

TIMEOUT=120
PORT=18518
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

WORK=$(mktemp -d ${TMPDIR:-/tmp}/macplatinum.XXXXXX)
cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    if [[ -n "${KEEP_DIR:-}" ]]; then mkdir -p "$KEEP_DIR"; cp "$WORK"/*.png "$KEEP_DIR"/ 2>/dev/null || true; fi
    rm -rf "$WORK"
}
trap cleanup EXIT SIGTERM SIGINT

echo "=== Platinum chrome (Executor 8.6) ==="
"$BINARY" --backend executor --executor-system 8.6.img --executor-ppc \
    --config /dev/null --screen 640x480 --timeout "$((TIMEOUT + 30))" \
    --bridge --network none --port "$PORT" &>"$WORK/emu.log" &
EMU_PID=$!

URL="http://localhost:$PORT/api"
for i in $(seq 1 40); do
    curl -sf "$URL/status" >/dev/null 2>&1 && break
    kill -0 "$EMU_PID" 2>/dev/null || { echo "FAIL: emulator died"; tail -20 "$WORK/emu.log"; exit 1; }
    sleep 0.5
done
curl -sf -X POST "$URL/emulator/start" >/dev/null
echo -n "Booting..."
for i in $(seq 1 "$TIMEOUT"); do
    curl -sf "$URL/status" | grep -q '"bridge_agent_connected": *true' && break
    kill -0 "$EMU_PID" 2>/dev/null || { echo " emulator died"; tail -20 "$WORK/emu.log"; exit 1; }
    sleep 1
done
curl -sf "$URL/status" | grep -q '"bridge_agent_connected": *true' \
    || { echo " no desktop in ${TIMEOUT}s"; exit 1; }
echo " desktop"
sleep 2

api() { curl -sf --max-time 10 -X POST "$URL/$1" -H 'Content-Type: application/json' -d "$2"; }
shot() { curl -sf "$URL/screenshot" -o "$WORK/$1.png"; }
FAILED=0
fail() { echo "FAIL: $1"; FAILED=1; }

api mouse '{"x":300,"y":300}' >/dev/null    # the pointer off the menu bar
sleep 1
shot desktop

# ---- 1. the apple's rows in the menu bar (columns 14..31: its title)
ROWS=$(python3 - "$WORK/desktop.png" <<'EOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
bar = im.getpixel((5, 10))
rows = [y for y in range(1, 18) if any(im.getpixel((x, y)) != bar for x in range(14, 32))]
print(f"{rows[0]} {rows[-1]}" if rows else "none")
EOF
)
echo "Apple menu icon: rows $ROWS (the G3: 2 16)"
[[ "$ROWS" == "2 16" ]] || fail "the apple is not where the G3 draws it"

# ---- 3. the startup disk's icon (x 575..611, y 96..125): 8.6's hard disk
# icon, platinum greys; Executor's drive icon was a black and white bar
GREYS=$(python3 - "$WORK/desktop.png" <<'EOF2'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
grey = lambda p: p[0] == p[1] == p[2] and 0x80 <= p[0] <= 0xEE
print(sum(1 for x in range(575, 612) for y in range(96, 126) if grey(im.getpixel((x, y)))))
EOF2
)
echo "Macintosh HD icon: $GREYS grey pixels (8.6's hard disk icon: about 300; the bar: 0)"
[[ "$GREYS" -gt 100 ]] || fail "the startup disk's icon is not 8.6's hard disk icon"

# ---- 2. a scroll bar's thumb: the System Folder's window (as the image's
# Finder keeps it: its vertical bar at x 402..417, the up arrow's bottom
# outline at y 82). 8.6's thumb is the bar's width (Smart Scrolling off),
# its outlines shared with the arrow above and the page below (black at
# 82 and 98 only: not 125 and 126 under a proportional thumb), and has four
# grip ridges (#333399 at 87, 89, 91, 93).
api launch '{"path":"Macintosh HD:System Folder"}' >/dev/null || true
sleep 4
api mouse '{"x":300,"y":400}' >/dev/null
sleep 1
shot folder
THUMB=$(python3 - "$WORK/folder.png" <<'EOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
black = [y for y in range(78, 140) if im.getpixel((410, y)) == (0, 0, 0)]
grip = [y for y in range(83, 98) if im.getpixel((410, y)) == (0x33, 0x33, 0x99)]
print(f"black {' '.join(map(str, black))} grip {' '.join(map(str, grip))}")
EOF
)
echo "Scroll bar thumb: $THUMB (the G3: black 82 98 grip 87 89 91 93)"
[[ "$THUMB" == "black 82 98 grip 87 89 91 93" ]] || fail "the thumb is not drawn as the G3 draws it"

# ---- 4. a short click on the page part below the thumb pages once: the
# thumb's top (its first blue pixel) from 83 to 134, as on the G3 (each pass
# of TrackControl's loop called the Finder's action: to the end)
api mouse '{"x":410,"y":200}' >/dev/null; sleep 0.4
api mouse '{"x":410,"y":200,"button":0,"down":true}' >/dev/null; sleep 0.1
api mouse '{"x":410,"y":200,"button":0,"down":false}' >/dev/null; sleep 1.5
api mouse '{"x":300,"y":400}' >/dev/null; sleep 0.5
shot paged
PAGED=$(python3 - "$WORK/paged.png" <<'EOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
print(next((y for y in range(83, 240) if (lambda p: p[2] > p[0] + 40)(im.getpixel((404, y)))), -1))
EOF
)
echo "A short click below the thumb: its top at $PAGED (the G3: 134)"
[[ "$PAGED" == 134 ]] || fail "a short click on the page part did not page once"

[[ $FAILED -eq 0 ]] && echo "PASS: platinum chrome as the G3's"
exit $FAILED
