#!/bin/bash
#
# test_ssl_mitm.sh - Guest-side smoke test for the host MITM TLS proxy.
#
# Boots the emulator with --network socket --mitm-tls --bridge, dispatches
# tests/guest/SslMitmTest.pl to MacPerl, and checks the results file
# for PASS lines covering: record-type, protocol version (SSLv3 or TLS1.0),
# ServerHello, and a weak-RSA cipher (RC4-MD5 / RC4-SHA / 3DES-SHA /
# export-RC4 / export-DES40).
#
# The guest script sends a hand-built SSLv3 ClientHello over a raw TCP
# connection. Without the MITM, modern public servers negotiate TLS1.2/1.3
# (which the ClientHello doesn't offer) and reply with a protocol_version
# alert — this script would then fail, as intended.
#
# Usage:
#   tests/test_ssl_mitm.sh [--timeout 90] [--port 18196]
#                          [--disk path] [--rom path] [--os-version 7.5.5|7.6]
#                          [--target example.com]
#
set -euo pipefail

TIMEOUT=90
PORT=18196
SIG_PORT=18197
BACKEND="uae"
ARCH=""
TARGET_HOST="example.com"
BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix"
GUEST_DIR="$(cd "$(dirname "$0")" && pwd)/guest"
OS_VERSION=""
ROM_OVERRIDE=""
DISK=""
DISMISS=1
# Negative-control mode: run the exact same boot/dispatch flow WITHOUT
# --mitm-tls, then invert the assertions. Any PASS at the handshake
# level would mean the modern public server somehow answered an SSLv3
# ClientHello with a normal-looking ServerHello, which would be real
# evidence the positive test isn't measuring what we think. Dev-only.
NEGATIVE_CONTROL=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port) PORT="$2"; SIG_PORT="$((PORT + 1))"; shift 2 ;;
        --disk) DISK="$2"; shift 2 ;;
        --rom) ROM_OVERRIDE="$2"; shift 2 ;;
        --backend) BACKEND="$2"; shift 2 ;;
        --arch) ARCH="$2"; shift 2 ;;
        --os-version) OS_VERSION="$2"; shift 2 ;;
        --target) TARGET_HOST="$2"; shift 2 ;;
        --no-dismiss) DISMISS=0; shift ;;
        --negative-control) NEGATIVE_CONTROL=1; shift ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

EXTRA_FLAGS=()
# --arch was deprecated; backend determines arch. Derive ARCH from BACKEND for
# the ROM/RAM selection logic only (not passed to the binary).
[[ -z "$ARCH" && ( "$BACKEND" == "kpx" || "$BACKEND" == "unicorn-ppc" ) ]] && ARCH=ppc
[[ "$ARCH" == "ppc" ]] && EXTRA_FLAGS+=(--ram 128)

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

# ROM + disk defaults (mirror test_guest_suite.sh).
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
if [[ ! -x "$BINARY" ]]; then
    echo "SKIP: Binary not found: $BINARY"; exit 77
fi
if [[ ! -f "$ROM" ]]; then
    echo "SKIP: ROM not found: $ROM"; exit 77
fi
if [[ ! -f "$DISK" ]]; then
    echo "SKIP: Disk image not found: $DISK"; exit 77
fi

PERL_SCRIPT="$GUEST_DIR/SslMitmTest.pl"
PERL_INSTALL="$GUEST_DIR/install_perl_test.py"
if [[ ! -f "$PERL_SCRIPT" ]]; then
    echo "SKIP: Perl test script not found: $PERL_SCRIPT"; exit 77
fi

# --- Preflight: host must be able to reach the upstream target ---
# The MITM connects out to $TARGET_HOST:443 from the host side. If that
# fails (offline lab, firewall) the test would look like a MITM bug.
if ! getent hosts "$TARGET_HOST" >/dev/null 2>&1; then
    echo "SKIP: Host cannot resolve $TARGET_HOST (no DNS — MITM upstream will fail)"; exit 77
fi
if ! timeout 5 bash -c "exec 3<>/dev/tcp/$TARGET_HOST/443" 2>/dev/null; then
    echo "SKIP: Host cannot reach $TARGET_HOST:443 (firewall/offline — MITM upstream will fail)"; exit 77
fi
exec 3>&- 2>/dev/null || true

# --- Setup ExtFS + CA dir ---
EXTFS_DIR=$(mktemp -d /tmp/mactest-mitm-extfs.XXXXXX)
CA_DIR=$(mktemp -d /tmp/mactest-mitm-ca.XXXXXX)
python3 "$PERL_INSTALL" "$PERL_SCRIPT" "$EXTFS_DIR" "SslMitmTest.pl"

