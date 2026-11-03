#!/bin/bash
#
# test_finder_904_executor.sh — Mac OS 9.0.4's own System on Executor
# (--executor-ppc, Executor's extension policy) starts to the Finder:
#
#  1. The startup runs its extensions to the end and BridgeAgent connects:
#     no session stopped (Apple Menu Options' AliasDispatch $2F), no jump
#     to the unimplemented-trap address (Mouse's CursorDeviceDispatch
#     through Mixed Mode), no host crash (AppleScript 1.4's component).
#  2. The Finder is the front process, with its menu bar.
#  3. A folder opens: "Macintosh HD:System Folder", its window on the
#     desktop.
#
# Usage:
#   tests/test_finder_904_executor.sh [--timeout N] [--port N]
#
# KEEP_DIR=DIR keeps the screenshots and log. Skips (exit 77) without the
# 9.0.4 image or PIL.
set -euo pipefail

TIMEOUT=120
PORT=18624
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
if [[ ! -f "$STORAGE/images/9.0.4.img" ]]; then echo "SKIP: $STORAGE/images/9.0.4.img not found"; exit 77; fi
if ! python3 -c 'import PIL' 2>/dev/null; then echo "SKIP: python3 PIL not installed"; exit 77; fi

WORK=$(mktemp -d ${TMPDIR:-/tmp}/macfinder904.XXXXXX)
cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    if [[ -n "${KEEP_DIR:-}" ]]; then mkdir -p "$KEEP_DIR"; cp "$WORK"/*.png "$WORK"/emu.log "$KEEP_DIR"/ 2>/dev/null || true; fi
    rm -rf "$WORK"
}
trap cleanup EXIT SIGTERM SIGINT

echo "=== The 9.0.4 Finder (Executor) ==="
"$BINARY" --backend executor --executor-system 9.0.4.img --executor-ppc \
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
    grep -aq 'session stopped\|unimplemented-trap address\|host signal\|FATAL CRASH' "$WORK/emu.log" && break
    kill -0 "$EMU_PID" 2>/dev/null || { echo " emulator died"; tail -20 "$WORK/emu.log"; exit 1; }
    sleep 1
done
FAILED=0
fail() { echo "FAIL: $1"; FAILED=1; }
if grep -a 'session stopped\|unimplemented-trap address\|host signal\|FATAL CRASH' "$WORK/emu.log"; then
    fail "the startup stopped"
    exit 1
fi
curl -sf "$URL/status" | grep -q '"bridge_agent_connected": *true' \
    || { echo " no desktop in ${TIMEOUT}s"; exit 1; }
echo " desktop"
sleep 2

api() { curl -sf --max-time 10 -X POST "$URL/$1" -H 'Content-Type: application/json' -d "$2"; }
shot() { curl -sf "$URL/screenshot" -o "$WORK/$1.png"; }

APP=$(curl -sf "$URL/app" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("app",""))')
echo "Front process: $APP"
[[ "$APP" == "Finder" ]] || fail "the Finder is not in front"

api mouse '{"x":300,"y":400}' >/dev/null
sleep 1
shot desktop

# The menu bar: titles drawn (dark pixels in the bar right of the apple)
TITLES=$(python3 - "$WORK/desktop.png" <<'EOF'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
print(sum(1 for x in range(40, 260) for y in range(4, 16) if sum(im.getpixel((x, y))) < 200))
EOF
)
echo "Menu bar: $TITLES dark pixels in the titles"
[[ "$TITLES" -gt 100 ]] || fail "no menu titles"

api launch '{"path":"Macintosh HD:System Folder"}' >/dev/null || true
sleep 4
api mouse '{"x":300,"y":460}' >/dev/null
sleep 1
shot folder
CHANGED=$(python3 - "$WORK/desktop.png" "$WORK/folder.png" <<'EOF'
import sys
from PIL import Image, ImageChops
a = Image.open(sys.argv[1]).convert('RGB')
b = Image.open(sys.argv[2]).convert('RGB')
box = ImageChops.difference(a, b).getbbox()
print(f"{box[2] - box[0]} {box[3] - box[1]}" if box else "0 0")
EOF
)
echo "System Folder window: ${CHANGED/ /x} pixels changed"
read -r W H <<<"$CHANGED"
[[ "$W" -gt 150 && "$H" -gt 100 ]] || fail "no folder window"

kill -0 "$EMU_PID" 2>/dev/null || fail "the emulator died"
if [[ "$FAILED" -ne 0 ]]; then tail -30 "$WORK/emu.log"; exit 1; fi
echo "PASS"
