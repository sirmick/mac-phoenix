#!/bin/bash
#
# test_guest_suite.sh - Run the guest-side Mac test suite
#
# Boots the emulator, launches the guest test suite through the bridge,
# waits for it to finish, then reads results from the shared folder. The
# suite is MacPhoenixSuite (tests/guest/suite: a fat application built with
# tools/mpw/mpw); --script NAME.pl dispatches a MacPerl script instead.
#
# Usage:
#   tests/test_guest_suite.sh [--timeout 60] [--port 18094] [--disk path]
#                             [--rom path] [--os-version 7.5.5|7.6]
#                             [--network MODE] [--backend uae|kpx|executor]
#                             [--script NAME.pl] [--results FILE]
#                             [--sections "disk extfs ..."] [--app SUITE.bin]
#                             [--image NAME]
#                             [--executor-debug memcheck,...]
#                             [--key-on PATTERN:KEYCODE]
#
# --script runs a MacPerl script from tests/guest instead of the suite;
# --results is the file either writes on Host: (default test_results.txt).
# Same report format: PASS/FAIL/SKIP lines, then "---". --sections: the
# suite's sections to run (default: disk extfs audio applescript network
# serial). --app: another build of the suite (MacBinary), e.g. the
# CodeWarrior one (tools/mpw/mpw --toolchain cw build tests/guest/suite -o DIR).
# --image: boot a copy of another built image (provisioning/images/NAME.json),
# e.g. dev-7.6.1, whose Extensions have the CFM-68K Runtime Enabler.
# --key-on: press a key (Mac keycode) once the results file has a line with
# PATTERN, for an app that waits on the user (tests/guest/notify: Return on
# the Notification Manager's alert).
#
# --backend executor runs on the System Folder of the run's copy of
# tests-<os-version> (<storage>/images/test-tests-<ver>-<pid>.img; no ROM;
# the copy is mounted writable beside it, MacPerl included).
#
# Prerequisites:
#   - Disk image with MacPerl installed (BridgeAgent: the emulator launches
#     it from the bridge dir)
#   - ROM and disk image available
#
set -euo pipefail

TIMEOUT=60
PORT=18094
SIG_PORT=18095
BACKEND="uae"
ARCH=""
BINARY="${MACEMU_BINARY:-$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix}"
GUEST_DIR="$(cd "$(dirname "$0")" && pwd)/guest"
EXTFS_DIR=""
OS_VERSION=""
ROM_OVERRIDE=""
EXTRA_FLAGS=()
DISMISS=1
NETWORK="socket"
SCRIPT_NAME=""
SECTIONS="disk extfs audio applescript network serial"
RESULTS_NAME="test_results.txt"

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port) PORT="$2"; SIG_PORT="$((PORT + 1))"; shift 2 ;;
        --disk) DISK="$2"; shift 2 ;;
        --rom) ROM_OVERRIDE="$2"; shift 2 ;;
        --backend) BACKEND="$2"; shift 2 ;;
        --core|--executor-cpu) CORE="$2"; shift 2 ;;   # CPU core: uae | mame-68k; kpx | mame-ppc
        --jit|--no-jit|--idlewait|--no-idlewait) EXTRA_FLAGS+=("$1"); shift ;;
        --executor-debug) EXTRA_FLAGS+=("$1" "$2"); shift 2 ;;   # Executor's --debug options (memcheck,...)
        --watch) EXTRA_FLAGS+=("$1" "$2"); shift 2 ;;            # write watchpoint on guest memory (Executor)
        --executor-logtraps) EXTRA_FLAGS+=("$1"); shift ;;       # Executor logs every trap call
        --executor-ppc) EXTRA_FLAGS+=("$1"); shift ;;            # Executor runs fat apps' PowerPC code
        --arch) ARCH="$2"; shift 2 ;;
        --os-version) OS_VERSION="$2"; shift 2 ;;
        --network) NETWORK="$2"; shift 2 ;;
        --no-dismiss) DISMISS=0; shift ;;
        --script) SCRIPT_NAME="$2"; shift 2 ;;
        --results) RESULTS_NAME="$2"; shift 2 ;;
        --sections) SECTIONS="$2"; shift 2 ;;
        --app) SUITE_APP="$2"; shift 2 ;;
        --image) IMAGE_NAME="$2"; shift 2 ;;
        --key-on) KEY_ON="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

