#!/bin/bash
#
# test_resource_cleanup.sh - Verify emulator cleans up resources on exit
#
# Checks that no SHM files, Unix sockets, or orphan processes are left
# behind after the emulator exits (both graceful and hard-kill scenarios).
#
# Usage:
#   tests/test_resource_cleanup.sh [--port 18074] [--timeout 15]
#
set -euo pipefail

PORT=18074
SIG_PORT=18075
TIMEOUT=15
BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix"
ROM="${MACEMU_ROM:-$HOME/roms/quadra.rom}"
DISK="${MACEMU_DISK:-$HOME/storage/images/macos-7.5.5.img}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) PORT="$2"; SIG_PORT="$((PORT + 1))"; shift 2 ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

if [[ ! -x "$BINARY" ]]; then
    echo "SKIP: Binary not found: $BINARY"
    exit 77
fi

if [[ ! -f "$ROM" ]]; then
    echo "SKIP: ROM not found: $ROM"
    exit 77
fi

PASS=0
FAIL=0
TESTS=0

check() {
    local name="$1" result="$2"
    TESTS=$((TESTS + 1))
    if [[ "$result" == "pass" ]]; then
        echo "  PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $name"
        FAIL=$((FAIL + 1))
    fi
}

cleanup_emu() {
    local pid="${1:-}"
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
}

# Snapshot existing resources before test (so we only check what we create)
shm_before=$(ls /dev/shm/macemu-video-* 2>/dev/null | sort || true)
sock_before=$(ls /tmp/macemu-*.sock 2>/dev/null | sort || true)
bridge_before=$(pgrep -f "net-bridge" 2>/dev/null | sort || true)

wait_for_server() {
    for i in $(seq 1 20); do
        if curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.5
    done
    return 1
}

# ============================================================
echo "=== Test 1: Graceful exit (--timeout) cleans up ==="
# ============================================================

setsid "$BINARY" --config /dev/null --backend uae --timeout 3 \
    --port "$PORT" --signaling-port "$SIG_PORT" \
    --disk "$DISK" --dismiss-shutdown-dialog \
    "$ROM" &>/tmp/cleanup_test_$$.log &
EMU_PID=$!

# Wait for server to start
if ! wait_for_server; then
    echo "  FAIL: Server didn't start"
    cleanup_emu "$EMU_PID"
    FAIL=$((FAIL + 1))
else
    # Start CPU so subprocess spawns
    curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null 2>&1 || true
    sleep 1

    # Find child subprocess PID (if any)
    CHILD_PID=$(pgrep -P "$EMU_PID" -f "mac-phoenix.*--ipc" 2>/dev/null | head -1 || true)

    # Wait for timeout exit + child cleanup via PR_SET_PDEATHSIG
    wait "$EMU_PID" 2>/dev/null || true
    sleep 3

    # Check: parent process exited
    if kill -0 "$EMU_PID" 2>/dev/null; then
        check "parent process exited" "fail"
        cleanup_emu "$EMU_PID"
    else
        check "parent process exited" "pass"
    fi

    # Check: child subprocess exited
    if [[ -n "$CHILD_PID" ]]; then
        if kill -0 "$CHILD_PID" 2>/dev/null; then
            check "child subprocess exited" "fail"
            kill -9 "$CHILD_PID" 2>/dev/null || true
        else
            check "child subprocess exited" "pass"
        fi

        # Check: child SHM cleaned up
        if [[ -e "/dev/shm/macemu-video-$CHILD_PID" ]]; then
            check "SHM file cleaned up (/dev/shm/macemu-video-$CHILD_PID)" "fail"
        else
            check "SHM file cleaned up" "pass"
        fi

        # Check: child socket cleaned up
        if [[ -e "/tmp/macemu-$CHILD_PID.sock" ]]; then
            check "socket cleaned up (/tmp/macemu-$CHILD_PID.sock)" "fail"
        else
            check "socket cleaned up" "pass"
        fi
    else
        echo "  (no child subprocess detected — skipping child checks)"
    fi
fi
EMU_PID=""

sleep 2

# ============================================================
echo ""
echo "=== Test 2: SIGTERM cleans up ==="
# ============================================================

setsid "$BINARY" --config /dev/null --backend uae --timeout 600 \
    --port "$PORT" --signaling-port "$SIG_PORT" \
    --disk "$DISK" --dismiss-shutdown-dialog \
    "$ROM" &>/tmp/cleanup_test_sigterm_$$.log &
