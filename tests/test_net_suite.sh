#!/bin/bash
#
# test_net_suite.sh - Run the deeper guest networking test suite.
#
# Boots the emulator with --network socket --bridge, dispatches
# tests/guest/GuestNetTest.pl to MacPerl, waits for
# Host:net_results.txt, and asserts on the PASS lines.
#
# Targets the data paths that MacTestSuite.pl::test_network doesn't:
# external HTTP with body-length integrity, 20x sequential connects,
# external UDP DNS, dead-host connect timing, post-stall liveness.
#
# Usage:
#   tests/test_net_suite.sh [--timeout 180] [--port 18296]
#                          [--os-version 7.5.5|7.6.1]
#                          [--disk path] [--rom path]
#
set -euo pipefail

TIMEOUT=180
PORT=18296
SIG_PORT=18297
BACKEND="uae"
ARCH=""
BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix"
GUEST_DIR="$(cd "$(dirname "$0")" && pwd)/guest"
OS_VERSION=""
ROM_OVERRIDE=""
DISK=""
DISMISS=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port) PORT="$2"; SIG_PORT="$((PORT + 1))"; shift 2 ;;
        --disk) DISK="$2"; shift 2 ;;
        --rom) ROM_OVERRIDE="$2"; shift 2 ;;
        --backend) BACKEND="$2"; shift 2 ;;
        --arch) ARCH="$2"; shift 2 ;;
        --os-version) OS_VERSION="$2"; shift 2 ;;
        --no-dismiss) DISMISS=0; shift ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

EXTRA_FLAGS=()
[[ -z "$ARCH" && ( "$BACKEND" == "kpx" || "$BACKEND" == "unicorn-ppc" ) ]] && ARCH=ppc
[[ "$ARCH" == "ppc" ]] && EXTRA_FLAGS+=(--ram 128)

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

if [[ -n "$ROM_OVERRIDE" ]]; then
    ROM="$ROM_OVERRIDE"
elif [[ "$ARCH" == "ppc" ]]; then
    ROM="${MACEMU_ROM:-$HOME/storage/roms/g3.rom}"
else
    ROM="${MACEMU_ROM:-$HOME/roms/quadra.rom}"
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [[ -z "$DISK" ]]; then
    if [[ -n "$OS_VERSION" ]]; then
        IMG_BASE="macos-${OS_VERSION}"
    elif [[ "$ARCH" == "ppc" ]]; then
        IMG_BASE="macos-9.0.4"
    else
        IMG_BASE="macos-7.5.5"
    fi
    DISK="${MACEMU_DISK:-$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" "$IMG_BASE")}"
fi

# --- Preflight ---
if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
if [[ ! -f "$ROM" ]]; then     echo "SKIP: ROM not found: $ROM"; exit 77; fi
if [[ ! -f "$DISK" ]]; then    echo "SKIP: Disk image not found: $DISK"; exit 77; fi

PERL_SCRIPT="$GUEST_DIR/GuestNetTest.pl"
PERL_INSTALL="$GUEST_DIR/install_perl_test.py"
[[ -f "$PERL_SCRIPT" ]] || { echo "SKIP: $PERL_SCRIPT not found"; exit 77; }

# Host must reach the same external hosts the guest will probe.
if ! getent hosts example.com >/dev/null 2>&1; then
    echo "SKIP: host cannot resolve example.com (net-bridge will have nothing to proxy)"; exit 77
fi
if ! timeout 5 bash -c "exec 3<>/dev/tcp/example.com/80" 2>/dev/null; then
    echo "SKIP: host cannot reach example.com:80"; exit 77
fi
exec 3>&- 2>/dev/null || true

# --- Setup ---
EXTFS_DIR=$(mktemp -d /tmp/mactest-net-extfs.XXXXXX)
python3 "$PERL_INSTALL" "$PERL_SCRIPT" "$EXTFS_DIR" "GuestNetTest.pl"
LOG="/tmp/mactest_net_$$.log"

cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    if [[ -f "$EXTFS_DIR/net_results.txt" && ${EXIT_CODE:-1} -ne 0 ]]; then
        echo "--- guest results ---"
        cat "$EXTFS_DIR/net_results.txt"
    fi
    if [[ ${EXIT_CODE:-1} -ne 0 ]]; then
        echo "--- emulator log (tail) ---"; tail -80 "$LOG" 2>/dev/null || true
    fi
    rm -rf "$EXTFS_DIR" "$LOG"
}
trap cleanup EXIT SIGTERM SIGINT

echo "=== Guest Network Test Suite ==="
[[ -n "$OS_VERSION" ]] && echo "OS: $OS_VERSION"
echo "ROM: $ROM"
echo "Disk: $DISK"
echo "ExtFS: $EXTFS_DIR"
echo "Port: $PORT"

DISMISS_FLAG=()
if [[ $DISMISS -eq 1 ]]; then
    DISMISS_FLAG=(--dismiss-shutdown-dialog)
else
    DISMISS_FLAG=(--no-dismiss-shutdown-dialog)
fi

"$BINARY" --backend "$BACKEND" --timeout "$((TIMEOUT + 15))" \
    --config /dev/null "${DISMISS_FLAG[@]}" --headless-http \
    --port "$PORT" --signaling-port "$SIG_PORT" \
    --disk "$DISK" --extfs "$EXTFS_DIR" \
    --network socket \
    "${EXTRA_FLAGS[@]}" "$ROM" &>"$LOG" &
