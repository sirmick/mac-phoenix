#!/bin/bash
#
# test_extfs.sh - ExtFS config and shared folder tests
#
# Tests:
#   1. Config field extfs is an array in GET /api/config
#   2. POST /api/config with extfs paths saves correctly
#   3. GET reflects saved extfs paths
#   4. Config file on disk has extfs array
#   5. --extfs CLI flag adds paths
#   6. Multiple --extfs flags accumulate
#   7. Backward compat: string extfs in JSON is read as array
#   8. ExtFS init validates directory exists
#
set -euo pipefail

PORT="${1:-18098}"
SIG_PORT="$((PORT + 1))"
ROM="${MACEMU_ROM:-/home/mick/quadra.rom}"
BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix"
TMPCONFIG=$(mktemp)
TMPDIR_EXTFS=$(mktemp -d)
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

cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    rm -f "$TMPCONFIG"
    rm -rf "$TMPDIR_EXTFS"
}
trap cleanup EXIT

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

echo "=== ExtFS Tests ==="

# --- Test 1-4: Config API round-trip ---

echo ""
echo "--- Config API round-trip ---"

cat > "$TMPCONFIG" << EOF
{
  "architecture": "m68k",
  "cpu_backend": "uae",
  "ram_mb": 32,
  "rom": "",
  "disks": [],
  "extfs": [],
  "storage_dir": "/home/mick/storage",
  "m68k": {"cpu_type": 4, "fpu": true, "modelid": 14}
}
EOF

"$BINARY" --config "$TMPCONFIG" --port "$PORT" --signaling-port "$SIG_PORT" "$ROM" &>/tmp/test_extfs_$$.log &
EMU_PID=$!

for i in $(seq 1 20); do
    curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1 && break
    sleep 0.5
done