EMU_PID=$!

if ! wait_for_server; then
    echo "  FAIL: Server didn't start"
    cleanup_emu "$EMU_PID"
    FAIL=$((FAIL + 1))
else
    curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null 2>&1 || true
    sleep 2

    CHILD_PID=$(pgrep -P "$EMU_PID" -f "mac-phoenix.*--ipc" 2>/dev/null | head -1 || true)

    # Send SIGTERM to parent only (setsid prevents group signal)
    kill -TERM "$EMU_PID" 2>/dev/null || true
    sleep 3

    # Check: parent process exited
    if kill -0 "$EMU_PID" 2>/dev/null; then
        check "parent exited after SIGTERM" "fail"
        kill -9 "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    else
        wait "$EMU_PID" 2>/dev/null || true
        check "parent exited after SIGTERM" "pass"
    fi

    # Check: child subprocess exited
    if [[ -n "$CHILD_PID" ]]; then
        if kill -0 "$CHILD_PID" 2>/dev/null; then
            check "child exited after SIGTERM" "fail"
            kill -9 "$CHILD_PID" 2>/dev/null || true
        else
            check "child exited after SIGTERM" "pass"
        fi

        if [[ -e "/dev/shm/macemu-video-$CHILD_PID" ]]; then
            check "SHM cleaned after SIGTERM" "fail"
        else
            check "SHM cleaned after SIGTERM" "pass"
        fi

        if [[ -e "/tmp/macemu-$CHILD_PID.sock" ]]; then
            check "socket cleaned after SIGTERM" "fail"
        else
            check "socket cleaned after SIGTERM" "pass"
        fi
    fi
fi
EMU_PID=""

sleep 2

# ============================================================
echo ""
echo "=== Test 3: SIGKILL leaves no orphan subprocess ==="
# ============================================================

setsid "$BINARY" --config /dev/null --backend uae --timeout 600 \
    --port "$PORT" --signaling-port "$SIG_PORT" \
    --disk "$DISK" --dismiss-shutdown-dialog \
    "$ROM" &>/tmp/cleanup_test_sigkill_$$.log &
EMU_PID=$!

if ! wait_for_server; then
    echo "  FAIL: Server didn't start"
    cleanup_emu "$EMU_PID"
    FAIL=$((FAIL + 1))
else
    curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null 2>&1 || true
    sleep 2

    CHILD_PID=$(pgrep -P "$EMU_PID" -f "mac-phoenix.*--ipc" 2>/dev/null | head -1 || true)

    # Hard kill parent (simulates crash)
    kill -9 "$EMU_PID" 2>/dev/null || true
    wait "$EMU_PID" 2>/dev/null || true

    # Check: child should exit via PR_SET_PDEATHSIG
    if [[ -n "$CHILD_PID" ]]; then
        # Give child time to receive SIGTERM and run atexit cleanup
        sleep 5
        if kill -0 "$CHILD_PID" 2>/dev/null; then
            check "child exits when parent is killed" "fail"
            echo "    orphan child still running: PID $CHILD_PID"
            kill -9 "$CHILD_PID" 2>/dev/null || true
        else
            check "child exits when parent is killed" "pass"
        fi

        # SHM/socket may linger after SIGKILL — that's expected.
        # But check if they do and report.
        if [[ -e "/dev/shm/macemu-video-$CHILD_PID" ]]; then
            check "SHM cleaned after parent SIGKILL" "fail"
            rm -f "/dev/shm/macemu-video-$CHILD_PID"
        else
            check "SHM cleaned after parent SIGKILL" "pass"
        fi

        if [[ -e "/tmp/macemu-$CHILD_PID.sock" ]]; then
            check "socket cleaned after parent SIGKILL" "fail"
            rm -f "/tmp/macemu-$CHILD_PID.sock"
        else
            check "socket cleaned after parent SIGKILL" "pass"
        fi
    fi
fi
EMU_PID=""

sleep 2

# ============================================================
echo ""
echo "=== Test 4: Stop/start cycle doesn't leak ==="
# ============================================================

setsid "$BINARY" --config /dev/null --backend uae --timeout 600 \
    --port "$PORT" --signaling-port "$SIG_PORT" \
    --disk "$DISK" --dismiss-shutdown-dialog \
    "$ROM" &>/tmp/cleanup_test_cycle_$$.log &
