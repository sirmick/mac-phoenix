#!/bin/bash
#
# test_apple_menu_submenus.sh — Apple Menu Options' submenus on Mac OS 8.6.
#
# Boots 8.6 (Executor on 8.6's System, or the G3 as the reference), holds
# the Apple menu down with the mouse API and rests on "Control Panels"
# (a folder alias, the 8th item: row y 158 on both machines). Apple Menu
# Options (an 'APPC' control panel whose INIT the 8.x loader runs) makes
# it a submenu: the test passes when a menu is drawn right of the Apple
# menu, where the desktop shows otherwise. Screenshots in $TMPDIR.
#
# Usage:
#   tests/test_apple_menu_submenus.sh [--backend executor|kpx] [--core C] [--jit]
#                                     [--timeout N] [--port N]
#
# Skips (exit 77) without the 8.6 image (or, on the G3, its ROM).
set -euo pipefail

TIMEOUT=120
PORT=18170
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
OUT="${TMPDIR:-/tmp}/applemenu-test-$BACKEND-$$"

if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
DISK="$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" 8.6 $$ 2>/dev/null || true)"
if [[ -z "$DISK" || ! -f "$DISK" ]]; then echo "SKIP: no 8.6 image"; exit 77; fi
if [[ "$BACKEND" == executor ]]; then
    EXTRA+=(--backend executor --executor-ppc --executor-system "$(basename "$DISK")")
else
    ROM="${MACEMU_ROM:-$HOME/storage/roms/g3.rom}"
    [[ -f "$ROM" ]] || { echo "SKIP: no G3 ROM: $ROM"; exit 77; }
    EXTRA+=(--backend "$BACKEND" --ram 128 --disk "$DISK" --dismiss-shutdown-dialog "$ROM")
fi

EXTFS_DIR=$(mktemp -d ${TMPDIR:-/tmp}/macapplemenu-extfs.XXXXXX)
mkdir -p "$OUT"
cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    rm -rf "$EXTFS_DIR"
    [[ "$(basename "$DISK")" == test-8.6-$$.img ]] && rm -f "$DISK"
}
trap cleanup EXIT SIGTERM SIGINT

api() { curl -sf --max-time 5 -X POST "http://localhost:$PORT/api/$1" -H 'Content-Type: application/json' -d "$2"; }
mouse() { api mouse "$1" >/dev/null; }

# The share of the points (x, y0..y1 step 4) that are menu grey (r = g = b,
# light), in percent, of a PNG screenshot.
grey_share() {
    python3 - "$1" "$2" "$3" "$4" <<'EOF'
import struct, sys, zlib
path, x, y0, y1 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
data = open(path, 'rb').read()
pos, idat, w = 8, b'', 0
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
for r in range(len(raw) // (stride + 1)):
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
ys = range(y0, y1, 4)
grey = sum(1 for y in ys if (lambda p: p[0] == p[1] == p[2] and p[0] >= 0xC0)(rows[y][x * bpp:x * bpp + 3]))
print(grey * 100 // len(ys))
EOF
}

echo "=== Apple Menu Options submenus (8.6, $BACKEND) ==="
"$BINARY" --config /dev/null --timeout "$((TIMEOUT + 10))" --bridge --network none \
    --port "$PORT" --extfs "$EXTFS_DIR" "${EXTRA[@]}" &>"$OUT/emulator.log" &
EMU_PID=$!

for i in $(seq 1 30); do
    curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1 && break
    kill -0 "$EMU_PID" 2>/dev/null || { echo "FAIL: emulator died"; tail -20 "$OUT/emulator.log"; exit 1; }
    sleep 0.5
done
curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null
echo -n "Booting..."
for i in $(seq 1 "$TIMEOUT"); do
    curl -sf "http://localhost:$PORT/api/status" | grep -q '"bridge_agent_connected": *true' && break
    kill -0 "$EMU_PID" 2>/dev/null || { echo " emulator died"; tail -20 "$OUT/emulator.log"; exit 1; }
    sleep 1
done
curl -sf "http://localhost:$PORT/api/status" | grep -q '"bridge_agent_connected": *true' \
    || { echo " no desktop in ${TIMEOUT}s"; exit 1; }
echo " desktop"
sleep 3

mouse '{"x":20,"y":10}'; sleep 0.3
mouse '{"x":20,"y":10,"button":0,"down":true}'; sleep 1.5
curl -sf "http://localhost:$PORT/api/screenshot" > "$OUT/menu.png"
mouse '{"x":80,"y":158}'; sleep 2.5
curl -sf "http://localhost:$PORT/api/screenshot" > "$OUT/submenu.png"
mouse '{"x":20,"y":10}'; sleep 0.3
mouse '{"x":20,"y":10,"button":0,"down":false}'

MENU=$(grey_share "$OUT/menu.png" 120 40 380)
BEFORE=$(grey_share "$OUT/menu.png" 300 60 400)
AFTER=$(grey_share "$OUT/submenu.png" 300 60 400)
echo "Apple menu grey ${MENU}%; right of it: ${BEFORE}% before, ${AFTER}% on Control Panels"
echo "Screenshots: $OUT"
[[ "$MENU" -ge 50 ]] || { echo "FAIL: the Apple menu did not open"; exit 1; }
[[ "$BEFORE" -lt 20 ]] || { echo "FAIL: something grey right of the Apple menu before"; exit 1; }
[[ "$AFTER" -ge 50 ]] || { echo "FAIL: no submenu for Control Panels"; exit 1; }
echo "PASS: Control Panels has a submenu"
exit 0
