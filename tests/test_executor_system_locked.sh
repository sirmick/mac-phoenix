#!/bin/bash
#
# test_executor_system_locked.sh — the --executor-system image stays as it was.
#
# Executor runs the System Folder from a copy (<storage>/executor-systems)
# and mounts the image itself too, for its applications. That image is the
# shared original (~/storage/images/NAME), so it is mounted locked even with
# --executor-writable-images (src/core/emulator_subprocess.cpp,
# --executor-locked-disk). A session on a copy of tests-7.5.5 with writable
# images copies a file onto the image's volume (/api/copy): the image's bytes
# are the same afterwards.
#
# Usage: tests/test_executor_system_locked.sh [--timeout N] [--port N]
#
# Skips (exit 77) without the tests-7.5.5 image.
set -euo pipefail

TIMEOUT=90
PORT=18541
BINARY="${MACEMU_BINARY:-$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port)    PORT="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done
source "$(dirname "$0")/lib/ports.sh"
PORT=$(mp_port "$PORT") || exit 1

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
STORAGE="${MACEMU_STORAGE:-$HOME/storage}"
OUT="${TMPDIR:-/tmp}/system-locked-test-$$"

if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
DISK="$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" tests-7.5.5 $$ 2>/dev/null || true)"
if [[ -z "$DISK" || ! -f "$DISK" ]]; then echo "SKIP: no tests-7.5.5 image"; exit 77; fi
NAME="$(basename "$DISK")"
SYSTEMS="$STORAGE/executor-systems"

mkdir -p "$OUT"
EXTFS=$(mktemp -d ${TMPDIR:-/tmp}/macsyslocked.XXXXXX)
cleanup() {
    if [[ -n "${PID:-}" ]] && kill -0 "$PID" 2>/dev/null; then
        kill "$PID" 2>/dev/null || true
        wait "$PID" 2>/dev/null || true
    fi
    rm -rf "$EXTFS"
    [[ "$NAME" == test-tests-7.5.5-$$.img ]] && rm -rf "$DISK" "$DISK.json" "$SYSTEMS/$NAME" "$SYSTEMS/$NAME.clean" "$SYSTEMS/$NAME.lock"
}
trap cleanup EXIT SIGTERM SIGINT

BEFORE=$(md5sum < "$DISK")

echo "=== Executor on $NAME with --executor-writable-images ==="
"$BINARY" --config /dev/null --timeout "$((TIMEOUT + 30))" --headless-http --network none \
    --backend executor --executor-system "$NAME" --executor-writable-images \
    --port "$PORT" --extfs "$EXTFS" &>"$OUT/emu.log" &
PID=$!
for _ in $(seq 1 30); do
    curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1 && break
    kill -0 "$PID" 2>/dev/null || { echo "FAIL: emulator died"; tail -20 "$OUT/emu.log"; exit 1; }
    sleep 0.5
done
curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null
connected() { curl -sf --max-time 5 "http://localhost:$PORT/api/status" | grep -q '"bridge_agent_connected": *true'; }
for _ in $(seq 1 "$TIMEOUT"); do
    connected && break
    kill -0 "$PID" 2>/dev/null || { echo "FAIL: emulator died"; tail -20 "$OUT/emu.log"; exit 1; }
    sleep 1
done
connected || { echo "FAIL: BridgeAgent never connected (log $OUT/emu.log)"; exit 1; }
# A write onto the image's volume through the guest's File Manager: the
# image is mounted under its file name or its HFS name ("Macintosh HD", which
# the System Folder copy's volume may have too), so try both. Writable, either
# one changed the image's bytes.
echo probe > "$EXTFS/probe.txt"
ANSWERS=""
for vol in "$NAME" "Macintosh HD"; do
    A=$(curl -s --max-time 60 -X POST "http://localhost:$PORT/api/copy" \
        -d "{\"from\":\"Host:probe.txt\",\"to\":\"$vol:\"}" || true)
    echo "$vol: $A"
    ANSWERS="$ANSWERS $A"
done
# Locked: the copy onto it answers wPrErr (-44), as a locked disk's does.
echo "$ANSWERS" | grep -q '"error_code": -44' || {
    echo "FAIL: no copy was refused as onto a locked volume (wPrErr)"; exit 1; }
sleep 5
kill "$PID"; wait "$PID" 2>/dev/null || true
PID=

AFTER=$(md5sum < "$DISK")
if [[ "$BEFORE" != "$AFTER" ]]; then
    echo "FAIL: the --executor-system image changed (writable session)"
    exit 1
fi
grep -q -- "--executor-locked-disk $DISK" "$OUT/emu.log" || {
    echo "FAIL: the image wasn't passed as a locked disk"; exit 1; }
echo "PASS: $NAME unchanged after a session with --executor-writable-images"
exit 0
