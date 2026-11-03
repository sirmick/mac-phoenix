#!/bin/bash
#
# test_codewarrior_86.sh — CodeWarrior IDE 3.2 (PowerPC) on Executor on Mac
# OS 8.6's System, from a copy of the dev-tools-system-7 image.
#
# The IDE prepares 8.6's Translation (Easy Open) in its own context, after
# an earlier preparation closed. Translation's data section is global (one
# for the system): Executor freed it with the first connection and handed
# the second the freed memory unrelocated, and the IDE's Finder calls
# (FinderLib) jumped through its transition vectors into garbage
# ("[mame-ppc] exception 7 at 0c130c10", nothing drawn). Passes when the
# IDE is still the front application after the launch, with no PowerPC
# exception in the log. Screenshot and log in $TMPDIR.
#
# Usage:
#   tests/test_codewarrior_86.sh [--timeout N] [--port N]
#
# Skips (exit 77) without the 8.6 or dev-tools-system-7 image.
set -euo pipefail

TIMEOUT=120
PORT=18340
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
OUT="${TMPDIR:-/tmp}/codewarrior-86-$$"
APP="Development:CodeWarrior Pro 4 ƒ:Metrowerks CodeWarrior:CodeWarrior IDE 3.2"
DEV_IMAGE="$HOME/storage/images/dev-tools-system-7.img"

if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
[[ -f "$DEV_IMAGE" ]] || { echo "SKIP: no $DEV_IMAGE"; exit 77; }
DISK="$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" 8.6 $$ 2>/dev/null || true)"
if [[ -z "$DISK" || ! -f "$DISK" ]]; then echo "SKIP: no 8.6 image"; exit 77; fi
DEV="${TMPDIR:-/tmp}/codewarrior-86-dev-$$.img"
cp --reflink=auto "$DEV_IMAGE" "$DEV"

EXTFS_DIR=$(mktemp -d ${TMPDIR:-/tmp}/maccw-extfs.XXXXXX)
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

echo "=== CodeWarrior IDE 3.2 (8.6, executor) ==="
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
# The IDE loads its plug-ins (thirty-odd), then Translation: the crash came
# a few seconds after.
sleep 20
curl -sf "$URL/screenshot" > "$OUT/codewarrior.png" || true

FAIL=0
if ! curl -sf --max-time 5 "$URL/app" | grep -q '"CodeWarrior IDE 3.2"'; then
    echo "FAIL: CodeWarrior IDE 3.2 is not the front application ($(curl -s --max-time 5 "$URL/app"))"
    FAIL=1
fi
if grep -q '\[mame-ppc\] exception' "$OUT/emulator.log"; then
    echo "FAIL: PowerPC exceptions:"
    grep -m3 '\[mame-ppc\] exception' "$OUT/emulator.log"
    FAIL=1
fi
if [[ $FAIL -eq 0 ]]; then
    echo "PASS: CodeWarrior IDE 3.2 runs ($OUT/codewarrior.png)"
fi
exit $FAIL
