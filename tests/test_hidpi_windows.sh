#!/bin/bash
#
# test_hidpi_windows.sh — Executor's HiDPI text runs and the windows in
# front of them (docs/executor/HiDPI.md "Runs and staleness").
#
# Boots Executor on Apple's 8.6 System at --executor-scale 2, types lines
# into a SimpleText document and closes it: the "Save changes" alert
# comes up over the lines. Inside the alert, where the lines' 2x text was
# and the alert's white is what the text's white was, the 2x screen must
# be the 1x frame scaled up (no slivers of the outline glyphs: a window in
# front clips the runs behind it). Then the alert's Cancel is pressed:
# where it was, over the document's white, the 2x screen must be the 1x
# frame again (no grey box with a white "Cancel": a closed window's runs
# go). The boxes are SimpleText's and the alert's on a 640x480 screen.
#
# Usage:
#   tests/test_hidpi_windows.sh [--timeout N] [--port N]
#
# Skips (exit 77) without the 8.6 image, PIL, or a build with Skia
# (-DEXECUTOR_SKIA=ON: the screenshot stays 1x).
set -euo pipefail

TIMEOUT=120
PORT=18620
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

WORK=$(mktemp -d ${TMPDIR:-/tmp}/machidpiwin.XXXXXX)
cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT SIGTERM SIGINT

echo "=== HiDPI runs behind windows (Executor 8.6, scale 2) ==="
"$BINARY" --backend executor --executor-system 8.6.img --executor-ppc --executor-scale 2 \
    --config /dev/null --bridge --timeout "$((TIMEOUT + 10))" \
    --port "$PORT" &>"$WORK/emu.log" &
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
    -H 'Content-Type: application/json' -d '{"path":"Macintosh HD:Applications:SimpleText"}' || echo '{"success":false}')
echo "$LAUNCH" | grep -q '"success": true' || { echo "FAIL: could not launch SimpleText: $LAUNCH"; exit 1; }
sleep 3

# Ten lines typed (Mac key codes): they end left of the alert's buttons
python3 - "$PORT" <<'PY'
import json, sys, time, urllib.request
codes = dict(a=0, s=1, d=2, f=3, h=4, g=5, z=6, x=7, c=8, v=9, b=11, q=12, w=13, e=14, r=15, y=16,
             t=17, o=31, u=32, i=34, p=35, l=37, j=38, k=40, n=45, m=46)
codes[' '], codes['\n'] = 49, 36
for ch in 'the quick brown fox jumps over\n' * 10:
    req = urllib.request.Request(f'http://localhost:{sys.argv[1]}/api/keypress', method='POST',
                                 data=json.dumps({'key': codes[ch]}).encode())
    urllib.request.urlopen(req).read()
    time.sleep(0.04)
PY

mouse() { curl -sf -X POST "http://localhost:$PORT/api/mouse" -d "$1" >/dev/null; }
# A click at X Y (1x screen coordinates)
click() {
    mouse "{\"x\":$1,\"y\":$2}"; sleep 0.5
    mouse '{"button":0,"down":true}'; sleep 0.3
    mouse '{"button":0,"down":false}'
}

# Each box (1x screen coordinates, x0 y0 x1 y1) must be plain: the 2x
# screen there is the 1x frame scaled up. STAGE: alert or closed.
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
# the alert: 138,94-502,243, its icon at 163,130; the lines end at x 200
if sys.argv[3] == 'alert':
    boxes = [('inside the alert, left of its icon, over the lines', 142, 110, 158, 230),
             ('inside the alert, under its message, over the lines', 200, 166, 340, 204)]
else:
    boxes = [('where the alert\'s Cancel was (pressed, the alert gone)', 356, 210, 412, 226)]
pa, pb = up.load(), b.load()
bad = 0
for name, x0, y0, x1, y1 in boxes:
    n = 0
    for y in range(2 * y0, 2 * y1):
        for x in range(2 * x0, 2 * x1):
            p, q = pa[x, y], pb[x, y]
            n += max(abs(p[0] - q[0]), abs(p[1] - q[1]), abs(p[2] - q[2])) > 8
    bad += n > 0
    print(f'  {"ok  " if n == 0 else "FAIL"} {name}: {n} pixels differ from the 1x frame scaled up (none expected)')
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
        cp "$WORK/1x.png" "$STORAGE/tmp/hidpi_windows-$1-1x.png" 2>/dev/null || true
        cp "$WORK/2x.png" "$STORAGE/tmp/hidpi_windows-$1-2x.png" 2>/dev/null || true
        echo "FAIL: screenshots in $STORAGE/tmp/hidpi_windows-$1-{1x,2x}.png"
        FAILED=1
    fi
}

# File > Close: "Save changes...?"
mouse '{"x":54,"y":10}'; sleep 0.3
mouse '{"button":0,"down":true}'; sleep 0.8
mouse '{"x":70,"y":40}'; sleep 0.2
mouse '{"x":75,"y":65}'; sleep 0.3
mouse '{"button":0,"down":false}'
curl -sf --max-time 20 -X POST "http://localhost:$PORT/api/wait" -d '{"condition":"text=before closing","timeout":15}' >/dev/null \
    || echo "  (the alert's text not seen)"
sleep 2
stage alert
click 384 218
sleep 3
stage closed
[[ $FAILED -eq 0 ]] || exit 1
echo "PASS"
