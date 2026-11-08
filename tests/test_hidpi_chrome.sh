#!/bin/bash
#
# test_hidpi_chrome.sh — Executor's HiDPI chrome runs (docs/executor/HiDPI.md
# "Phase 2"): platinum chrome drawn as shapes at the display scale.
#
# Boots Executor on Apple's 8.6 System at --executor-scale 2, launches
# ChromeGallery (tests/guest/chrome) through the bridge and compares the 2x
# screen (/api/screenshot) with the 1x frame (?scale=1) scaled up, pixel
# for pixel: inside each piece of chrome (slider, tabs, a push button's
# corner, a title-bar box, the menu bar's corners, a radio button) the 2x
# surface must differ from the 1x frame scaled up, and inside a plain
# face it must not. Text ChromeGallery draws again (a word through an
# empty clip, a line through a clip of half of it) must still be inked
# at 2x. Then the gallery's Press button puts up an alert and its OK is
# clicked: where the button was, the 2x screen must be the 1x desktop
# again (no HiDPI chrome left behind). The boxes are ChromeGallery's (and
# StandardAlert's on a 640x480 screen): keep them in step.
#
# Usage:
#   tests/test_hidpi_chrome.sh [--timeout N] [--port N]
#
# Skips (exit 77) without the 8.6 image, PIL, or a build with Skia
# (-DEXECUTOR_SKIA=ON: the screenshot stays 1x).
set -euo pipefail

TIMEOUT=120
PORT=18474
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
APP="$PROJECT_ROOT/tests/guest/chrome/ChromeGalleryPPC.bin"

if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
if [[ ! -f "$STORAGE/images/8.6.img" ]]; then echo "SKIP: $STORAGE/images/8.6.img not found"; exit 77; fi
if ! python3 -c 'import PIL' 2>/dev/null; then echo "SKIP: python3 PIL not installed"; exit 77; fi

WORK=$(mktemp -d ${TMPDIR:-/tmp}/machidpichrome.XXXXXX)
cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT SIGTERM SIGINT

mkdir -p "$WORK/extfs"
python3 tests/guest/install_macbinary.py "$APP" "$WORK/extfs" ChromeGallery >/dev/null

echo "=== HiDPI chrome (Executor 8.6, scale 2) ==="
"$BINARY" --backend executor --executor-system 8.6.img --executor-ppc --executor-scale 2 \
    --config /dev/null --bridge --timeout "$((TIMEOUT + 10))" \
    --port "$PORT" --extfs "$WORK/extfs" &>"$WORK/emu.log" &
EMU_PID=$!

echo -n "HTTP up..."
for i in $(seq 1 40); do
    curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1 && { echo " ok"; break; }
    kill -0 "$EMU_PID" 2>/dev/null || { echo " emulator died"; tail -20 "$WORK/emu.log"; exit 1; }
    sleep 0.5
done
curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null

echo -n "Bridge..."
for i in $(seq 1 $((TIMEOUT / 2))); do
    curl -sf "http://localhost:$PORT/api/status" | grep -q '"bridge_agent_connected": true' && { echo " ok"; break; }
    kill -0 "$EMU_PID" 2>/dev/null || { echo " emulator died"; tail -20 "$WORK/emu.log"; exit 1; }
    [[ $i -eq $((TIMEOUT / 2)) ]] && { echo " timeout"; exit 1; }
    sleep 2
done

LAUNCH=$(curl -sf --max-time 30 -X POST "http://localhost:$PORT/api/launch" \
    -H 'Content-Type: application/json' -d '{"path":"Host:ChromeGallery"}' || echo '{"success":false}')
echo "$LAUNCH" | grep -q '"success": true' || { echo "FAIL: could not launch ChromeGallery: $LAUNCH"; exit 1; }

# A click at X Y (1x screen coordinates)
click() {
    curl -sf -X POST "http://localhost:$PORT/api/mouse" -d "{\"x\":$1,\"y\":$2}" >/dev/null
    sleep 0.5
    curl -sf -X POST "http://localhost:$PORT/api/mouse" -d '{"button":0,"down":true}' >/dev/null
    sleep 0.3
    curl -sf -X POST "http://localhost:$PORT/api/mouse" -d '{"button":0,"down":false}' >/dev/null
}