LOG="/tmp/mactest_ssl_mitm_$$.log"
BRIDGE_LOG="/tmp/mactest_ssl_mitm_bridge_$$.log"

cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    if [[ -f "$EXTFS_DIR/ssl_mitm_results.txt" && ${EXIT_CODE:-1} -ne 0 ]]; then
        echo "--- guest results ---"
        cat "$EXTFS_DIR/ssl_mitm_results.txt"
    fi
    if [[ ${EXIT_CODE:-1} -ne 0 ]]; then
        echo "--- emulator log (tail) ---"; tail -50 "$LOG" 2>/dev/null || true
        echo "--- bridge/mitm log (grep mitm) ---"; grep -iE "(mitm|tls)" "$LOG" 2>/dev/null | head -30 || true
    fi
    rm -rf "$EXTFS_DIR" "$CA_DIR" "$LOG"
}
trap cleanup EXIT SIGTERM SIGINT

echo "=== MITM TLS Guest Test ==="
[[ -n "$OS_VERSION" ]] && echo "OS: $OS_VERSION"
echo "ROM: $ROM"
echo "Disk: $DISK"
echo "ExtFS: $EXTFS_DIR"
echo "CA dir: $CA_DIR"
echo "Target: $TARGET_HOST:443"
echo "Port: $PORT"

DISMISS_FLAG=()
if [[ $DISMISS -eq 1 ]]; then
    DISMISS_FLAG=(--dismiss-shutdown-dialog)
else
    DISMISS_FLAG=(--no-dismiss-shutdown-dialog)
fi

MITM_FLAGS=(--mitm-tls --mitm-ca-dir "$CA_DIR")
if [[ $NEGATIVE_CONTROL -eq 1 ]]; then
    echo "*** NEGATIVE CONTROL: --mitm-tls DISABLED; expecting FAIL ***"
    MITM_FLAGS=()
fi

"$BINARY" --backend "$BACKEND" --timeout "$((TIMEOUT + 15))" \
    --config /dev/null "${DISMISS_FLAG[@]}" --headless-http \
    --port "$PORT" --signaling-port "$SIG_PORT" \
    --disk "$DISK" --extfs "$EXTFS_DIR" \
    --network socket \
    "${MITM_FLAGS[@]}" \
    "${EXTRA_FLAGS[@]}" "$ROM" &>"$LOG" &
EMU_PID=$!

# --- Wait for HTTP server ---
echo -n "Waiting for server..."
for i in $(seq 1 30); do
    if curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1; then
        echo " ready"; break
    fi
    if ! kill -0 "$EMU_PID" 2>/dev/null; then
        echo " FAIL: emulator exited early"; tail -20 "$LOG"; exit 1
    fi
    echo -n "."; sleep 0.5
done

curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null

# --- Wait for boot ---
echo -n "Booting..."
START_TIME=$(date +%s)
BOOT_TIMEOUT=$((TIMEOUT / 2 > 45 ? TIMEOUT / 2 : 45))
while true; do
    ELAPSED=$(( $(date +%s) - START_TIME ))
    if [[ $ELAPSED -ge $BOOT_TIMEOUT ]]; then
        echo ""
        echo "FAIL: Boot timeout after ${BOOT_TIMEOUT}s"
        tail -10 "$LOG"; exit 1
    fi
    PHASE=$(curl -sf "http://localhost:$PORT/api/status" 2>/dev/null \
        | grep -oP '"boot_phase"\s*:\s*"\K[^"]+' || echo "unknown")
    if [[ "$PHASE" == "Finder" || "$PHASE" == "desktop" ]]; then
        echo " $PHASE (${ELAPSED}s)"; break
    fi
    echo -n "."; sleep 1
done

sleep 3  # Finder + MacTCP/OpenTransport settle (DHCP with net-bridge)

if [[ $NEGATIVE_CONTROL -eq 0 ]]; then
    # Confirm MITM CA materialized host-side — proves the flag was plumbed.
    if [[ ! -f "$CA_DIR/mitm_ca.crt" ]]; then
        echo "FAIL: MITM CA not created at $CA_DIR — net-bridge may not have received --mitm-tls"
        exit 1
    fi
    echo "MITM CA present: $CA_DIR/mitm_ca.crt"
fi