EMU_PID=$!

if ! wait_for_server; then
    echo "  FAIL: Server didn't start"
    cleanup_emu "$EMU_PID"
    FAIL=$((FAIL + 1))
else
    LEAKED_PIDS=()
    CYCLES=3

    for cycle in $(seq 1 "$CYCLES"); do
        # Start
        curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null 2>&1 || true
        sleep 2
        CHILD_PID=$(pgrep -P "$EMU_PID" -f "mac-phoenix.*--ipc" 2>/dev/null | head -1 || true)
        [[ -n "$CHILD_PID" ]] && LEAKED_PIDS+=("$CHILD_PID")

        # Stop
        curl -sf -X POST "http://localhost:$PORT/api/emulator/stop" >/dev/null 2>&1 || true
        sleep 2
    done

    # Check: no child processes left
    orphans=0
    for pid in "${LEAKED_PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            orphans=$((orphans + 1))
        fi
    done
    if [[ $orphans -eq 0 ]]; then
        check "no orphan children after $CYCLES stop/start cycles" "pass"
    else
        check "no orphan children after $CYCLES stop/start cycles ($orphans orphans)" "fail"
    fi

    # Check: no stale SHM from cycle children
    stale_shm=0
    for pid in "${LEAKED_PIDS[@]}"; do
        if [[ -e "/dev/shm/macemu-video-$pid" ]]; then
            stale_shm=$((stale_shm + 1))
        fi
    done
    if [[ $stale_shm -eq 0 ]]; then
        check "no stale SHM after $CYCLES cycles" "pass"
    else
        check "no stale SHM after $CYCLES cycles ($stale_shm leaked)" "fail"
    fi

    # Check: no stale sockets from cycle children
    stale_sock=0
    for pid in "${LEAKED_PIDS[@]}"; do
        if [[ -e "/tmp/macemu-$pid.sock" ]]; then
            stale_sock=$((stale_sock + 1))
        fi
    done
    if [[ $stale_sock -eq 0 ]]; then
        check "no stale sockets after $CYCLES cycles" "pass"
    else
        check "no stale sockets after $CYCLES cycles ($stale_sock leaked)" "fail"
    fi

    cleanup_emu "$EMU_PID"
fi
EMU_PID=""

sleep 2

# ============================================================
echo ""
echo "=== Test 5: net-bridge cleanup ==="
# ============================================================

# Only test if network=socket is functional
setsid "$BINARY" --config /dev/null --backend uae --timeout 3 \
    --network socket --port "$PORT" --signaling-port "$SIG_PORT" \
    --disk "$DISK" --dismiss-shutdown-dialog \
    "$ROM" &>/tmp/cleanup_test_bridge_$$.log &
EMU_PID=$!

if ! wait_for_server; then
    echo "  (server didn't start — network test skipped)"
else
    curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null 2>&1 || true
    sleep 2

    BRIDGE_PID=$(pgrep -f "net-bridge" 2>/dev/null | grep -v -F "$bridge_before" | head -1 || true)

    # Wait for timeout exit
    wait "$EMU_PID" 2>/dev/null || true
    sleep 2

    if [[ -n "$BRIDGE_PID" ]]; then
        if kill -0 "$BRIDGE_PID" 2>/dev/null; then
            check "net-bridge exited after emulator exit" "fail"
            echo "    orphan net-bridge: PID $BRIDGE_PID"
            kill "$BRIDGE_PID" 2>/dev/null || true
        else
            check "net-bridge exited after emulator exit" "pass"
        fi
    else
        echo "  (no net-bridge detected — skipping)"
    fi

    if [[ -e "/tmp/mac-ether.sock" ]]; then
        # Socket may be from a pre-existing bridge — only fail if we saw our bridge
        if [[ -n "$BRIDGE_PID" ]]; then
            check "net-bridge socket cleaned up" "fail"
        fi
    else
        if [[ -n "$BRIDGE_PID" ]]; then
            check "net-bridge socket cleaned up" "pass"
        fi
    fi
fi
EMU_PID=""

# ============================================================
echo ""
echo "========================================"
echo "Resource Cleanup Summary"
echo "========================================"
echo "  $PASS/$TESTS passed, $FAIL failed"

if [[ $FAIL -gt 0 ]]; then
    exit 1
fi
exit 0
