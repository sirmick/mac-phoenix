#!/bin/bash
#
# test_executor_system_sessions.sh — two Executor sessions on one System.
#
# Starts two emulators at once on the same --executor-system (a copy of
# tests-7.5.5) and waits for both BridgeAgents. The first runs on
# <storage>/executor-systems/NAME and holds NAME.lock; the second gets a
# copy of its own, run/<pid>/NAME (src/core/executor_systems.cpp), so it
# neither wipes the first one's folder at start nor rewrites its
# MacPhoenix.cfg (the bridge dir). Then the second is stopped: its copy goes.
#
# Usage: tests/test_executor_system_sessions.sh [--timeout N] [--port N]
#        (uses --port and --port + 1)
#
# Skips (exit 77) without the tests-7.5.5 image.
set -euo pipefail

TIMEOUT=90
PORT=18540
BINARY="${MACEMU_BINARY:-$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port)    PORT="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done
source "$(dirname "$0")/lib/ports.sh"
PORT_A=$(mp_port "$PORT") || exit 1
PORT_B=$(mp_port "$((PORT + 1))") || exit 1

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
STORAGE="${MACEMU_STORAGE:-$HOME/storage}"
OUT="${TMPDIR:-/tmp}/system-sessions-test-$$"

if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
DISK="$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" tests-7.5.5 $$ 2>/dev/null || true)"
if [[ -z "$DISK" || ! -f "$DISK" ]]; then echo "SKIP: no tests-7.5.5 image"; exit 77; fi
NAME="$(basename "$DISK")"
SYSTEMS="$STORAGE/executor-systems"

mkdir -p "$OUT"
EXTFS_A=$(mktemp -d ${TMPDIR:-/tmp}/macsessions-a.XXXXXX)
EXTFS_B=$(mktemp -d ${TMPDIR:-/tmp}/macsessions-b.XXXXXX)
cleanup() {
    for pid in ${PID_A:-} ${PID_B:-}; do
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
    rm -rf "$EXTFS_A" "$EXTFS_B"
    [[ "$NAME" == test-tests-7.5.5-$$.img ]] && rm -rf "$DISK" "$DISK.json" "$SYSTEMS/$NAME" "$SYSTEMS/$NAME.clean" "$SYSTEMS/$NAME.lock"
}
trap cleanup EXIT SIGTERM SIGINT

start() {  # PORT EXTFS LOG
    "$BINARY" --config /dev/null --timeout "$((TIMEOUT + 30))" --headless-http --network none \
        --backend executor --executor-system "$NAME" --port "$1" --extfs "$2" &>"$3" &
}
up() {  # PORT PID LOG: the server answers, then the emulator is started
    for _ in $(seq 1 30); do
        curl -sf "http://localhost:$1/api/status" >/dev/null 2>&1 && break
        kill -0 "$2" 2>/dev/null || { echo "FAIL: emulator died"; tail -20 "$3"; exit 1; }
        sleep 0.5
    done
    curl -sf -X POST "http://localhost:$1/api/emulator/start" >/dev/null
}
connected() { curl -sf --max-time 5 "http://localhost:$1/api/status" | grep -q '"bridge_agent_connected": *true'; }

echo "=== Two Executor sessions on $NAME ==="
start "$PORT_A" "$EXTFS_A" "$OUT/a.log"; PID_A=$!
start "$PORT_B" "$EXTFS_B" "$OUT/b.log"; PID_B=$!
up "$PORT_A" "$PID_A" "$OUT/a.log"
# The second while the first is starting: before the fix it wiped the
# first one's System Folder and wrote its own bridge dir into it.
sleep 1
up "$PORT_B" "$PID_B" "$OUT/b.log"

for i in $(seq 1 "$TIMEOUT"); do
    connected "$PORT_A" && connected "$PORT_B" && break
    kill -0 "$PID_A" 2>/dev/null || { echo "FAIL: first emulator died"; tail -20 "$OUT/a.log"; exit 1; }
    kill -0 "$PID_B" 2>/dev/null || { echo "FAIL: second emulator died"; tail -20 "$OUT/b.log"; exit 1; }
    sleep 1
done
A=no; B=no
connected "$PORT_A" && A=yes
connected "$PORT_B" && B=yes
echo "BridgeAgent: first $A, second $B"
grep -h "is in use" "$OUT/a.log" "$OUT/b.log" || true

COPY="$SYSTEMS/run/$PID_B/$NAME"
[[ "$A" == yes && "$B" == yes ]] || { echo "FAIL: a session lost its bridge (logs in $OUT)"; exit 1; }
grep -q "is in use" "$OUT/a.log" && { echo "FAIL: the first session ran on a copy"; exit 1; }
[[ -d "$COPY/System Folder" ]] || { echo "FAIL: no copy for the second session at $COPY"; exit 1; }
# Each folder's MacPhoenix.cfg names its own session's bridge dir (the
# last word of "[Bridge] Enabled (dir=.../MacPhoenix/<pid>)").
own_cfg() {  # LOG FOLDER
    local dir; dir=$(sed -n 's/^\[Bridge\] Enabled (dir=\(.*\))$/\1/p' "$1" | head -1)
    grep -q "${dir##*/}" "$2/System Folder/Preferences/MacPhoenix.cfg" 2>/dev/null
}
own_cfg "$OUT/a.log" "$SYSTEMS/$NAME" || { echo "FAIL: the first session's MacPhoenix.cfg was rewritten"; exit 1; }
own_cfg "$OUT/b.log" "$COPY" || { echo "FAIL: the second session's copy has the wrong MacPhoenix.cfg"; exit 1; }

kill "$PID_B"; wait "$PID_B" 2>/dev/null || true
[[ -e "$SYSTEMS/run/$PID_B" ]] && { echo "FAIL: the second session's copy is left at $SYSTEMS/run/$PID_B"; exit 1; }
connected "$PORT_A" || { echo "FAIL: the first session lost its bridge when the second stopped"; exit 1; }
echo "PASS: both sessions kept their bridge; the second's copy is gone"
exit 0