# Test 1: extfs field is an array
R=$(curl -s "http://localhost:$PORT/api/config" | python3 -c "
import sys,json; d=json.load(sys.stdin)
e=d.get('extfs')
print('OK' if isinstance(e, list) else f'not array: {type(e).__name__}={e}')
" 2>/dev/null)
check "GET /api/config has extfs as array" "$R"

# Test 2: POST extfs paths
R=$(curl -s -X POST "http://localhost:$PORT/api/config" \
    -H 'Content-Type: application/json' \
    -d "{\"extfs\":[\"$TMPDIR_EXTFS\",\"/tmp\"]}" | python3 -c "
import sys,json; d=json.load(sys.stdin)
print('OK' if d.get('success') else 'save failed')
" 2>/dev/null)
check "POST /api/config saves extfs paths" "$R"

# Test 3: GET reflects saved paths
R=$(curl -s "http://localhost:$PORT/api/config" | python3 -c "
import sys,json; d=json.load(sys.stdin)
e=d.get('extfs',[])
print('OK' if len(e)==2 and '/tmp' in e else f'got {e}')
" 2>/dev/null)
check "GET /api/config reflects extfs paths" "$R"

# Test 4: Config file on disk updated
R=$(python3 -c "
import json
d=json.load(open('$TMPCONFIG'))
e=d.get('extfs',[])
print('OK' if isinstance(e, list) and len(e)==2 else f'got {e}')
" 2>/dev/null)
check "Config file on disk has extfs array" "$R"

# Stop emulator for next test
kill "$EMU_PID" 2>/dev/null || true
wait "$EMU_PID" 2>/dev/null || true
unset EMU_PID
sleep 0.5

# --- Test 5-6: CLI flags ---

echo ""
echo "--- CLI flags ---"

cat > "$TMPCONFIG" << EOF
{
  "architecture": "m68k",
  "cpu_backend": "uae",
  "ram_mb": 32,
  "rom": "",
  "disks": [],
  "extfs": [],
  "m68k": {"cpu_type": 4, "fpu": true, "modelid": 14}
}
EOF

# Test 5: Single --extfs flag
"$BINARY" --config "$TMPCONFIG" --port "$PORT" --signaling-port "$SIG_PORT" \
    --extfs "$TMPDIR_EXTFS" "$ROM" &>/tmp/test_extfs_$$.log &
EMU_PID=$!

for i in $(seq 1 20); do
    curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1 && break
    sleep 0.5
done

R=$(curl -s "http://localhost:$PORT/api/config" | python3 -c "
import sys,json; d=json.load(sys.stdin)
e=d.get('extfs',[])
print('OK' if len(e)==1 and '$TMPDIR_EXTFS' in e[0] else f'got {e}')
" 2>/dev/null)
check "Single --extfs flag adds path" "$R"

kill "$EMU_PID" 2>/dev/null || true
wait "$EMU_PID" 2>/dev/null || true
unset EMU_PID
sleep 0.5

# Test 6: Multiple --extfs flags
"$BINARY" --config "$TMPCONFIG" --port "$PORT" --signaling-port "$SIG_PORT" \
    --extfs "$TMPDIR_EXTFS" --extfs "/tmp" "$ROM" &>/tmp/test_extfs_$$.log &
EMU_PID=$!

for i in $(seq 1 20); do
    curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1 && break
    sleep 0.5
done

R=$(curl -s "http://localhost:$PORT/api/config" | python3 -c "
import sys,json; d=json.load(sys.stdin)
e=d.get('extfs',[])
print('OK' if len(e)==2 else f'expected 2, got {e}')
" 2>/dev/null)
check "Multiple --extfs flags accumulate" "$R"

kill "$EMU_PID" 2>/dev/null || true
wait "$EMU_PID" 2>/dev/null || true
unset EMU_PID
sleep 0.5

# --- Test 7: Backward compatibility ---

echo ""
echo "--- Backward compatibility ---"

# Old-style single string extfs
cat > "$TMPCONFIG" << EOF
{
  "architecture": "m68k",
  "cpu_backend": "uae",
  "ram_mb": 32,
  "rom": "",
  "disks": [],
  "extfs": "$TMPDIR_EXTFS",
  "m68k": {"cpu_type": 4, "fpu": true, "modelid": 14}
}
EOF

"$BINARY" --config "$TMPCONFIG" --port "$PORT" --signaling-port "$SIG_PORT" \
    "$ROM" &>/tmp/test_extfs_$$.log &
EMU_PID=$!

for i in $(seq 1 20); do
    curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1 && break
    sleep 0.5
done

R=$(curl -s "http://localhost:$PORT/api/config" | python3 -c "
import sys,json; d=json.load(sys.stdin)
e=d.get('extfs',[])
print('OK' if isinstance(e, list) and len(e)==1 and '$TMPDIR_EXTFS' in e[0] else f'got {e}')
" 2>/dev/null)
check "Backward compat: string extfs read as array" "$R"

kill "$EMU_PID" 2>/dev/null || true
wait "$EMU_PID" 2>/dev/null || true
unset EMU_PID
sleep 0.5

# --- Test 8: ExtFS init validates directory ---

echo ""
echo "--- ExtFS init validation ---"

# Point extfs at a valid directory and check no crash
cat > "$TMPCONFIG" << EOF
{
  "architecture": "m68k",
  "cpu_backend": "uae",
  "ram_mb": 32,
  "rom": "",
  "disks": [],
  "extfs": ["$TMPDIR_EXTFS"],
  "m68k": {"cpu_type": 4, "fpu": true, "modelid": 14}
}
EOF

"$BINARY" --config "$TMPCONFIG" --port "$PORT" --signaling-port "$SIG_PORT" \
    --timeout 5 "$ROM" &>/tmp/test_extfs_$$.log &
EMU_PID=$!

for i in $(seq 1 20); do
    curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1 && break
    sleep 0.5
done

R=$(curl -s "http://localhost:$PORT/api/status" | python3 -c "
import sys,json; d=json.load(sys.stdin)
print('OK' if 'emulator_running' in d else 'status check failed')
" 2>/dev/null)
check "ExtFS with valid directory does not crash" "$R"

echo ""
echo "Results: $PASSED passed, $FAILED failed"
[[ $FAILED -eq 0 ]]
