#!/bin/bash
#
# test_mpw_86.sh — MPW Shell (PowerPC) on Executor on Mac OS 8.6's System,
# from a copy of the dev-tools-system-7 image.
#
# MPW's Startup script builds its Project, Directory and Build menus with
# scripts it finds through {Commands} (MakeResolvedPath, StdCLib), sends
# output through StdCLib's devices (dev:stdout, redirection). Passes when
# MPW Shell is the front application with those menus, a do-script's
# "Echo ... > Host:file" writes the file, and the log has no PowerPC
# exception. Screenshot and log in $TMPDIR.
#
# Also: an AddMenu Help item, chosen from 8.x's Help menu (a text title after
# the application's menus), runs.
#
# Usage:
#   tests/test_mpw_86.sh [--timeout N] [--port N]
#
# Skips (exit 77) without the 8.6 or dev-tools-system-7 image.
set -euo pipefail

TIMEOUT=120
PORT=18344
BINARY="${MACEMU_BINARY:-$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port)    PORT="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done
source "$(dirname "$0")/lib/ports.sh"; PORT=$(mp_port "$PORT") || exit 1  # a free port (docs/Testing.md "Ports")

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="${TMPDIR:-/tmp}/mpw-86-$$"
APP="Development:MPW:MPW Shell"
DEV_IMAGE="$HOME/storage/images/dev-tools-system-7.img"

if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
[[ -f "$DEV_IMAGE" ]] || { echo "SKIP: no $DEV_IMAGE"; exit 77; }
DISK="$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" 8.6 $$ 2>/dev/null || true)"
if [[ -z "$DISK" || ! -f "$DISK" ]]; then echo "SKIP: no 8.6 image"; exit 77; fi
DEV="${TMPDIR:-/tmp}/mpw-86-dev-$$.img"
cp --reflink=auto "$DEV_IMAGE" "$DEV"

EXTFS_DIR=$(mktemp -d ${TMPDIR:-/tmp}/macmpw-extfs.XXXXXX)
mkdir -p "$OUT"
cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    rm -rf "$EXTFS_DIR"
    [[ "$(basename "$DISK")" == test-8.6-$$.img ]] && rm -f "$DISK"
    rm -f "$DEV"
}
trap cleanup EXIT SIGTERM SIGINT

URL="http://localhost:$PORT/api"

echo "=== MPW Shell (8.6, executor) ==="
"$BINARY" --config /dev/null --timeout "$((TIMEOUT + 60))" --bridge --network none \
    --port "$PORT" --extfs "$EXTFS_DIR" --backend executor --executor-ppc \
    --executor-system "$(basename "$DISK")" --disk "$DEV" --executor-writable-images \
    &>"$OUT/emulator.log" &
EMU_PID=$!

for i in $(seq 1 30); do
    curl -sf "$URL/status" >/dev/null 2>&1 && break
    kill -0 "$EMU_PID" 2>/dev/null || { echo "FAIL: emulator died"; tail -20 "$OUT/emulator.log"; exit 1; }
    sleep 1
done
curl -sf -X POST "$URL/emulator/start" >/dev/null
curl -sf -X POST "$URL/wait" -H 'Content-Type: application/json' \
    -d '{"condition":"boot=Finder","timeout":60}' >/dev/null || true
sleep 3

curl -sf --max-time 30 -X POST "$URL/launch" -H 'Content-Type: application/json' \
    -d "{\"path\":\"$APP\"}" >/dev/null || { echo "FAIL: launch refused"; exit 1; }
# Startup runs for a few seconds (ProjectMenu, DirectoryMenu, BuildMenu).
curl -sf -X POST "$URL/wait" -H 'Content-Type: application/json' \
    -d '{"condition":"app=MPW Shell","timeout":30}' >/dev/null || true
sleep 10

FAIL=0
APPNOW=$(curl -sf --max-time 5 "$URL/app" || true)
if ! grep -q '"MPW Shell"' <<< "$APPNOW"; then
    echo "FAIL: MPW Shell is not the front application ($APPNOW)"
    FAIL=1
fi
# Do-scripts (misc/dosc): redirection through StdCLib, and AddMenu's list of
# the menus the Startup scripts added (/api/ui doesn't answer on Executor).
mpw_script() {
    python3 -c "import json,sys; print(json.dumps({'creator':'MPS ','script':sys.argv[1]}))" \
        "$1" > "$OUT/script.json"
    curl -sf --max-time 60 -X POST "$URL/script" -H 'Content-Type: application/json' \
        -d @"$OUT/script.json" >> "$OUT/script.out" || true
}
mpw_script 'Echo "mpw on executor" > Host:mpw-echo.txt'
mpw_script 'AddMenu > Host:mpw-menus.txt'
# 8.x's Help menu: a text title right after Build (x 434..475; a G3 lays it
# out after the application's menus too), where AddMenu Help items go:
# after MPW Help..., the menu's fourth row.
mpw_script 'AddMenu Help "Say Hello" "Echo hello > Host:mpw-help.txt"'
sleep 2
curl -sf "$URL/screenshot" > "$OUT/mpw.png" || true
mouse() { curl -sf -X POST "$URL/mouse" -d "$1" >/dev/null; sleep 0.4; }
mouse '{"x":455,"y":10}'
mouse '{"button":0,"down":true}'
curl -sf "$URL/screenshot" > "$OUT/mpw-help.png" || true
mouse '{"x":470,"y":85}'
mouse '{"button":0,"down":false}'
sleep 3
if ! tr '\r' '\n' < "$EXTFS_DIR/mpw-help.txt" 2>/dev/null | grep -q '^hello$'; then
    echo "FAIL: Help > Say Hello (AddMenu Help) did not run ($OUT/mpw-help.png)"
    FAIL=1
fi
if ! tr '\r' '\n' < "$EXTFS_DIR/mpw-echo.txt" 2>/dev/null | grep -q '^mpw on executor$'; then
    echo "FAIL: Echo > Host:mpw-echo.txt wrote nothing ($(cat "$OUT/script.out" 2>/dev/null))"
    FAIL=1
fi
tr '\r' '\n' < "$EXTFS_DIR/mpw-menus.txt" > "$OUT/menus.txt" 2>/dev/null || true
for m in Project Directory Build; do
    if ! grep -qs "^AddMenu $m " "$OUT/menus.txt"; then
        echo "FAIL: no $m menu ($OUT/menus.txt)"
        FAIL=1
    fi
done
if grep -q '\[mame-ppc\] exception' "$OUT/emulator.log"; then
    echo "FAIL: PowerPC exceptions:"
    grep -m3 '\[mame-ppc\] exception' "$OUT/emulator.log"
    FAIL=1
fi
if [[ $FAIL -eq 0 ]]; then
    echo "PASS: MPW Shell runs, with its menus ($OUT/mpw.png)"
fi
exit $FAIL
