#!/bin/bash
#
# test_config_api.sh - Verify config API round-trip and CDROM/boot settings
#
# Tests:
#   1. GET /api/config returns bootdriver field
#   2. POST /api/config saves bootdriver
#   3. GET /api/config reflects saved value
#   4. CDROM paths appear in config
#   5. Config file on disk is updated
#
set -euo pipefail

PORT="${1:-18096}"
SIG_PORT="$((PORT + 1))"
ROM="${MACEMU_ROM:-/home/mick/quadra.rom}"
BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix"
TMPCONFIG=$(mktemp)
PASSED=0
FAILED=0

if [[ ! -x "$BINARY" ]]; then
    echo "SKIP: Binary not found: $BINARY"
    exit 77
fi

if [[ ! -f "$ROM" ]]; then
    echo "SKIP: ROM not found: $ROM"
    exit 77
fi

# Create a clean config with known values
cat > "$TMPCONFIG" << EOF
{
  "architecture": "m68k",
  "cpu_backend": "uae",
  "ram_mb": 32,
  "rom": "",
  "disks": [],
  "cdroms": ["System-7-Version-7.5.iso"],
  "bootdriver": 0,
  "storage_dir": "/home/mick/storage",
  "m68k": {"cpu_type": 4, "fpu": true, "modelid": 14}
}
EOF

cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    rm -f "$TMPCONFIG"
}
trap cleanup EXIT

# Start emulator with clean config
"$BINARY" --config "$TMPCONFIG" --port "$PORT" --signaling-port "$SIG_PORT" "$ROM" &>/tmp/test_config_$$.log &
EMU_PID=$!

# Wait for server
for i in $(seq 1 20); do
    curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1 && break
    sleep 0.5
done

check() {
    local desc="$1" result="$2"
    if [[ "$result" == "OK" ]]; then
        echo "  PASS: $desc"
        PASSED=$((PASSED + 1))
    else
        echo "  FAIL: $desc ($result)"
        FAILED=$((FAILED + 1))
    fi
}

echo "=== Config API Tests ==="

# Test 1: GET /api/config has bootdriver
R=$(curl -s "http://localhost:$PORT/api/config" | python3 -c "
import sys,json; d=json.load(sys.stdin)
print('OK' if 'bootdriver' in d else 'missing bootdriver')
" 2>/dev/null)
check "GET /api/config has bootdriver field" "$R"

# Test 2: GET /api/config has cdroms
R=$(curl -s "http://localhost:$PORT/api/config" | python3 -c "
import sys,json; d=json.load(sys.stdin)
cdroms=d.get('cdroms',[])
print('OK' if any('System-7' in c for c in cdroms) else f'unexpected: {cdroms}')
" 2>/dev/null)
check "GET /api/config has CDROM entry" "$R"

# Test 3: POST bootdriver=-62 saves
R=$(curl -s -X POST "http://localhost:$PORT/api/config" \
    -H 'Content-Type: application/json' \
    -d '{"bootdriver":-62}' | python3 -c "
import sys,json; d=json.load(sys.stdin)
print('OK' if d.get('success') else 'save failed')
" 2>/dev/null)
check "POST /api/config saves bootdriver=-62" "$R"

# Test 4: GET reflects saved bootdriver
R=$(curl -s "http://localhost:$PORT/api/config" | python3 -c "
import sys,json; d=json.load(sys.stdin)
v=d.get('bootdriver')
print('OK' if v==-62 else f'got {v}')
" 2>/dev/null)
check "GET /api/config reflects bootdriver=-62" "$R"

# Test 5: POST bootdriver=0 resets
R=$(curl -s -X POST "http://localhost:$PORT/api/config" \
    -H 'Content-Type: application/json' \
    -d '{"bootdriver":0}' | python3 -c "
import sys,json; d=json.load(sys.stdin)
print('OK' if d.get('success') else 'save failed')
" 2>/dev/null)
check "POST /api/config resets bootdriver=0" "$R"

# Test 6: Config file on disk updated
R=$(python3 -c "
import json
d=json.load(open('$TMPCONFIG'))
print('OK' if d.get('bootdriver')==0 else f'got {d.get(\"bootdriver\")}')
" 2>/dev/null)
check "Config file on disk has bootdriver=0" "$R"

# Test 7: CDROM in log output
R=$(grep -c "Added CDROM to prefs" /tmp/test_config_$$.log 2>/dev/null)
if [[ "$R" -ge 1 ]]; then
    check "CDROM added to prefs at startup" "OK"
else
    check "CDROM added to prefs at startup" "not found in log"
fi

echo ""
echo "Results: $PASSED passed, $FAILED failed"
[[ $FAILED -eq 0 ]]
