#!/bin/bash
#
# test_stop_restart.sh - Exercise stop/start/restart lifecycle
#
# Boots the emulator, then cycles through stop → start → verify loops.
# Tests subprocess mode (webserver) across multiple backends.
#
# Usage:
#   tests/test_stop_restart.sh [--backend uae|unicorn|kpx] [--cycles 3]
#                              [--port 18070] [--timeout 30]
#
# If --backend is omitted, runs all backends sequentially.
#
set -euo pipefail

CYCLES=3
PORT=18070
SIG_PORT=18071
TIMEOUT=30
BACKENDS=()
BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix"
ROM="${MACEMU_ROM:-$HOME/roms/quadra.rom}"
PPC_ROM="${MACEMU_PPC_ROM:-$HOME/storage/roms/g3.rom}"
DISK="${MACEMU_DISK:-$HOME/storage/images/macos-7.5.5.img}"
EXTRA_FLAGS=()
VERBOSE=false

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --backend) BACKENDS+=("$2"); shift 2 ;;
        --cycles) CYCLES="$2"; shift 2 ;;
        --port) PORT="$2"; SIG_PORT="$((PORT + 1))"; shift 2 ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --verbose) VERBOSE=true; shift ;;
        --jit|--no-jit) EXTRA_FLAGS+=("$1"); shift ;;
        --ppc-jit) EXTRA_FLAGS+=("--jit"); shift ;;
        --no-ppc-jit) EXTRA_FLAGS+=("--no-jit"); shift ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

if [[ ! -x "$BINARY" ]]; then
    echo "SKIP: Binary not found: $BINARY"
    exit 77
fi