# Each box (1x screen coordinates, x0 y0 x1 y1): chrome (differs from the
# 1x frame scaled up), plain (doesn't), or ink (text: dark pixels at 2x).
# STAGE: open (the gallery up) or closed (its window closed).
check() {
    curl -sf "http://localhost:$PORT/api/screenshot" -o "$WORK/2x.png" || return 1
    curl -sf "http://localhost:$PORT/api/screenshot?scale=1" -o "$WORK/1x.png" || return 1
    python3 - "$WORK/1x.png" "$WORK/2x.png" "$1" <<'PY'
import sys
from PIL import Image
a = Image.open(sys.argv[1]).convert('RGB')
b = Image.open(sys.argv[2]).convert('RGB')
if b.size != (2 * a.size[0], 2 * a.size[1]):
    print(f'SKIP: the screen is {b.size}, the frame {a.size}: no display scale (EXECUTOR_SKIA off?)')
    sys.exit(77)
up = a.resize(b.size, Image.NEAREST)
# ChromeGallery's document window: content at (20, 60)
if sys.argv[3] == 'open':
    boxes = [
        ('slider (pointed thumb)', 40, 160, 220, 184, 'chrome'),
        ('north tabs', 280, 200, 440, 224, 'chrome'),
        ('push button corner', 40, 80, 46, 86, 'chrome'),
        ('title-bar close box', 20, 41, 36, 57, 'chrome'),
        ('menu bar left corner', 0, 0, 8, 8, 'chrome'),
        ('menu bar right corner', a.size[0] - 8, 0, a.size[0], 8, 'chrome'),
        ('radio button', 40, 130, 54, 146, 'chrome'),
        ('tab pane face', 300, 240, 400, 260, 'plain'),
        ('word drawn again, clipped away', 326, 155, 360, 167, 'chrome'),
        ('line\'s left half (right half drawn again)', 280, 276, 348, 288, 'ink'),
    ]
else:
    boxes = [('where the alert\'s OK was (pressed, the alert gone)', 470, 210, 498, 226, 'plain')]
pa, pb = up.load(), b.load()
bad = 0
for name, x0, y0, x1, y1, kind in boxes:
    n = 0
    for y in range(2 * y0, 2 * y1):
        for x in range(2 * x0, 2 * x1):
            p, q = pa[x, y], pb[x, y]
            if kind == 'ink':
                n += max(q) < 0x60
            elif max(abs(p[0] - q[0]), abs(p[1] - q[1]), abs(p[2] - q[2])) > 8:
                n += 1
    ok = n >= 8 if kind != 'plain' else n == 0
    bad += not ok
    what = {'chrome': 'pixels differ from the 1x frame scaled up (some expected)',
            'plain': 'pixels differ from the 1x frame scaled up (none expected)',
            'ink': 'dark pixels at 2x (some expected)'}[kind]
    print(f'  {"ok  " if ok else "FAIL"} {name}: {n} {what}')
sys.exit(1 if bad else 0)
PY
}

# STAGE, checked again after a pause if it fails at first
FAILED=0
stage() {
    local rc=0
    check "$1" || rc=$?
    if [[ $rc -eq 1 ]]; then
        echo "  (again, in case the window was still drawing)"
        sleep 5
        rc=0
        check "$1" || rc=$?
    fi
    if [[ $rc -eq 77 ]]; then exit 77; fi
    if [[ $rc -ne 0 ]]; then
        mkdir -p "$STORAGE/tmp"
        cp "$WORK/1x.png" "$STORAGE/tmp/hidpi_chrome-$1-1x.png" 2>/dev/null || true
        cp "$WORK/2x.png" "$STORAGE/tmp/hidpi_chrome-$1-2x.png" 2>/dev/null || true
        echo "FAIL: screenshots in $STORAGE/tmp/hidpi_chrome-$1-{1x,2x}.png"
        FAILED=1
    fi
}

sleep 6
stage open
# Press: an alert; its OK
click 360 180
curl -sf --max-time 20 -X POST "http://localhost:$PORT/api/wait" -d '{"condition":"text=Pressed.","timeout":15}' >/dev/null \
    || echo "  (the alert's text not seen)"
sleep 1
click 465 218
sleep 3
stage alert
[[ $FAILED -eq 0 ]] || exit 1
echo "PASS"
