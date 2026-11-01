#!/bin/bash
#
# test_text_hilite_executor.sh — 8.x selects text in the highlight colour
# (Executor, Apple's 8.6 System).
#
# TextEdit hilites its selection in hilite mode, which paints the highlight
# colour (HiliteRGB) for the background. Executor took it from extended
# parameter RAM, zero (black) when nothing was saved there, so a selection
# came out inverted black. 8.6 with none saved uses the Mac OS Default
# theme's lavender, $CCCC $CCCC $FFFF: a G3 on 8.6 with its parameter RAM
# zapped selects text in it.
#
# Launches SimpleText, types "hello world", selects it all (Command-A) and
# counts the selection line's pixels: mostly lavender, almost none black
# but the letters. The coordinates are those of SimpleText's first window
# on a fresh 8.6 desktop on a 640x480 screen.
#
# Usage:
#   tests/test_text_hilite_executor.sh [--timeout N] [--port N]
#
# KEEP_DIR=DIR keeps the screenshot.
# Skips (exit 77) without the 8.6 image or PIL.
set -euo pipefail

TIMEOUT=90
PORT=18532
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

WORK=$(mktemp -d ${TMPDIR:-/tmp}/mactexthilite.XXXXXX)
cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    if [[ -n "${KEEP_DIR:-}" ]]; then mkdir -p "$KEEP_DIR"; cp "$WORK"/*.png "$KEEP_DIR"/ 2>/dev/null || true; fi
    rm -rf "$WORK"
}
trap cleanup EXIT SIGTERM SIGINT

URL="http://localhost:$PORT/api"

echo "=== Text selection in the highlight colour (Executor 8.6) ==="
"$BINARY" --backend executor --executor-system 8.6.img --executor-ppc --bridge \
    --config /dev/null --screen 640x480 --timeout "$((TIMEOUT + 10))" \
    --port "$PORT" &>"$WORK/emu.log" &
EMU_PID=$!

echo -n "HTTP up..."
for i in $(seq 1 40); do
    curl -sf "$URL/status" >/dev/null 2>&1 && { echo " ok"; break; }
    kill -0 "$EMU_PID" 2>/dev/null || { echo " emulator died"; tail -20 "$WORK/emu.log"; exit 1; }
    sleep 0.5
done
curl -sf -X POST "$URL/emulator/start" >/dev/null

echo -n "Bridge..."
for i in $(seq 1 $((TIMEOUT * 2))); do
    if grep -q '"bridge_agent_connected": *true' <(curl -sf "$URL/status" 2>/dev/null); then echo " ok"; break; fi
    kill -0 "$EMU_PID" 2>/dev/null || { echo " emulator died"; tail -20 "$WORK/emu.log"; exit 1; }
    [[ $i -eq $((TIMEOUT * 2)) ]] && { echo " timeout"; exit 1; }
    sleep 0.5
done
curl -sf --max-time 30 -X POST "$URL/launch" -d '{"path":"Macintosh HD:Applications:SimpleText"}' >/dev/null \
    || { echo "FAIL: SimpleText didn't launch"; exit 1; }
curl -sf -X POST "$URL/wait" -d '{"condition":"app=SimpleText","timeout":30}' >/dev/null || true
sleep 3

key() { curl -sf -X POST "$URL/keypress" -d "$1" >/dev/null; sleep 0.12; }
# "hello world" in Mac key codes, then Command-A
for k in 4 14 37 37 31 49 13 31 15 37 2; do key "{\"key\":$k}"; done
key '{"key":55,"down":true}'; key '{"key":0}'; key '{"key":55,"down":false}'
sleep 1
curl -sf "$URL/screenshot" -o "$WORK/selected.png"

# The first line, 10..550 x 45..59
python3 - "$WORK/selected.png" <<'EOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
px = [im.getpixel((x, y)) for x in range(10, 550) for y in range(45, 59)]
lavender = sum(1 for p in px if p == (204, 204, 255))
black = sum(1 for p in px if p == (0, 0, 0))
print(f"selection line: {lavender} lavender, {black} black of {len(px)}")
if lavender < len(px) // 2:
    print("FAIL: the selection isn't drawn in the highlight colour")
    sys.exit(1)
if black > len(px) // 10:
    print("FAIL: the selection is inverted black")
    sys.exit(1)
print("PASS: selection in the highlight colour")
EOF