# --arch was deprecated; backend determines arch. Derive ARCH from BACKEND for
# the ROM/RAM/disk selection logic only (not passed to the binary).
[[ -z "$ARCH" && "$BACKEND" == "kpx" ]] && ARCH=ppc
[[ "$ARCH" == "ppc" ]] && EXTRA_FLAGS+=(--ram 128)
# Plain "socket" joins the shared bridge at /tmp/mac-ether.sock, which
# every running MacPhoenix shares, so tests run in parallel would share one
# net-bridge (and lose it when the test that started it exits): each run
# gets its own.
ETHER_SOCK=""
if [[ "$NETWORK" == "socket" ]]; then
    ETHER_SOCK="${TMPDIR:-/tmp}/mac-ether-test-$$.sock"
    NETWORK="socket:$ETHER_SOCK"
fi
[[ -n "$NETWORK" && "$NETWORK" != "none" ]] && EXTRA_FLAGS+=(--network "$NETWORK")

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

# ROM and disk default by architecture (MacPerl runs on both 68k and PPC;
# our 68k BridgeAgent runs under PPC's built-in 68k emulator).
if [[ -n "$ROM_OVERRIDE" ]]; then
    ROM="$ROM_OVERRIDE"
elif [[ "$ARCH" == "ppc" ]]; then
    ROM="${MACEMU_ROM:-$HOME/storage/roms/g3.rom}"
else
    ROM="${MACEMU_ROM:-$HOME/roms/quadra.rom}"
fi
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [[ -n "${IMAGE_NAME:-}" && -z "${DISK:-}" ]]; then
    DISK="$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" "$IMAGE_NAME" $$)"
fi
if [[ "$BACKEND" == "executor" ]]; then
    # No ROM: Executor runs on the System Folder of the test copy of the
    # image (extracted once into <storage>/executor-systems, refreshed each
    # run) and mounts the copy beside it, writable, as the ROM backends'
    # boot disk is.
    [[ -z "${DISK:-}" ]] && DISK="${MACEMU_DISK:-$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" "tests-${OS_VERSION:-7.5.5}" $$)}"
    EXEC_SYSTEM="$(basename "$DISK")"
    ROM=""
    EXTRA_FLAGS+=(--executor-system "$EXEC_SYSTEM" --executor-writable-images)
elif [[ -z "${DISK:-}" ]]; then
    # The baked test disks (provisioning/images/tests-*.json).
    if [[ -n "$OS_VERSION" ]]; then
        case "$OS_VERSION" in 7.5.5|7.6.1|9.0.4) IMG_BASE="tests-${OS_VERSION}" ;; *) IMG_BASE="macos-${OS_VERSION}" ;; esac
    elif [[ "$ARCH" == "ppc" ]]; then
        IMG_BASE="tests-9.0.4"
    else
        IMG_BASE="tests-7.5.5"
    fi
    DISK="${MACEMU_DISK:-$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" "$IMG_BASE" $$)}"
fi

# --- Preflight checks ---

if [[ ! -x "$BINARY" ]]; then
    echo "SKIP: Binary not found: $BINARY"
    exit 77
fi

if [[ "$BACKEND" != "executor" && ! -f "$ROM" ]]; then
    echo "SKIP: ROM not found: $ROM"
    exit 77
fi

if [[ ! -f "$DISK" ]]; then
    echo "SKIP: Disk image not found: $DISK"
    exit 77
fi