# --- Dispatch ---
echo "Launching SslMitmTest.pl..."
LAUNCH=$(curl -sf --max-time 15 -X POST "http://localhost:$PORT/api/launch" \
    -d '{"path":"Host:SslMitmTest.pl","open":true}' || echo '{"success":false}')
if ! echo "$LAUNCH" | grep -q '"success": true'; then
    echo "FAIL: Could not launch SslMitmTest.pl"
    echo "  Response: $LAUNCH"; exit 1
fi

# --- Wait for results ---
echo -n "Waiting for tests..."
TEST_START=$(date +%s)
TEST_TIMEOUT=$((TIMEOUT - (TEST_START - START_TIME)))
[[ $TEST_TIMEOUT -lt 15 ]] && TEST_TIMEOUT=15
RESULTS_FILE="$EXTFS_DIR/ssl_mitm_results.txt"
while true; do
    ELAPSED=$(( $(date +%s) - TEST_START ))
    if [[ $ELAPSED -ge $TEST_TIMEOUT ]]; then
        echo ""
        echo "FAIL: Test timeout after ${TEST_TIMEOUT}s"; exit 1
    fi
    if [[ -f "$RESULTS_FILE" ]] && grep -q '\-\-\-' "$RESULTS_FILE" 2>/dev/null; then
        echo " done (${ELAPSED}s)"; break
    fi
    echo -n "."; sleep 1
done

# --- Parse ---
echo ""
echo "--- Results ---"
cat "$RESULTS_FILE"
echo ""

tr '\r' '\n' < "$RESULTS_FILE" > "${RESULTS_FILE}.unix"
RU="${RESULTS_FILE}.unix"

FAIL_COUNT=$(grep -c "^FAIL " "$RU" || true)
PASS_COUNT=$(grep -c "^PASS " "$RU" || true)
SKIP_COUNT=$(grep -c "^SKIP " "$RU" || true)

# Required assertions: the MITM must have produced a recognizable
# SSLv3/TLS1.0 ServerHello carrying a weak-RSA cipher.
require_pass() {
    local pattern="$1"
    if ! grep -q "^PASS .*${pattern}" "$RU"; then
        echo "ASSERT FAIL: missing PASS matching '${pattern}'"
        EXIT_CODE=1
    fi
}
EXIT_CODE=0
if [[ $NEGATIVE_CONTROL -eq 1 ]]; then
    # Without MITM we expect a modern server to reject SSLv3 — so
    # ssl_mitm_record_handshake MUST NOT have passed (it'd either FAIL
    # at record_type or never reach that line at all).
    if grep -q '^PASS .*ssl_mitm_record_handshake$' "$RU" \
        && grep -q '^PASS .*ssl_mitm_server_hello$' "$RU"; then
        echo "ASSERT FAIL [negative control]: handshake should not have succeeded without MITM"
        EXIT_CODE=1
    else
        echo "NEGATIVE CONTROL OK: handshake did not succeed without MITM"
    fi
else
    require_pass 'ssl_mitm_connect$'
    require_pass 'ssl_mitm_record_handshake$'
    require_pass 'ssl_mitm_\(sslv3\|tls10\)$'
    require_pass 'ssl_mitm_server_hello$'
    require_pass 'ssl_mitm_cipher_RSA_'
fi

echo "$PASS_COUNT passed, $FAIL_COUNT failed, $SKIP_COUNT skipped"

# --- Shutdown ---
echo -n "Shutting down..."
SHUT_RESP=$(curl -sf --max-time 10 -X POST "http://localhost:$PORT/api/shutdown" \
    -d '{}' 2>/dev/null || echo '{"success":false}')
if echo "$SHUT_RESP" | grep -q '"success": true'; then
    for i in $(seq 1 20); do
        if ! kill -0 "$EMU_PID" 2>/dev/null; then
            echo " done (${i}s)"; EMU_PID=""; break
        fi
        sleep 1
    done
fi

if [[ $NEGATIVE_CONTROL -eq 1 ]]; then
    # In negative-control mode "failure to downgrade" is the success signal.
    if [[ $EXIT_CODE -eq 0 ]]; then
        echo "PASS [negative control]"
        exit 0
    else
        echo "FAIL [negative control]"
        exit 1
    fi
fi
if [[ $FAIL_COUNT -gt 0 || $EXIT_CODE -ne 0 ]]; then
    EXIT_CODE=1
    echo "FAIL"
    exit 1
else
    EXIT_CODE=0
    echo "PASS"
    exit 0
fi
