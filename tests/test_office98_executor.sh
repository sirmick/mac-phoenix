#!/bin/bash
#
# test_office98_executor.sh — Microsoft Word 98 on Executor's 8.6 finds its
# Office 98 libraries and starts Office First Run, as on the G3 with 8.6:
#
#  1. Word's import library Microsoft_Office_98 is in the Office folder its
#     'cfrg' names (appSubdirID: its 'alis' 199): the Code Fragment Manager
#     searches it (without: "Word cannot load the Microsoft Office 98 shared
#     libraries").
#  2. Its data's relocations past a RelocLgRepeat are done (they weren't:
#     its SetCursor patch jumped through $F9E0).
#  3. The First Run installer's 'MVis' event reaches Microsoft Office First
#     Run by signature (not "Word cannot launch the First Run Installer").
#     First Run's name dialog is up: the front process is First Run.
#
# Usage:
#   tests/test_office98_executor.sh [--timeout N] [--port N]
#
# KEEP_DIR=DIR keeps the screenshot and log. Skips (exit 77) without the 8.6
# and productivity-system-7 images.
set -euo pipefail

TIMEOUT=120
PORT=18632
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
PROD="$STORAGE/images/productivity-system-7.img"

if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
if [[ ! -f "$STORAGE/images/8.6.img" ]]; then echo "SKIP: $STORAGE/images/8.6.img not found"; exit 77; fi
if [[ ! -f "$PROD" ]]; then echo "SKIP: $PROD not found"; exit 77; fi

WORK=$(mktemp -d ${TMPDIR:-/tmp}/macoffice98.XXXXXX)
cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    if [[ -n "${KEEP_DIR:-}" ]]; then mkdir -p "$KEEP_DIR"; cp "$WORK"/*.png "$WORK"/emu.log "$KEEP_DIR"/ 2>/dev/null || true; fi
    rm -rf "$WORK"
}
trap cleanup EXIT SIGTERM SIGINT

echo "=== Word 98 on 8.6 (Executor) ==="
# the disk read-only (Executor's default): nothing is written before First Run's dialog
"$BINARY" --backend executor --executor-system 8.6.img --executor-ppc --disk "$PROD" \
    --config /dev/null --screen 640x480 --timeout "$((TIMEOUT + 60))" \
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
curl -sf "$URL/status" | grep -q '"bridge_agent_connected": *true' || { echo " FAIL: no BridgeAgent"; exit 1; }
echo " up"

R=$(curl -s -X POST "$URL/launch" -d '{"path":"Productivity:Microsoft Office 98 ƒ:Microsoft Word"}')
echo "launch: $R"
FRONT=""
for i in $(seq 1 30); do
    FRONT=$(curl -s "$URL/app" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("app",""))' 2>/dev/null || true)
    [[ "$FRONT" == "Microsoft Office First Run" ]] && break
    sleep 1
done
curl -s "$URL/screenshot" -o "$WORK/word98.png" || true
if [[ "$FRONT" != "Microsoft Office First Run" ]]; then
    echo "FAIL: the front process is \"$FRONT\", not Microsoft Office First Run"
    grep -a "CFM\] file\|exception" "$WORK/emu.log" | tail -10
    exit 1
fi
if ! grep -aq "CFM\] file 'Microsoft_Office_98'" "$WORK/emu.log"; then
    echo "FAIL: Microsoft_Office_98 not loaded"
    exit 1
fi
echo "PASS: Word 98 started Microsoft Office First Run"
