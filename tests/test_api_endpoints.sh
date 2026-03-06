#!/bin/bash
#
# test_api_endpoints.sh - Smoke test all API endpoints
#
# Starts emulator (no ROM needed for most tests), verifies each endpoint
# returns expected status codes and JSON structure.
#
set -uo pipefail

PORT=18092
SIG_PORT=18093
BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix"
ROM="${MACEMU_ROM:-/home/mick/quadra.rom}"
PASS=0
FAIL=0

if [[ ! -x "$BINARY" ]]; then
    echo "SKIP: Binary not found: $BINARY"
    exit 77
fi

echo "=== API Endpoint Tests ==="

# Start emulator (with ROM if available, without for basic endpoint testing)
ARGS=()
if [[ -f "$ROM" ]]; then
    ARGS+=("$ROM")
fi

CPU_BACKEND=uae \
EMULATOR_TIMEOUT=20 \
    "$BINARY" --port "$PORT" --signaling-port "$SIG_PORT" "${ARGS[@]}" &>/tmp/macemu_api_test_$$.log &
EMU_PID=$!

cleanup() {
    kill "$EMU_PID" 2>/dev/null || true
    wait "$EMU_PID" 2>/dev/null || true
}
trap cleanup EXIT

# Wait for server
echo -n "Waiting for server..."
SERVER_UP=false
for i in $(seq 1 20); do
    HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" "http://localhost:$PORT/api/status" 2>/dev/null || echo "000")
    if [[ "$HTTP_CODE" == "200" ]]; then
        SERVER_UP=true
        echo " ready"
        break
    fi
    if ! kill -0 "$EMU_PID" 2>/dev/null; then
        echo " FAIL: emulator exited"
        cat /tmp/macemu_api_test_$$.log | tail -20
        exit 1
    fi
    echo -n "."
    sleep 0.5
done
if [[ "$SERVER_UP" != "true" ]]; then
    echo " FAIL: server did not start"
    cat /tmp/macemu_api_test_$$.log | tail -20
    exit 1
fi

check() {
    local desc="$1" url="$2" method="${3:-GET}" expected_code="${4:-200}"
    local actual_code
    echo -n "  $desc... "
    if [[ "$method" == "GET" ]]; then
        actual_code=$(curl -s -o /dev/null -w "%{http_code}" "http://localhost:$PORT$url" 2>/dev/null || echo "000")
    else
        actual_code=$(curl -s -o /dev/null -w "%{http_code}" -X "$method" "http://localhost:$PORT$url" 2>/dev/null || echo "000")
    fi
    if [[ "$actual_code" == "$expected_code" ]]; then
        echo "OK ($actual_code)"
        PASS=$((PASS + 1))
    else
        echo "FAIL (expected $expected_code, got $actual_code)"
        FAIL=$((FAIL + 1))
    fi
}

check_json_field() {
    local desc="$1" url="$2" field="$3"
    echo -n "  $desc... "
    local body
    body=$(curl -s "http://localhost:$PORT$url" 2>/dev/null || echo "")
    if echo "$body" | grep -q "\"$field\""; then
        echo "OK (field '$field' present)"
        PASS=$((PASS + 1))
    else
        echo "FAIL (field '$field' missing in: $body)"
        FAIL=$((FAIL + 1))
    fi
}

echo ""
echo "--- Status API ---"
check "GET /api/status" "/api/status"
check_json_field "status has emulator_running" "/api/status" "emulator_running"
check_json_field "status has boot_phase" "/api/status" "boot_phase"
check_json_field "status has checkload_count" "/api/status" "checkload_count"
check_json_field "status has boot_elapsed" "/api/status" "boot_elapsed"

echo ""
echo "--- Config API ---"
check "GET /api/config" "/api/config"
check "GET /api/storage" "/api/storage"

echo ""
echo "--- Screenshot API ---"
check "GET /api/screenshot (no frames yet)" "/api/screenshot" "GET" "503"

echo ""
echo "--- Mouse API ---"
check "GET /api/mouse (not running)" "/api/mouse" "GET" "503"

echo ""
echo "--- Unknown endpoint ---"
check "GET /api/nonexistent" "/api/nonexistent" "GET" "404"

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="
if [[ $FAIL -gt 0 ]]; then
    exit 1
fi
exit 0
