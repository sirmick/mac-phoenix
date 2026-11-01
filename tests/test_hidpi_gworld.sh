#!/bin/bash
#
# test_hidpi_gworld.sh — Executor's HiDPI GWorld shadows (docs/executor/HiDPI.md
# "Phase 4"): text drawn offscreen and copied to the screen comes out drawn
# from outlines at the display scale.
#
# Boots Executor on Apple's 8.6 System at --executor-scale 2, launches
# GWorldGallery (tests/guest/gworld) through the bridge and compares the 2x
# screen (/api/screenshot) with the 1x frame (?scale=1) scaled up, pixel
# for pixel: text the gallery draws in a GWorld and copies to its window
# (CopyBits, through a second GWorld, CopyMask's unmasked part) must differ
# from the 1x frame scaled up and show anti-aliasing (greys the 1x frame
# hasn't); text whose GWorld the program then wrote itself (behind
# QuickDraw), and CopyMask's masked-out part, must not differ. The boxes
# are GWorldGallery's: keep them in step. Its second window scrolls text
# three ways: every row of it with text must still be anti-aliased.
#
# Usage:
#   tests/test_hidpi_gworld.sh [--timeout N] [--port N]
#
# Skips (exit 77) without the 8.6 image, PIL, or a build with Skia
# (-DEXECUTOR_SKIA=ON: the screenshot stays 1x).
set -euo pipefail

TIMEOUT=120
PORT=18476
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
APP="$PROJECT_ROOT/tests/guest/gworld/GWorldGalleryPPC.bin"

if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
if [[ ! -f "$STORAGE/images/8.6.img" ]]; then echo "SKIP: $STORAGE/images/8.6.img not found"; exit 77; fi
if ! python3 -c 'import PIL' 2>/dev/null; then echo "SKIP: python3 PIL not installed"; exit 77; fi

WORK=$(mktemp -d ${TMPDIR:-/tmp}/machidpigworld.XXXXXX)
cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT SIGTERM SIGINT

mkdir -p "$WORK/extfs"
python3 tests/guest/install_macbinary.py "$APP" "$WORK/extfs" GWorldGallery >/dev/null

echo "=== HiDPI GWorld shadows (Executor 8.6, scale 2) ==="
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
    -H 'Content-Type: application/json' -d '{"path":"Host:GWorldGallery"}' || echo '{"success":false}')
echo "$LAUNCH" | grep -q '"success": true' || { echo "FAIL: could not launch GWorldGallery: $LAUNCH"; exit 1; }

