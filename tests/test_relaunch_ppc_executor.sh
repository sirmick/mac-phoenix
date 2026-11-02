#!/bin/bash
#
# test_relaunch_ppc_executor.sh — a PowerPC application launched again.
#
# Executor on Mac OS 8.6 with --executor-ppc: BBEdit Lite 6.1 (dev-tools
# image) launched, its survey dismissed, quit, then launched again
# by opening a document of its own (odoc). Its main window's definition is
# the System's WDEF 124, an accelerated resource in the system heap that
# Mixed Mode prepares at the first call: the prepared fragment has to be the
# system's, not the first BBEdit's (whose heap the second launch called
# into, running off at a garbage address). Passes when the second launch
# raises no PowerPC exception and the document's window is up.
#
# Usage: tests/test_relaunch_ppc_executor.sh [--timeout N] [--port N]
#
# Skips (exit 77) without the 8.6 or dev-tools-macos-8-9 image.
set -euo pipefail

TIMEOUT=120
PORT=18542
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
STORAGE="${MACEMU_STORAGE:-$HOME/storage}"
TOOLS="$STORAGE/images/dev-tools-macos-8-9.img"
OUT="${TMPDIR:-/tmp}/relaunch-ppc-test-$$"

if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
[[ -f "$TOOLS" ]] || { echo "SKIP: no $TOOLS"; exit 77; }
DISK="$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" 8.6 $$ 2>/dev/null || true)"
if [[ -z "$DISK" || ! -f "$DISK" ]]; then echo "SKIP: no 8.6 image"; exit 77; fi

EXTFS_DIR=$(mktemp -d ${TMPDIR:-/tmp}/macrelaunch-extfs.XXXXXX)
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

# A text document of BBEdit's (TEXT/R*ch) on the Host volume, as MacBinary.
python3 - "$EXTFS_DIR/relaunch.txt.bin" <<'EOF'
import struct, sys
def crc16(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xffff if crc & 0x8000 else (crc << 1) & 0xffff
    return crc
name, data = b'relaunch.txt', b'Opened by the second launch\r'
h = bytearray(128)
h[1] = len(name); h[2:2 + len(name)] = name
h[65:73] = b'TEXTR*ch'
struct.pack_into('>I', h, 83, len(data))
h[122] = h[123] = 129
struct.pack_into('>H', h, 124, crc16(bytes(h[:124])))
open(sys.argv[1], 'wb').write(bytes(h) + data + b'\0' * (-len(data) % 128))
EOF

api() { curl -sf --max-time 60 -X POST "http://localhost:$PORT/api/$1" -H 'Content-Type: application/json' -d "$2"; }
click() {
    api mouse "{\"x\":$1,\"y\":$2}" >/dev/null; sleep 0.4
    api mouse "{\"x\":$1,\"y\":$2,\"button\":0,\"down\":true}" >/dev/null; sleep 0.15
    api mouse "{\"x\":$1,\"y\":$2,\"button\":0,\"down\":false}" >/dev/null
}
app() { curl -sf --max-time 5 "http://localhost:$PORT/api/app" | sed -n 's/.*"app": *"\([^"]*\)".*/\1/p'; }
BBEDIT='Development:Tools ƒ:BBEdit Lite 6.1.2 ƒ:BBEdit Lite 6.1'

echo "=== A PowerPC application launched again (8.6, BBEdit Lite 6.1) ==="
"$BINARY" --config /dev/null --timeout "$((TIMEOUT + 60))" --bridge --network none \
    --backend executor --executor-ppc --executor-system "$(basename "$DISK")" \
    --disk "$TOOLS" --port "$PORT" --extfs "$EXTFS_DIR" &>"$OUT/emulator.log" &
EMU_PID=$!
for _ in $(seq 1 30); do
    curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1 && break
    kill -0 "$EMU_PID" 2>/dev/null || { echo "FAIL: emulator died"; tail -20 "$OUT/emulator.log"; exit 1; }
    sleep 0.5
done
curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null
for _ in $(seq 1 "$TIMEOUT"); do
    curl -sf "http://localhost:$PORT/api/status" | grep -q '"bridge_agent_connected": *true' && break
    kill -0 "$EMU_PID" 2>/dev/null || { echo "FAIL: emulator died"; tail -20 "$OUT/emulator.log"; exit 1; }
    sleep 1
done

api launch "{\"path\":\"$BBEDIT\"}" >/dev/null || { echo "FAIL: first launch"; exit 1; }
for _ in $(seq 1 30); do [[ "$(app)" == "BBEdit Lite 6.1" ]] && break; sleep 1; done
# The survey's Cancel, then Quit; again while BBEdit is still there (a
# click before it is in front, its menus not yet up, goes to the Finder).
for _ in $(seq 1 5); do
    sleep 3
    click 418 259
    sleep 2
    api quit '{}' >/dev/null || true
    for _ in $(seq 1 6); do [[ "$(app)" != "BBEdit Lite 6.1" ]] && break; sleep 1; done
    [[ "$(app)" != "BBEdit Lite 6.1" ]] && break
done
curl -sf "http://localhost:$PORT/api/screenshot" > "$OUT/quit.png"
[[ "$(app)" != "BBEdit Lite 6.1" ]] || { echo "FAIL: BBEdit didn't quit (survey still up? $OUT/quit.png)"; exit 1; }
MARK=$(wc -l < "$OUT/emulator.log")

api launch '{"path":"Host:relaunch.txt.bin","open":true}' >/dev/null || { echo "FAIL: second launch"; exit 1; }
for _ in $(seq 1 30); do [[ "$(app)" == "BBEdit Lite 6.1" ]] && break; sleep 1; done
sleep 6
click 418 259                               # the survey again if it is up (else the text)
sleep 4
curl -sf "http://localhost:$PORT/api/screenshot" > "$OUT/second.png"

EXC=$(tail -n +"$MARK" "$OUT/emulator.log" | grep -c '\[mame-ppc\] exception' || true)
# The document window's text area (white) where the desktop is otherwise.
WHITE=$(python3 - "$OUT/second.png" <<'EOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
w, h = im.size
pts = [(x * w // 640, y * h // 480) for x in range(100, 500, 20) for y in range(200, 400, 20)]
print(sum(1 for p in pts if im.getpixel(p) == (255, 255, 255)) * 100 // len(pts))
EOF
)
echo "second launch: front '$(app)', PowerPC exceptions $EXC, document area white ${WHITE}%"
echo "Screenshot: $OUT/second.png"
[[ "$EXC" -eq 0 ]] || { echo "FAIL: the second launch ran off (exceptions in $OUT/emulator.log)"; exit 1; }
[[ "$WHITE" -ge 80 ]] || { echo "FAIL: no document window"; exit 1; }
echo "PASS: BBEdit launched again opens its document"
exit 0