EMU_PID=$!

echo -n "Waiting for server..."
for i in $(seq 1 30); do
    if curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1; then
        echo " ready"; break
    fi
    kill -0 "$EMU_PID" 2>/dev/null || { echo " FAIL: emulator exited early"; tail -20 "$LOG"; exit 1; }
    echo -n "."; sleep 0.5
done
curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null

echo -n "Booting..."
START_TIME=$(date +%s)
BOOT_TIMEOUT=$((TIMEOUT / 3 > 45 ? TIMEOUT / 3 : 45))
while true; do
    ELAPSED=$(( $(date +%s) - START_TIME ))
    if [[ $ELAPSED -ge $BOOT_TIMEOUT ]]; then
        echo ""; echo "FAIL: boot timeout ${BOOT_TIMEOUT}s"; tail -10 "$LOG"; exit 1
    fi
    PHASE=$(curl -sf "http://localhost:$PORT/api/status" 2>/dev/null \
        | grep -oP '"boot_phase"\s*:\s*"\K[^"]+' || echo "unknown")
    if [[ "$PHASE" == "Finder" || "$PHASE" == "desktop" ]]; then
        echo " $PHASE (${ELAPSED}s)"; break
    fi
    echo -n "."; sleep 1
done

sleep 3  # Finder + MacTCP DHCP settle.

echo "Launching GuestNetTest.pl..."
LAUNCH=$(curl -sf --max-time 15 -X POST "http://localhost:$PORT/api/launch" \
    -d '{"path":"Host:GuestNetTest.pl","open":true}' || echo '{"success":false}')
echo "$LAUNCH" | grep -q '"success": true' \
    || { echo "FAIL: launch response: $LAUNCH"; exit 1; }

echo -n "Running probes..."
TEST_START=$(date +%s)
TEST_TIMEOUT=$((TIMEOUT - (TEST_START - START_TIME)))
[[ $TEST_TIMEOUT -lt 30 ]] && TEST_TIMEOUT=30
RESULTS="$EXTFS_DIR/net_results.txt"
while true; do
    ELAPSED=$(( $(date +%s) - TEST_START ))
    if [[ $ELAPSED -ge $TEST_TIMEOUT ]]; then
        echo ""; echo "FAIL: test timeout ${TEST_TIMEOUT}s"; exit 1
    fi
    if [[ -f "$RESULTS" ]] && grep -q '\-\-\-' "$RESULTS" 2>/dev/null; then
        echo " done (${ELAPSED}s)"; break
    fi
    echo -n "."; sleep 2
done

echo ""
echo "--- Results ---"
cat "$RESULTS"
echo ""

tr '\r' '\n' < "$RESULTS" > "${RESULTS}.unix"
RU="${RESULTS}.unix"

FAIL_COUNT=$(grep -c "^FAIL " "$RU" || true)
PASS_COUNT=$(grep -c "^PASS " "$RU" || true)
SKIP_COUNT=$(grep -c "^SKIP " "$RU" || true)

# The required baseline — these must PASS for a healthy NAT:
EXIT_CODE=0
require_pass() {
    local pat="$1"
    if ! grep -q "^PASS .*${pat}" "$RU"; then
        echo "ASSERT FAIL: missing PASS matching '${pat}'"
        EXIT_CODE=1
    fi
}
require_pass 'http_dns$'
require_pass 'http_connect$'
require_pass 'http_read_[0-9]\+_bytes$'
require_pass 'http_status_[23][0-9]\+$'
# body-length integrity (Content-Length is often absent on chunked/close)
require_pass 'http_body_[0-9]\+_bytes$'
require_pass 'seq_connects_[0-9]\+_of_[0-9]\+_in_'
require_pass 'udp_ext_send$'
require_pass 'udp_ext_recv_[0-9]\+_bytes$'
require_pass 'udp_ext_txid$'
require_pass 'udp_ext_answers_'
# Large TCP transfer — the Mac-receive-window flow-control check.
require_pass 'bulk_connect$'
require_pass 'bulk_received_[0-9]\+_bytes$'
require_pass 'bulk_length_exactly_65536$'
require_pass 'bulk_pattern_intact$'
# post_stall_connect PASS proves the NAT survived the dead-host attempt
require_pass 'post_stall_connect_'

echo "$PASS_COUNT passed, $FAIL_COUNT failed, $SKIP_COUNT skipped"

echo -n "Shutting down..."
SHUT=$(curl -sf --max-time 10 -X POST "http://localhost:$PORT/api/shutdown" -d '{}' 2>/dev/null || echo '{}')
if echo "$SHUT" | grep -q '"success": true'; then
    for i in $(seq 1 20); do
        kill -0 "$EMU_PID" 2>/dev/null || { echo " done (${i}s)"; EMU_PID=""; break; }
        sleep 1
    done
fi

if [[ $FAIL_COUNT -gt 0 || $EXIT_CODE -ne 0 ]]; then
    EXIT_CODE=1
    echo "FAIL"
    exit 1
fi
EXIT_CODE=0
echo "PASS"
exit 0