SUITE_APP="${SUITE_APP:-$GUEST_DIR/suite/MacPhoenixSuite.bin}"
if [[ -n "$SCRIPT_NAME" ]]; then
    PERL_SCRIPT="$GUEST_DIR/$SCRIPT_NAME"
    [[ -f "$PERL_SCRIPT" ]] || { echo "SKIP: Perl test script not found: $PERL_SCRIPT"; exit 77; }
else
    [[ -f "$SUITE_APP" ]] || { echo "SKIP: $SUITE_APP not built (tools/mpw/mpw build tests/guest/suite -o tests/guest/suite)"; exit 77; }
fi

# --- Setup ExtFS shared folder ---

EXTFS_DIR=$(mktemp -d ${TMPDIR:-/tmp}/mactest-extfs.XXXXXX)
if [[ -n "$SCRIPT_NAME" ]]; then
    python3 "$GUEST_DIR/install_perl_test.py" "$PERL_SCRIPT" "$EXTFS_DIR" "$SCRIPT_NAME"
else
    python3 "$GUEST_DIR/install_macbinary.py" "$SUITE_APP" "$EXTFS_DIR" MacPhoenixSuite
    printf '%s\r%s\r' "$RESULTS_NAME" "$SECTIONS" > "$EXTFS_DIR/suite.cfg"
fi

# Executor's MacTCP is on host sockets, where net-bridge's echo service
# (10.0.2.1:7) does not exist: a host echo server stands in, and the guest
# script finds it in Host:net_echo.txt ("address port").
if [[ "$BACKEND" == "executor" ]]; then
    ECHO_PORTFILE="$EXTFS_DIR/.echo_port"
    python3 "$SCRIPT_DIR/lib/echo_server.py" "$ECHO_PORTFILE" &
    ECHO_PID=$!
    for i in $(seq 1 20); do [[ -s "$ECHO_PORTFILE" ]] && break; sleep 0.1; done
    printf '127.0.0.1 %s\r' "$(cat "$ECHO_PORTFILE")" > "$EXTFS_DIR/net_echo.txt"
    rm -f "$ECHO_PORTFILE"
fi

cleanup() {
    [[ -n "${ECHO_PID:-}" ]] && kill "$ECHO_PID" 2>/dev/null || true
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    if [[ -f "$EXTFS_DIR/$RESULTS_NAME" && ${EXIT_CODE:-1} -ne 0 ]]; then
        echo "--- guest results ---"
        cat "$EXTFS_DIR/$RESULTS_NAME"
    fi
    rm -rf "$EXTFS_DIR"
    if [[ -n "$ETHER_SOCK" ]]; then rm -f "$ETHER_SOCK"; fi
}
trap cleanup EXIT SIGTERM SIGINT

echo "=== Guest Test Suite ==="
[[ -n "$OS_VERSION" ]] && echo "OS: $OS_VERSION"
echo "ROM: $ROM"
echo "Disk: $DISK"
echo "ExtFS: $EXTFS_DIR"
echo "Port: $PORT"
echo "Network: ${NETWORK:-none}"

# --- Boot emulator ---

DISK_FLAGS=(--disk "$DISK")
[[ "$BACKEND" == "executor" ]] && DISK_FLAGS=()  # the System's image is mounted itself

DISMISS_FLAG=()
if [[ $DISMISS -eq 1 ]]; then
    DISMISS_FLAG=(--dismiss-shutdown-dialog)
else
    DISMISS_FLAG=(--no-dismiss-shutdown-dialog)
fi

[[ -n "${CORE:-}" ]] && EXTRA_FLAGS+=(--core "$CORE")
# --headless-http runs the ROM machine in-process, where /api/snapshot has
# no child to ask; MACTEST_SNAPSHOT runs it behind the web server instead.
if [[ -n "${MACTEST_SNAPSHOT:-}" ]]; then
    MODE_FLAGS=(--bridge)
else
    MODE_FLAGS=(--headless-http)
