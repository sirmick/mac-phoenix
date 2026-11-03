#!/bin/bash
#
# test_utilities_executor.sh — the Utilities disk (provisioning/images/
# utilities.json: system information and benchmarks) on Executor: each
# application below launched from it in a session of its own and in front
# 20 s later. 7.6.1 (68k): Gestalt.Appl. 8.6 (--executor-ppc): Gestalt.Appl,
# MacBench 5.0 (its PowerPC code: up to its licence).
# Screenshots and logs in $TMPDIR. Left out: TattleTech (it exits at once:
# UseResFile on a local only its Name Registry path sets, which Executor
# lacks; stack junk on a Mac, zero on Executor: docs/Provisioning.md),
# Speedometer 4.0 (its splash waits for a click, on the Quadra too),
# Clockometer (hangs
# the Quadra's ROM too: it times the CPU against hardware emulators don't
# have), Cache-22 ("Unknown processor type" everywhere).
#
# Usage:
#   tests/test_utilities_executor.sh SYSTEM [--port N]     SYSTEM: 7.6.1 or 8.6
#
# Skips (exit 77) without the System's image or utilities.img in ~/storage/images.
set -euo pipefail

SYSTEM="$1"; shift
PORT=18790
BINARY="${MACEMU_BINARY:-$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix}"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) PORT="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done
source "$(dirname "$0")/lib/ports.sh"; PORT=$(mp_port "$PORT") || exit 1  # per-worktree (docs/Testing.md)

IMAGES="$HOME/storage/images"
UTIL="$IMAGES/utilities.img"
if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
[[ -f "$UTIL" ]] || { echo "SKIP: no $UTIL (provisioning/build.py utilities)"; exit 77; }
[[ -f "$IMAGES/$SYSTEM.img" ]] || { echo "SKIP: no $IMAGES/$SYSTEM.img"; exit 77; }

case "$SYSTEM" in
    7.6.1) EXTRA=(); APPS=("Gestalt.Appl 2.7.9 ƒ:Gestalt.appl 2.7.9|Gestalt.appl 2.7.9") ;;
    8.6)   EXTRA=(--executor-ppc)
           APPS=("Gestalt.Appl 2.7.9 ƒ:Gestalt.appl 2.7.9|Gestalt.appl 2.7.9"
                 "MacBench 5.0 ƒ:MacBench® 5.0|MacBench") ;;
    *) echo "Unknown system: $SYSTEM"; exit 1 ;;
esac

OUT="${TMPDIR:-/tmp}/utilities-$SYSTEM-$$"
mkdir -p "$OUT"
URL="http://localhost:$PORT/api"
EMU_PID=""
stop() {
    if [[ -n "$EMU_PID" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    EMU_PID=""
}
trap stop EXIT SIGTERM SIGINT

FAILED=0
for entry in "${APPS[@]}"; do
    path="${entry%%|*}"; want="${entry##*|}"; tag="${want// /_}"
    echo "=== $SYSTEM: $want ==="
    "$BINARY" --config /dev/null --timeout 200 --bridge --network none --port "$PORT" \
        --backend executor --screen 640x480 --executor-system "$SYSTEM.img" "${EXTRA[@]}" \
        --disk "$UTIL" &>"$OUT/$tag.log" &
    EMU_PID=$!
    for i in $(seq 1 30); do curl -sf "$URL/status" >/dev/null 2>&1 && break; sleep 1; done
    curl -sf -X POST "$URL/emulator/start" >/dev/null || true
    for i in $(seq 1 90); do
        curl -sf "$URL/status" 2>/dev/null | grep -q '"bridge_agent_connected": true' && break
        kill -0 "$EMU_PID" 2>/dev/null || break
        sleep 2
    done
    curl -sf -X POST "$URL/launch" -d "{\"path\":\"Utilities:$path\"}" >/dev/null || true
    sleep 20
    curl -sf --max-time 10 "$URL/screenshot" -o "$OUT/$tag.png" || true
    app=$(curl -sf --max-time 5 "$URL/app" 2>/dev/null || true)
    if [[ "$app" == *"$want"* ]] && ! grep -q "fatal error\|unhandled trap" "$OUT/$tag.log"; then
        echo "PASS: $want in front"
    else
        echo "FAIL: $want not in front ($app); log $OUT/$tag.log"
        FAILED=1
    fi
    stop
    sleep 2
done
exit $FAILED
