#!/bin/bash
#
# test_scroll_bars_86.sh — scroll bars on Mac OS 8.6 (Executor on 8.6's
# System, or the G3 as the reference).
#
#  1. The Finder's live scrolling: a window's thumb dragged with the button
#     held scrolls its contents before the button is let go (the Finder
#     gives TrackControl an action procedure taking the control and the
#     part, which the Control Manager calls all along for a live-feedback
#     scroll bar; called with nothing, it read a garbage ControlHandle).
#  2. Executor only: Standard File's list (AppleWorks' Open, on a copy of
#     the productivity-macos-9 image) on Executor's own 8.x scroll bar: a
#     click in the track below the thumb pages down, one above it pages
#     back up (the hit test went by the classic thumb, never drawn on 8.x:
#     every click was "page down").
#
# Positions are fixed: the System Folder's window as the image's Finder
# keeps it, the same on both machines (/api/ui doesn't answer on
# Executor); what changed is compared on screenshots. Screenshots in $TMPDIR.
#
# Usage:
#   tests/test_scroll_bars_86.sh [--backend executor|kpx] [--core C] [--jit]
#                                [--timeout N] [--port N]
#
# Skips (exit 77) without the 8.6 image (or, on the G3, its ROM).
set -euo pipefail

TIMEOUT=120
PORT=18210
BACKEND=executor
EXTRA=()
BINARY="${MACEMU_BINARY:-$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --backend) BACKEND="$2"; shift 2 ;;
        --core)    EXTRA+=(--core "$2"); shift 2 ;;
        --jit)     EXTRA+=(--jit); shift ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port)    PORT="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done
source "$(dirname "$0")/lib/ports.sh"; PORT=$(mp_port "$PORT") || exit 1  # per-worktree (docs/Testing.md)

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="${TMPDIR:-/tmp}/scrollbars-test-$BACKEND-$$"

if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
DISK="$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" 8.6 $$ 2>/dev/null || true)"
if [[ -z "$DISK" || ! -f "$DISK" ]]; then echo "SKIP: no 8.6 image"; exit 77; fi
PROD=""
if [[ "$BACKEND" == executor ]]; then
    EXTRA+=(--backend executor --executor-ppc --executor-system "$(basename "$DISK")")
    if [[ -f "$HOME/storage/images/productivity-macos-9.img" ]]; then
        PROD="${TMPDIR:-/tmp}/scrollbars-productivity-$$.img"
        cp --reflink=auto "$HOME/storage/images/productivity-macos-9.img" "$PROD"
        EXTRA+=(--disk "$PROD" --executor-writable-images)
    fi
else
    ROM="${MACEMU_ROM:-$HOME/storage/roms/g3.rom}"
    [[ -f "$ROM" ]] || { echo "SKIP: no G3 ROM: $ROM"; exit 77; }
    EXTRA+=(--backend "$BACKEND" --ram 128 --disk "$DISK" --dismiss-shutdown-dialog "$ROM")
fi

EXTFS_DIR=$(mktemp -d ${TMPDIR:-/tmp}/macscroll-extfs.XXXXXX)
mkdir -p "$OUT"
cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    rm -rf "$EXTFS_DIR"
    # (ifs: a false test as the trap's last command is the script's status)
    if [[ "$(basename "$DISK")" == test-8.6-$$.img ]]; then rm -f "$DISK"; fi
    if [[ -n "$PROD" ]]; then rm -f "$PROD"; fi
}
trap cleanup EXIT SIGTERM SIGINT

URL="http://localhost:$PORT/api"
api() { curl -sf --max-time 10 -X POST "$URL/$1" -H 'Content-Type: application/json' -d "$2"; }
mouse() { api mouse "$1" >/dev/null; }
shot() { curl -sf "$URL/screenshot" > "$OUT/$1.png" || true; }
alive() { kill -0 "$EMU_PID" 2>/dev/null && curl -sf --max-time 5 "$URL/app" | grep -q "\"$1\""; }

# A click: the button held a moment (Executor polls the mouse).
click() {
    mouse "{\"x\":$1,\"y\":$2}"; sleep 0.4
    mouse "{\"x\":$1,\"y\":$2,\"button\":0,\"down\":true}"; sleep 0.2
    mouse "{\"x\":$1,\"y\":$2,\"button\":0,\"down\":false}"; sleep 1.5
}