fi
"$BINARY" --backend "$BACKEND" --timeout "$((TIMEOUT + 10))" \
    --config /dev/null "${DISMISS_FLAG[@]}" "${MODE_FLAGS[@]}" \
    --port "$PORT" \
    "${DISK_FLAGS[@]}" --extfs "$EXTFS_DIR" \
    "${EXTRA_FLAGS[@]}" ${ROM:+"$ROM"} &>${TMPDIR:-/tmp}/mactest_guest_$$.log &
EMU_PID=$!

# Wait for HTTP server
echo -n "Waiting for server..."
for i in $(seq 1 20); do
    if curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1; then
        echo " ready"
        break
    fi
    if ! kill -0 "$EMU_PID" 2>/dev/null; then
        echo " FAIL: emulator exited early"
        tail -20 ${TMPDIR:-/tmp}/mactest_guest_$$.log
        exit 1
    fi
    echo -n "."
    sleep 0.5
done

curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null

# --- Wait for boot to desktop ---

echo -n "Booting..."
START_TIME=$(date +%s)
BOOT_TIMEOUT=$((TIMEOUT / 2 > 30 ? TIMEOUT / 2 : 30))
while true; do
    ELAPSED=$(( $(date +%s) - START_TIME ))
    if [[ $ELAPSED -ge $BOOT_TIMEOUT ]]; then
        echo ""
        echo "FAIL: Boot timeout after ${BOOT_TIMEOUT}s"
        tail -10 ${TMPDIR:-/tmp}/mactest_guest_$$.log
        exit 1
    fi

    PHASE=$(curl -sf "http://localhost:$PORT/api/status" 2>/dev/null \
        | grep -oP '"boot_phase"\s*:\s*"\K[^"]+' || echo "unknown")

    if [[ "$PHASE" == "Finder" || "$PHASE" == "desktop" ]]; then
        echo " $PHASE (${ELAPSED}s)"
        break
    fi
    echo -n "."
    sleep 1
done

sleep 2  # Finder settle

# BridgeAgent says hello once it is running (launched at desktop); the
# boot phase can say Finder before that (Executor reports it at the first
# frame), so wait for the agent before dispatching.
echo -n "Waiting for BridgeAgent..."
for i in $(seq 1 "$BOOT_TIMEOUT"); do
    if curl -sf "http://localhost:$PORT/api/status" | grep -q "\"bridge_agent_connected\": true"; then
        echo " up (${i}s)"
        break
    fi
    if [[ $i -eq $BOOT_TIMEOUT ]]; then
        echo " no BridgeAgent after ${BOOT_TIMEOUT}s (dispatching anyway)"
    fi
    sleep 1
done

# --- Dispatch the suite (or a MacPerl script) via the bridge ---

if [[ -z "$SCRIPT_NAME" ]]; then
    echo "Launching MacPhoenixSuite ($SECTIONS)..."
    LAUNCH=$(curl -sf --max-time 30 -X POST "http://localhost:$PORT/api/launch" \
        -H "Content-Type: application/json" \
        -d '{"path":"Host:MacPhoenixSuite"}' || echo '{"success":false}')
    if ! echo "$LAUNCH" | grep -q '"success": true'; then
        echo "FAIL: Could not launch MacPhoenixSuite"
        echo "  Response: $LAUNCH"
        exit 1
    fi
