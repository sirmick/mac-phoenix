#!/bin/bash
# launch_probe.sh - boot the Power Mac headless with the bridge, wait for
# BridgeAgent, POST /api/launch, and keep the emulator log for inspection.
#
# Usage: tests/launch_probe.sh --core NAME [--jit] [--port N] [--log FILE] [--path "HD:App"]
set -uo pipefail
CORE=mame-ppc; PORT=18230; LOG=/tmp/launch_probe.log; EXTRA=(); APPPATH="MacOS 7.5.5:SimpleText"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --core) CORE="$2"; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
        --log) LOG="$2"; shift 2 ;;
        --path) APPPATH="$2"; shift 2 ;;
        --jit|--no-jit) EXTRA+=("$1"); shift ;;
        *) echo "unknown arg $1"; exit 1 ;;
    esac
done
HERE="$(cd "$(dirname "$0")" && pwd)"
DISK=$(bash "$HERE/lib/refresh_test_disk.sh" macos-7.5.5)
ROM="${MACEMU_ROM:-$HOME/storage/roms/g3.rom}"
"$HERE/../build/mac-phoenix" --backend kpx --core "$CORE" "${EXTRA[@]}" --timeout 120 \
    --config /dev/null --dismiss-shutdown-dialog --headless-http --port "$PORT" \
    --disk "$DISK" --rom "$ROM" > "$LOG" 2>&1 &
PID=$!
for i in $(seq 1 100); do
    if curl -s "localhost:$PORT/api/status" > /dev/null 2>&1; then break; fi
    sleep 0.2
done
curl -s -X POST "localhost:$PORT/api/emulator/start" > /dev/null
for i in $(seq 1 400); do
    S=$(curl -s "localhost:$PORT/api/status" || true)
    if echo "$S" | grep -q '"bridge_agent_connected": true'; then break; fi
    sleep 0.25
done
echo "status before launch: $(echo "$S" | cut -c1-160)"
R=$(curl -s -m 30 -X POST "localhost:$PORT/api/launch" -H 'Content-Type: application/json' -d "{\"path\":\"$APPPATH\"}")
echo "launch: ${R:-<no response>}"
sleep 3
S=$(curl -s -m 5 "localhost:$PORT/api/status" || echo "<no status>")
echo "status after: $(echo "$S" | cut -c1-200)"
kill "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true