# How many pixels differ between two screenshots in the rectangle l t r b.
differ() {
    python3 - "$@" <<'EOF'
import struct, sys, zlib
def load(path):
    data = open(path, 'rb').read()
    pos, idat = 8, b''
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
            bpp = {2: 3, 6: 4}[ctype]
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    raw, stride, rows, prev = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp)
    for r in range(h):
        f, line = raw[r * (stride + 1)], bytearray(raw[r * (stride + 1) + 1:(r + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return rows, bpp
(a, bpp), (b, _) = load(sys.argv[1]), load(sys.argv[2])
l, t, r, btm = map(int, sys.argv[3:7])
print(sum(1 for y in range(t, btm) for x in range(l, r)
          if a[y][x * bpp:x * bpp + 3] != b[y][x * bpp:x * bpp + 3]))
EOF
}

echo "=== Scroll bars (8.6, $BACKEND) ==="
"$BINARY" --config /dev/null --timeout "$((TIMEOUT + 60))" --bridge --network none \
    --port "$PORT" --extfs "$EXTFS_DIR" "${EXTRA[@]}" &>"$OUT/emulator.log" &
EMU_PID=$!

for i in $(seq 1 30); do
    curl -sf "$URL/status" >/dev/null 2>&1 && break
    kill -0 "$EMU_PID" 2>/dev/null || { echo "FAIL: emulator died"; tail -20 "$OUT/emulator.log"; exit 1; }
    sleep 0.5
done
curl -sf -X POST "$URL/emulator/start" >/dev/null
echo -n "Booting..."
for i in $(seq 1 "$TIMEOUT"); do
    curl -sf "$URL/status" | grep -q '"bridge_agent_connected": *true' && break
    kill -0 "$EMU_PID" 2>/dev/null || { echo " emulator died"; tail -20 "$OUT/emulator.log"; exit 1; }
    sleep 1
done
curl -sf "$URL/status" | grep -q '"bridge_agent_connected": *true' \
    || { echo " no desktop in ${TIMEOUT}s"; exit 1; }
echo " desktop"
sleep 2

FAILED=0
fail() { echo "FAIL: $1"; FAILED=1; }

# ---- 1. the Finder's live scrolling: the System Folder's window, its
# scroll bar at x 403-418, the thumb at the top of the track (y 83 on)
api launch '{"path":"Macintosh HD:System Folder"}' >/dev/null || true
sleep 4
X=410; Y=90
shot finder-before
mouse "{\"x\":$X,\"y\":$Y}"; sleep 0.3
mouse "{\"x\":$X,\"y\":$Y,\"button\":0,\"down\":true}"; sleep 0.4
for dy in 10 20 30 40; do mouse "{\"x\":$X,\"y\":$((Y + dy))}"; sleep 0.25; done
sleep 1
shot finder-dragging
mouse "{\"x\":$X,\"y\":$((Y + 40)),\"button\":0,\"down\":false}"; sleep 1
shot finder-after
N=$(differ "$OUT/finder-before.png" "$OUT/finder-dragging.png" 14 66 400 236 2>/dev/null || echo 0)
echo "System Folder window: $N pixels changed with the thumb still held"
[[ "$N" -gt 500 ]] || fail "the window did not scroll while its thumb was dragged"
alive Finder || fail "the Finder is gone after the drag"

# ---- 2. Standard File's list on Executor: AppleWorks' Open
if [[ -n "$PROD" ]]; then
    api launch '{"path":"Productivity:AppleWorks 5.0.4 \u0192:AppleWorks"}' >/dev/null || true
    for i in $(seq 1 30); do alive AppleWorks && break; sleep 0.5; done
    sleep 4
    click 416 329                                  # New Document: Cancel
    mouse '{"x":53,"y":10}'; sleep 0.3             # File > Open...
    mouse '{"x":53,"y":10,"button":0,"down":true}'; sleep 1
    mouse '{"x":75,"y":44}'; sleep 0.5
    mouse '{"x":75,"y":44,"button":0,"down":false}'; sleep 3
    click 430 209                                  # Desktop: Macintosh HD, 12 names
    shot sf-top
    click 363 272                                  # the track below the thumb
    shot sf-down
    click 363 205                                  # above it
    shot sf-up
    LIST="154 170 355 297"
    DOWN=$(differ "$OUT/sf-top.png" "$OUT/sf-down.png" $LIST 2>/dev/null || echo 0)
    BACK=$(differ "$OUT/sf-top.png" "$OUT/sf-up.png" $LIST 2>/dev/null || echo 99999)
    echo "Open dialog's list: $DOWN pixels changed paging down, $BACK left after paging up"
    [[ "$DOWN" -gt 500 ]] || fail "a click below the thumb did not page down"
    [[ "$BACK" -lt 50 ]] || fail "a click above the thumb did not page back up"
fi

echo "Screenshots: $OUT"
[[ "$FAILED" == 0 ]] || exit 1
echo "PASS: live scrolling in the Finder${PROD:+, paging in Standard File}"
exit 0
