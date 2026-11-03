#!/bin/bash
#
# test_thinkc_run_executor.sh — THINK C 6 runs a project on Executor (7.6.1's
# System, a copy of the dev-tools-system-7 image): the Object Bullseye demo
# opened, converted, brought up to date and run (Project > Run, Cmd-R).
#
# THINK's Run makes the project file an application ('????' creator) and
# launches it with MultiFinder's short extended block: launchBlockID 'LC',
# launchEPBLength 6 (no launchAppSpec; the name is the old block's, in the
# default directory), launchContinue. Executor read the absent
# launchAppSpec (garbage), launched nothing it could run ("launch failure:
# 3") and died ("unhandled trap 11"). The project's runtime stub then finds
# THINK's data through ApplScratch, which a new process starts with from
# its launcher; Executor's started at $FF ("unhandled trap 3 at pc
# 0xFFFFFFFF"). Passes when the demo is the front application, a Cmd-N
# later still, with no fatal error in the log. Screenshot and log in
# $TMPDIR.
#
# Usage:
#   tests/test_thinkc_run_executor.sh [--timeout N] [--port N]
#
# Skips (exit 77) without the tests-7.6.1 or dev-tools-system-7 image.
set -euo pipefail

TIMEOUT=150
PORT=18780
BINARY="${MACEMU_BINARY:-$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port)    PORT="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done
source "$(dirname "$0")/lib/ports.sh"; PORT=$(mp_port "$PORT") || exit 1  # per-worktree (docs/Testing.md)

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="${TMPDIR:-/tmp}/thinkc-run-$$"
PROJECT="Development:THINK C 6.0 ƒ:Demos:OOP Demos:Object Bullseye:Object Bullseye.π"
DEV_IMAGE="$HOME/storage/images/dev-tools-system-7.img"

if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
[[ -f "$DEV_IMAGE" ]] || { echo "SKIP: no $DEV_IMAGE"; exit 77; }
DISK="$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" tests-7.6.1 $$ 2>/dev/null || true)"
if [[ -z "$DISK" || ! -f "$DISK" ]]; then echo "SKIP: no tests-7.6.1 image"; exit 77; fi
DEV="${TMPDIR:-/tmp}/thinkc-run-dev-$$.img"
cp --reflink=auto "$DEV_IMAGE" "$DEV"

EXTFS_DIR=$(mktemp -d ${TMPDIR:-/tmp}/macthinkc-extfs.XXXXXX)
mkdir -p "$OUT"
cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    rm -rf "$EXTFS_DIR"
    [[ "$(basename "$DISK")" == test-tests-7.6.1-$$.img ]] && rm -f "$DISK"
    rm -f "$DEV"
}
trap cleanup EXIT SIGTERM SIGINT

URL="http://localhost:$PORT/api"
click() {
    curl -sf -X POST "$URL/mouse" -d "{\"x\":$1,\"y\":$2}" >/dev/null; sleep 0.3
    curl -sf -X POST "$URL/mouse" -d "{\"x\":$1,\"y\":$2,\"button\":0,\"down\":true}" >/dev/null; sleep 0.25
    curl -sf -X POST "$URL/mouse" -d "{\"x\":$1,\"y\":$2,\"button\":0,\"down\":false}" >/dev/null
}
cmdkey() {
    curl -sf -X POST "$URL/keypress" -d '{"key":55,"down":true}' >/dev/null; sleep 0.2
    curl -sf -X POST "$URL/keypress" -d "{\"key\":$1,\"down\":true}" >/dev/null; sleep 0.2
    curl -sf -X POST "$URL/keypress" -d "{\"key\":$1,\"down\":false}" >/dev/null; sleep 0.2
    curl -sf -X POST "$URL/keypress" -d '{"key":55,"down":false}' >/dev/null
}
front() { curl -sf --max-time 5 "$URL/app" 2>/dev/null || true; }

echo "=== THINK C 6: Object Bullseye run (7.6.1, executor) ==="
"$BINARY" --config /dev/null --timeout "$((TIMEOUT + 60))" --bridge --network none \
    --port "$PORT" --extfs "$EXTFS_DIR" --backend executor --screen 640x480 \
    --executor-system "$(basename "$DISK")" --disk "$DEV" --executor-writable-images \
    &>"$OUT/emulator.log" &
EMU_PID=$!

for i in $(seq 1 30); do
    curl -sf "$URL/status" >/dev/null 2>&1 && break
    kill -0 "$EMU_PID" 2>/dev/null || { echo "FAIL: emulator died"; tail -20 "$OUT/emulator.log"; exit 1; }
    sleep 1
done
curl -sf -X POST "$URL/emulator/start" >/dev/null
for i in $(seq 1 60); do
    curl -sf "$URL/status" | grep -q '"bridge_agent_connected": true' && break
    sleep 1
done

curl -sf --max-time 30 -X POST "$URL/launch" -H 'Content-Type: application/json' \
    -d "{\"path\":\"$PROJECT\",\"open\":true}" >/dev/null || { echo "FAIL: launch refused"; exit 1; }
# "This project was created by an earlier version ... Convert it?": Convert
# (its default is Cancel), at its place in the alert on a 640x480 screen.
sleep 8
click 459 187
sleep 6
cmdkey 15                     # Project > Run
sleep 4
click 468 156                 # "Bring the project up to date?": Update
for i in $(seq 1 60); do
    case "$(front)" in *'Object Bullseye'*) break ;; esac
    kill -0 "$EMU_PID" 2>/dev/null || break
    sleep 1
done
sleep 2
cmdkey 45                     # File > New: a Bullseye window
sleep 3
curl -sf "$URL/screenshot" > "$OUT/thinkc.png" || true

FAIL=0
APPNOW="$(front)"
if ! grep -q 'Object Bullseye' <<<"$APPNOW"; then
    echo "FAIL: the demo is not the front application ($APPNOW)"
    FAIL=1
fi
if grep -aq 'fatal\|launch failure' "$OUT/emulator.log"; then
    echo "FAIL: in the log:"
    grep -a -m3 'fatal\|launch failure' "$OUT/emulator.log"
    FAIL=1
fi
if [[ $FAIL -eq 0 ]]; then
    echo "PASS: THINK C ran Object Bullseye ($OUT/thinkc.png)"
fi
exit $FAIL