# Default: test all available backends
if [[ ${#BACKENDS[@]} -eq 0 ]]; then
    BACKENDS=(uae-interp uae-jit unicorn-m68k unicorn-ppc kpx-interp kpx-jit)
fi

LOG="/tmp/macemu_stoprestart_$$.log"
TOTAL_PASS=0
TOTAL_FAIL=0
TOTAL_SKIP=0
RESULTS=()

cleanup_emu() {
    local pid="${EMU_PID:-}"
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
    EMU_PID=""
}
trap cleanup_emu EXIT SIGTERM SIGINT

# --- Helpers ---

api() {
    local method="$1" path="$2"
    shift 2
    curl -sf --max-time 5 -X "$method" "http://localhost:$PORT$path" "$@" 2>/dev/null
}

wait_for_server() {
    local max_wait="${1:-20}"
    for i in $(seq 1 "$max_wait"); do
        if api GET /api/status >/dev/null; then
            return 0
        fi
        if [[ -n "${EMU_PID:-}" ]] && ! kill -0 "$EMU_PID" 2>/dev/null; then
            return 1
        fi
        sleep 0.5
    done
    return 1
}

wait_for_boot() {
    local target_phase="${1:-Finder}"
    local max_wait="${2:-$TIMEOUT}"
    local start elapsed phase
    start=$(date +%s)
    while true; do
        elapsed=$(( $(date +%s) - start ))
        if [[ $elapsed -ge $max_wait ]]; then
            return 1
        fi
        phase=$(api GET /api/status | grep -oP '"boot_phase"\s*:\s*"\K[^"]+' || true)
        if [[ "$phase" == "$target_phase" || "$phase" == "desktop" ]]; then
            return 0
        fi
        sleep 1
    done
}

wait_for_stopped() {
    local max_wait="${1:-10}"
    local start elapsed running
    start=$(date +%s)
    while true; do
        elapsed=$(( $(date +%s) - start ))
        if [[ $elapsed -ge $max_wait ]]; then
            return 1
        fi
        running=$(api GET /api/status | grep -oP '"emulator_running"\s*:\s*\K(true|false)' || echo "unknown")
        if [[ "$running" == "false" ]]; then
            return 0
        fi
        sleep 0.5
    done
}

get_status_field() {
    local field="$1"
    api GET /api/status | grep -oP "\"$field\"\s*:\s*\"?\K[^\",}]+" || echo "unknown"
}

log_line() {
    echo "  $*"
    if $VERBOSE; then
        echo "  $*" >> "$LOG"
    fi
}

# --- Per-backend test ---

run_backend_test() {
    local label="$1"
    local backend="$2"
    # Arg 3 is legacy "arch" (m68k|ppc) — kept positional for caller readability;
    # the binary derives arch from backend, so we don't pass it through.
    local _arch="$3"
    local rom="$4"
    shift 4
    local extra=("$@")

    echo ""
    echo "=== $label: $CYCLES stop/restart cycles ==="

    # Check ROM
    if [[ ! -f "$rom" ]]; then
        echo "  SKIP: ROM not found: $rom"
        TOTAL_SKIP=$((TOTAL_SKIP + 1))
        RESULTS+=("SKIP $label (no ROM)")
        return
    fi

    local pass=0 fail=0

    # Start emulator
    "$BINARY" --config /dev/null --backend "$backend" \
        --timeout 600 --dismiss-shutdown-dialog \
        --port "$PORT" \
        --disk "$DISK" "${extra[@]}" "$rom" &>"$LOG.${label}" &
    EMU_PID=$!
    log_line "PID: $EMU_PID"

    # Wait for HTTP server
    if ! wait_for_server 20; then
        echo "  FAIL: HTTP server never started"
        if [[ -f "$LOG.${label}" ]]; then
            echo "  --- emulator log tail ---"
            tail -10 "$LOG.${label}"
        fi
        cleanup_emu
        fail=$((fail + 1))
        TOTAL_FAIL=$((TOTAL_FAIL + 1))
        RESULTS+=("FAIL $label (server didn't start)")
        return
    fi
    log_line "Server ready"

    # Start CPU
    local start_resp
    start_resp=$(api POST /api/emulator/start || echo '{"success":false}')
    if ! echo "$start_resp" | grep -q '"success": true\|"success":true'; then
        echo "  FAIL: Initial start failed: $start_resp"
        cleanup_emu
        fail=$((fail + 1))
        TOTAL_FAIL=$((TOTAL_FAIL + 1))
        RESULTS+=("FAIL $label (initial start)")
        return
    fi
    log_line "CPU started"

    # Wait for first boot
    if ! wait_for_boot Finder "$TIMEOUT"; then
        local phase
        phase=$(get_status_field boot_phase)
        echo "  FAIL: Initial boot didn't reach Finder (stuck at: $phase)"
        cleanup_emu
        fail=$((fail + 1))
        TOTAL_FAIL=$((TOTAL_FAIL + 1))
        RESULTS+=("FAIL $label (initial boot: $phase)")
        return
    fi
    local checkloads
    checkloads=$(get_status_field checkload_count)
    log_line "Booted ($checkloads resources)"

    # --- Stop/restart cycles ---
    for cycle in $(seq 1 "$CYCLES"); do
        echo -n "  Cycle $cycle/$CYCLES: "

        # STOP
        local stop_resp
        stop_resp=$(api POST /api/emulator/stop || echo '{"error":"request failed"}')
        if ! echo "$stop_resp" | grep -q '"success": true\|"success":true'; then
            echo "FAIL (stop request: $stop_resp)"
            fail=$((fail + 1))
            continue
        fi

        # Verify stopped — check that the HTTP server is still responsive
        # and reports not running
        sleep 1
        local server_alive=false
        if api GET /api/status >/dev/null 2>&1; then
            server_alive=true
        fi

        if ! $server_alive; then
            echo "FAIL (HTTP server died after stop)"
            fail=$((fail + 1))

            # If process died, we can't continue cycles — try to respawn
            if ! kill -0 "$EMU_PID" 2>/dev/null; then
                echo "    Process $EMU_PID died. Aborting remaining cycles."
                break
            fi
            continue
        fi

        local running
        running=$(get_status_field emulator_running)
        if [[ "$running" != "false" ]]; then
            echo "WARN (emulator_running=$running after stop, waiting...)"
            if ! wait_for_stopped 5; then
                running=$(get_status_field emulator_running)
                echo "FAIL (still running=$running after 5s)"
                fail=$((fail + 1))
                continue
            fi
        fi

        # START (triggers fresh subprocess boot)
        local start_resp2
        start_resp2=$(api POST /api/emulator/start || echo '{"error":"request failed"}')
        if ! echo "$start_resp2" | grep -q '"success": true\|"success":true'; then
            echo "FAIL (start request: $start_resp2)"
            fail=$((fail + 1))
            continue
        fi

        # Wait for re-boot
        if ! wait_for_boot Finder "$TIMEOUT"; then
            local phase2
            phase2=$(get_status_field boot_phase)
            echo "FAIL (re-boot stuck at: $phase2)"
            fail=$((fail + 1))
            continue
        fi

        local checkloads2
        checkloads2=$(get_status_field checkload_count)
        echo "PASS (stop+start, booted $checkloads2 resources)"
        pass=$((pass + 1))
    done

    # Cleanup
    cleanup_emu

    # Summary for this backend
    echo "  Result: $pass/$CYCLES passed"
    if [[ $fail -gt 0 ]]; then
        TOTAL_FAIL=$((TOTAL_FAIL + fail))
        RESULTS+=("FAIL $label ($pass/$CYCLES)")
    else
        TOTAL_PASS=$((TOTAL_PASS + 1))
        RESULTS+=("PASS $label ($pass/$CYCLES)")
    fi
}

# --- Main ---

echo "=== Stop/Restart Test Suite ==="
echo "Cycles: $CYCLES per backend"
echo "Timeout: ${TIMEOUT}s per boot"
echo ""

for backend_spec in "${BACKENDS[@]}"; do
    # Clean up any leftover emulator
    cleanup_emu
    sleep 1

    case "$backend_spec" in
        uae-interp|uae)
            run_backend_test "UAE-interp" uae m68k "$ROM" --no-jit
            ;;
        uae-jit)
            run_backend_test "UAE-JIT" uae m68k "$ROM" --jit
            ;;
        unicorn|unicorn-m68k)
            run_backend_test "Unicorn-m68k" unicorn-m68k m68k "$ROM"
            ;;
        unicorn-ppc)
            if [[ -f "$PPC_ROM" ]]; then
                run_backend_test "Unicorn-PPC" unicorn-ppc ppc "$PPC_ROM"
            else
                echo "SKIP: Unicorn-PPC (no PPC ROM)"
                TOTAL_SKIP=$((TOTAL_SKIP + 1))
                RESULTS+=("SKIP Unicorn-PPC (no PPC ROM)")
            fi
            ;;
        kpx-interp|kpx)
            if [[ -f "$PPC_ROM" ]]; then
                run_backend_test "KPX-interp" kpx ppc "$PPC_ROM" --no-jit
            else
                echo "SKIP: KPX-interp (no PPC ROM)"
                TOTAL_SKIP=$((TOTAL_SKIP + 1))
                RESULTS+=("SKIP KPX-interp (no PPC ROM)")
            fi
            ;;
        kpx-jit)
            if [[ -f "$PPC_ROM" ]]; then
                run_backend_test "KPX-JIT" kpx ppc "$PPC_ROM" --jit
            else
                echo "SKIP: KPX-JIT (no PPC ROM)"
                TOTAL_SKIP=$((TOTAL_SKIP + 1))
                RESULTS+=("SKIP KPX-JIT (no PPC ROM)")
            fi
            ;;
        *)
            echo "Unknown backend: $backend_spec"
            exit 1
            ;;
    esac
done

# --- Summary ---

echo ""
echo "========================================"
echo "Stop/Restart Test Summary"
echo "========================================"
for r in "${RESULTS[@]}"; do
    echo "  $r"
done
echo ""
echo "Backends passed: $TOTAL_PASS  Failed: $TOTAL_FAIL  Skipped: $TOTAL_SKIP"

if [[ $TOTAL_FAIL -gt 0 ]]; then
    exit 1
fi
exit 0
