#!/bin/bash
# The Executor backend's cursor drawn by mac-phoenix's page (client/cursor.js),
# not in the frames: starts Executor (Executor's own System; CODEC: png or
# h264), and tests/web/cursor_client.mjs checks the UI made it its CSS cursor.
# Skips (77) without node or playwright.
#
#   tests/test_cursor_executor.sh [--codec png|h264] [--port N]
set -u
CODEC=png
PORT=18444
while [[ $# -gt 0 ]]; do
    case "$1" in
        --codec) CODEC="$2"; shift 2 ;;
        --port)  PORT="$2";  shift 2 ;;
        *) echo "unknown option $1"; exit 2 ;;
    esac
done
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${MACEMU_BINARY:-$ROOT/build/mac-phoenix}"
command -v node >/dev/null || { echo "SKIP: no node"; exit 77; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/cursor-executor.XXXXXX")
PID=
cleanup() { [[ -n "$PID" ]] && kill "$PID" 2>/dev/null && wait "$PID" 2>/dev/null; rm -rf "$WORK"; }
trap cleanup EXIT
mkdir -p "$WORK/storage"
printf '{"backend":"executor","codec":"%s","screen":"640x480","storage_dir":"%s"}' \
    "$CODEC" "$WORK/storage" > "$WORK/cfg.json"
"$BIN" --config "$WORK/cfg.json" --storage-dir "$WORK/storage" --port "$PORT" --timeout 90 \
    > "$WORK/server.log" 2>&1 &
PID=$!
for i in $(seq 1 50); do
    curl -s "localhost:$PORT/api/status" >/dev/null 2>&1 && break
    sleep 0.2
done
curl -s -X POST "localhost:$PORT/api/emulator/start" >/dev/null
node "$ROOT/tests/web/cursor_client.mjs" "$PORT"
rc=$?
[[ $rc -ne 0 && $rc -ne 77 ]] && tail -20 "$WORK/server.log"
exit $rc