# Each box (1x screen coordinates, x0 y0 x1 y1): text (differs from the 1x
# frame scaled up, with greys the 1x frame hasn't there) or plain (doesn't
# differ).
check() {
    curl -sf "http://localhost:$PORT/api/screenshot" -o "$WORK/2x.png" || return 1
    curl -sf "http://localhost:$PORT/api/screenshot?scale=1" -o "$WORK/1x.png" || return 1
    python3 - "$WORK/1x.png" "$WORK/2x.png" <<'PY'
import sys
from PIL import Image
a = Image.open(sys.argv[1]).convert('RGB')
b = Image.open(sys.argv[2]).convert('RGB')
if b.size != (2 * a.size[0], 2 * a.size[1]):
    print(f'SKIP: the screen is {b.size}, the frame {a.size}: no display scale (EXECUTOR_SKIA off?)')
    sys.exit(77)
up = a.resize(b.size, Image.NEAREST)
# GWorldGallery's window: content at (20, 60)
boxes = [
    ('Geneva 12 copied from a GWorld', 28, 66, 300, 84, 'text'),
    ('New York 18 bold copied from a GWorld', 28, 90, 220, 116, 'text'),
    ('Times 14 italic copied from a GWorld', 28, 124, 220, 146, 'text'),
    ('copied through a second GWorld', 28, 167, 230, 184, 'text'),
    ('written behind QuickDraw (1x)', 20, 190, 460, 210, 'plain'),
    ('CopyMask, the mask\'s part', 28, 217, 218, 234, 'text'),
    ('CopyMask, masked out', 221, 215, 460, 235, 'plain'),
]
pa, pb = up.load(), b.load()
grey = lambda p: 0x30 < max(p) < 0xD0 and max(p) - min(p) < 0x20
bad = 0
# The Scroll Gallery window (content at (20, 322)): three columns of text
# scrolled 37 pixels after they were drawn (TextEdit's TEScroll on the
# screen; ScrollRect in a GWorld, copied; the window copied onto itself
# and the strip it exposes copied from a GWorld). Every 1x row with text
# in it must be anti-aliased at 2x: a piece of a line left 1x is not.
pa1 = a.load()
for name, x0, x1 in [('TextEdit scrolled (ScrollRect on the screen)', 20, 165),
                     ('ScrollRect in a GWorld, copied', 167, 312),
                     ('copied onto itself + strip from a GWorld', 314, 460)]:
    rows = sharp = 0
    flat = []
    for y in range(324, 456):
        if sum(max(pa1[x, y]) < 0x60 for x in range(x0, x1)) < 3:
            continue
        rows += 1
        if any(grey(pb[x, Y]) for Y in (2 * y, 2 * y + 1) for x in range(2 * x0, 2 * x1)):
            sharp += 1
        else:
            flat.append(y)
    ok = rows >= 60 and sharp == rows
    bad += not ok
    print(f'  {"ok  " if ok else "FAIL"} {name}: {sharp} of {rows} rows with text anti-aliased'
          + (f' (1x: {flat[:8]})' if flat else ''))
for name, x0, y0, x1, y1, kind in boxes:
    n = g = g1 = 0
    for y in range(2 * y0, 2 * y1):
        for x in range(2 * x0, 2 * x1):
            p, q = pa[x, y], pb[x, y]
            n += max(abs(p[0] - q[0]), abs(p[1] - q[1]), abs(p[2] - q[2])) > 8
            g += grey(q)
            g1 += grey(p)
    ok = n >= 50 and g > g1 + 20 if kind == 'text' else n == 0
    bad += not ok
    what = (f'differ from the 1x frame scaled up, {g} grey (1x: {g1})' if kind == 'text'
            else 'differ from the 1x frame scaled up (none expected)')
    print(f'  {"ok  " if ok else "FAIL"} {name}: {n} pixels {what}')
sys.exit(1 if bad else 0)
PY
}

# STAGE, checked again after a pause if it fails at first
FAILED=0
stage() {
    local rc=0
    check || rc=$?
    if [[ $rc -eq 1 ]]; then
        echo "  (again, in case the window was still drawing)"
        sleep 5
        rc=0
        check || rc=$?
    fi
    if [[ $rc -eq 77 ]]; then exit 77; fi
    # HIDPI_SHOTS=DIR: keep the screenshots
    if [[ -n "${HIDPI_SHOTS:-}" ]]; then
        mkdir -p "$HIDPI_SHOTS"
        cp "$WORK/1x.png" "$HIDPI_SHOTS/gworld-$1-1x.png"
        cp "$WORK/2x.png" "$HIDPI_SHOTS/gworld-$1-2x.png"
    fi
    if [[ $rc -ne 0 ]]; then
        mkdir -p "$STORAGE/tmp"
        cp "$WORK/1x.png" "$STORAGE/tmp/hidpi_gworld-$1-1x.png" 2>/dev/null || true
        cp "$WORK/2x.png" "$STORAGE/tmp/hidpi_gworld-$1-2x.png" 2>/dev/null || true
        echo "FAIL: screenshots in $STORAGE/tmp/hidpi_gworld-$1-{1x,2x}.png"
        FAILED=1
    fi
}

sleep 6
stage open
[[ $FAILED -eq 0 ]] || exit 1
echo "PASS"