else
    # /api/script (generic 'misc'/'dosc'): the "Perl path" is just the
    # eval-from-file stub composed here, so BridgeAgent stays
    # language-agnostic. The stub does the \r->\n fix MacPerl needs to
    # install sub defs from a slurped Mac-text-mode file.
    PERL_STUB='open(R,"<Host:'"$SCRIPT_NAME"'")||die "open: $!";local $/;$c=<R>;close R;$c=~tr/\r/\n/;eval $c;die $@ if $@;'
    PAYLOAD=$(python3 -c '
import json, sys
print(json.dumps({"creator": "McPL", "script": sys.argv[1]}))
' "$PERL_STUB")

    echo "Dispatching $SCRIPT_NAME via MacPerl..."
    LAUNCH=$(curl -sf --max-time 15 -X POST "http://localhost:$PORT/api/script" \
        -H "Content-Type: application/json" \
        -d "$PAYLOAD" || echo '{"success":false}')

    if ! echo "$LAUNCH" | grep -q '"success": true'; then
        echo "FAIL: Could not dispatch $SCRIPT_NAME to MacPerl"
        echo "  Response: $LAUNCH"
        exit 1
    fi
fi

# --- Wait for test app to run and finish ---

echo -n "Waiting for tests..."
TEST_START=$(date +%s)
TEST_TIMEOUT=$((TIMEOUT - (TEST_START - START_TIME)))
[[ $TEST_TIMEOUT -lt 10 ]] && TEST_TIMEOUT=10

RESULTS_FILE="$EXTFS_DIR/$RESULTS_NAME"
while true; do
    ELAPSED=$(( $(date +%s) - TEST_START ))
    if [[ $ELAPSED -ge $TEST_TIMEOUT ]]; then
        echo ""
        echo "FAIL: Test execution timeout after ${TEST_TIMEOUT}s"
        exit 1
    fi

    if [[ -f "$RESULTS_FILE" ]] && grep -q '\-\-\-' "$RESULTS_FILE" 2>/dev/null; then
        echo " done (${ELAPSED}s)"
        break
    fi

    if [[ -n "${KEY_ON:-}" && -f "$RESULTS_FILE" ]] && tr '\r' '\n' < "$RESULTS_FILE" | grep -q "${KEY_ON%:*}"; then
        for down in true false; do
            curl -s --max-time 5 -X POST "http://localhost:$PORT/api/keypress" \
                -H 'Content-Type: application/json' -d "{\"key\": ${KEY_ON##*:}, \"down\": $down}" >/dev/null
            sleep 0.1
        done
        KEY_ON=""
    fi

    echo -n "."
    sleep 1
done

# --- Collect & parse results ---

# MACTEST_SNAPSHOT=NAME: dump guest RAM to <storage>/snapshots/NAME while
# the script's application is still up (tools/macdecode reads it).
if [[ -n "${MACTEST_SNAPSHOT:-}" ]]; then
    curl -sf --max-time 30 -X POST -H 'Content-Type: application/json' \
        -d "{\"name\":\"$MACTEST_SNAPSHOT\"}" "http://localhost:$PORT/api/snapshot" >/dev/null \
        && echo "Snapshot: $MACTEST_SNAPSHOT"
fi

echo ""
echo "--- Results ---"
cat "$RESULTS_FILE"
echo ""

tr '\r' '\n' < "$RESULTS_FILE" > "${RESULTS_FILE}.unix"
RESULTS_UNIX="${RESULTS_FILE}.unix"

FAIL_COUNT=$(grep -c "^FAIL " "$RESULTS_UNIX" || true)
PASS_COUNT=$(grep -c "^PASS " "$RESULTS_UNIX" || true)
SKIP_COUNT=$(grep -c "^SKIP " "$RESULTS_UNIX" || true)

echo "$PASS_COUNT passed, $FAIL_COUNT failed, $SKIP_COUNT skipped"

# --- Graceful shutdown via BridgeAgent (ShutDwnPower) ---

echo -n "Shutting down..."
SHUT_RESP=$(curl -sf --max-time 10 -X POST "http://localhost:$PORT/api/shutdown" \
    -d '{}' 2>/dev/null || echo '{"success":false}')
if echo "$SHUT_RESP" | grep -q '"success": true'; then
    for i in $(seq 1 20); do
        if ! kill -0 "$EMU_PID" 2>/dev/null; then
            echo " done (${i}s)"
            EMU_PID=""
            break
        fi
        sleep 1
    done
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        echo " timeout, falling back to kill"
    fi
else
    echo " dispatch failed: $SHUT_RESP"
fi

if [[ $FAIL_COUNT -gt 0 ]]; then
    EXIT_CODE=1
    echo "FAIL: $FAIL_COUNT test(s) failed"
    exit 1
else
    EXIT_CODE=0
    echo "PASS: All tests passed"
    exit 0
fi
