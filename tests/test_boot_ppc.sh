#!/bin/bash
#
# test_boot_ppc.sh - Verify PPC Mac OS 9 boots with KPX backend
#
# Usage:
#   tests/test_boot_ppc.sh [--timeout 45] [--port 18095] [--webserver]
#
# Modes:
#   Default (headless): boots with --no-webserver, checks log for milestones
#   --webserver:        boots with HTTP server, polls /api/status for boot phases
#
# Requires:
#   - PPC ROM (4MB G3): set MACEMU_PPC_ROM or uses ~/storage/roms/g3.rom
#   - Disk image:       set MACEMU_DISK or uses ~/storage/images/macos-7.5.5.img
#
set -euo pipefail

TIMEOUT=45
PORT=18095
SIG_PORT=18096
WEBSERVER=false
BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix"
ROM="${MACEMU_PPC_ROM:-$HOME/storage/roms/g3.rom}"
DISK="${MACEMU_DISK:-$HOME/storage/images/macos-7.5.5.img}"
MIN_CHECKLOADS=200
EXTRA_FLAGS=()

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port) PORT="$2"; SIG_PORT="$((PORT + 1))"; shift 2 ;;
        --webserver) WEBSERVER=true; shift ;;
        --ppc-jit|--no-ppc-jit) EXTRA_FLAGS+=("$1"); shift ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

if [[ ! -x "$BINARY" ]]; then
    echo "SKIP: Binary not found: $BINARY"
    exit 77
fi

# Resolve ROM through symlinks
if [[ -L "$ROM" ]]; then
    ROM="$(readlink -f "$ROM")"
fi
if [[ ! -f "$ROM" ]]; then
    echo "SKIP: PPC ROM not found: $ROM (set MACEMU_PPC_ROM)"
    exit 77
fi

if [[ ! -f "$DISK" ]]; then
    echo "SKIP: Disk image not found: $DISK (set MACEMU_DISK)"
    exit 77
fi

echo "=== PPC Boot Test: KPX backend, timeout=${TIMEOUT}s, webserver=$WEBSERVER ==="
echo "ROM: $ROM"
echo "DISK: $DISK"

# Create temp config with ISO as cdrom
TMPCONFIG=$(mktemp /tmp/macemu_ppc_config_XXXXXX.json)
cat > "$TMPCONFIG" << EOJSON
{
  "rom": "$ROM",
  "disks": ["$DISK"],
  "cdroms": [],
  "ram": 128,
  "architecture": "ppc"
}
EOJSON

LOG="/tmp/macemu_ppc_test_$$.log"

cleanup() {
    if kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    rm -f "$TMPCONFIG"
}

if [[ "$WEBSERVER" == "true" ]]; then
    # ── Webserver mode: poll /api/status for boot phases ──

    "$BINARY" --config "$TMPCONFIG" --backend kpx --arch ppc \
        --timeout "$((TIMEOUT + 5))" \
        --port "$PORT" --signaling-port "$SIG_PORT" \
        "${EXTRA_FLAGS[@]}" &>"$LOG" &
    EMU_PID=$!
    trap cleanup EXIT SIGTERM SIGINT

    # Wait for HTTP server to be ready
    echo -n "Waiting for server..."
    for i in $(seq 1 20); do
        if curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1; then
            echo " ready"
            break
        fi
        if ! kill -0 "$EMU_PID" 2>/dev/null; then
            echo " FAIL: emulator exited early"
            tail -20 "$LOG"
            exit 1
        fi
        echo -n "."
        sleep 0.5
    done

    # Start CPU
    echo "Starting CPU..."
    curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null 2>&1 || true

    # Poll boot_phase until "Finder"/"desktop" or timeout
    echo -n "Booting..."
    START_TIME=$(date +%s)
    LAST_PHASE=""
    while true; do
        ELAPSED=$(( $(date +%s) - START_TIME ))
        if [[ $ELAPSED -ge $TIMEOUT ]]; then
            echo ""
            echo "FAIL: Timeout after ${TIMEOUT}s (last phase: $LAST_PHASE)"
            echo "--- emulator log tail ---"
            tail -10 "$LOG"
            exit 1
        fi

        STATUS=$(curl -sf "http://localhost:$PORT/api/status" 2>/dev/null || echo "{}")
        PHASE=$(echo "$STATUS" | grep -oP '"boot_phase"\s*:\s*"\K[^"]+' || echo "unknown")
        CHECKLOADS=$(echo "$STATUS" | grep -oP '"checkload_count"\s*:\s*\K[0-9]+' || echo "0")

        if [[ "$PHASE" != "$LAST_PHASE" ]]; then
            echo ""
            echo -n "  [$PHASE @ ${ELAPSED}s, ${CHECKLOADS} resources]"
            LAST_PHASE="$PHASE"
        else
            echo -n "."
        fi

        if [[ "$PHASE" == "Finder" || "$PHASE" == "desktop" ]]; then
            echo ""
            echo "PASS: Reached $PHASE in ${ELAPSED}s ($CHECKLOADS resources loaded)"
            rm -f "$LOG"
            exit 0
        fi

        sleep 1
    done

else
    # ── Headless mode: check log output for milestones ──

    "$BINARY" --config "$TMPCONFIG" --backend kpx --arch ppc \
        --timeout "$TIMEOUT" --no-webserver \
        "${EXTRA_FLAGS[@]}" 2>&1 | tee "$LOG" &
    EMU_PID=$!
    trap cleanup EXIT SIGTERM SIGINT

    # Wait for emulator to finish (headless mode with --timeout)
    wait "$EMU_PID" 2>/dev/null || true

    # Count milestones (PPC boot emits [Boot +Xs] phase markers, not m68k CHECKLOADs)
    DRIVERS=$(grep -cE "Installing drivers" "$LOG" 2>/dev/null | head -1)
    FINDER=$(grep -cE "Finder launched" "$LOG" 2>/dev/null | head -1)
    DESKTOP=$(grep -cE "Desktop ready" "$LOG" 2>/dev/null | head -1)
    : "${DRIVERS:=0}" "${FINDER:=0}" "${DESKTOP:=0}"

    echo ""
    echo "Boot milestones: drivers=$DRIVERS finder=$FINDER desktop=$DESKTOP"

    if [[ "$DESKTOP" -ge 1 ]]; then
        echo "PASS: PPC boot reached Desktop (Finder idle)"
        rm -f "$LOG"
        exit 0
    elif [[ "$FINDER" -ge 1 ]]; then
        echo "PASS: PPC boot reached Finder"
        rm -f "$LOG"
        exit 0
    else
        echo "FAIL: PPC boot did not reach Finder"
        echo "--- last 20 lines ---"
        tail -20 "$LOG"
        exit 1
    fi
fi
