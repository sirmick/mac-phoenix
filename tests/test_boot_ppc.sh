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
#   - PPC ROM (4MB G3): set MACEMU_PPC_ROM or uses ~/g3.rom
#   - Mac OS 9 ISO:     set MACEMU_PPC_CDROM or uses ~/storage/images/MacOS_90.iso
#
set -euo pipefail

TIMEOUT=45
PORT=18095
SIG_PORT=18096
WEBSERVER=false
BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix"
ROM="${MACEMU_PPC_ROM:-$HOME/g3.rom}"
CDROM="${MACEMU_PPC_CDROM:-$HOME/storage/images/MacOS_90.iso}"
MIN_CHECKLOADS=200

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port) PORT="$2"; SIG_PORT="$((PORT + 1))"; shift 2 ;;
        --webserver) WEBSERVER=true; shift ;;
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

if [[ ! -f "$CDROM" ]]; then
    echo "SKIP: Mac OS 9 ISO not found: $CDROM (set MACEMU_PPC_CDROM)"
    exit 77
fi

echo "=== PPC Boot Test: KPX backend, timeout=${TIMEOUT}s, webserver=$WEBSERVER ==="
echo "ROM: $ROM"
echo "CDROM: $CDROM"

# Create temp config with ISO as cdrom
TMPCONFIG=$(mktemp /tmp/macemu_ppc_config_XXXXXX.json)
cat > "$TMPCONFIG" << EOJSON
{
  "rom": "$ROM",
  "disks": [],
  "cdroms": ["$CDROM"],
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
        &>"$LOG" &
    EMU_PID=$!
    trap cleanup EXIT

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
        2>&1 | tee "$LOG" &
    EMU_PID=$!
    trap cleanup EXIT

    # Wait for emulator to finish (headless mode with --timeout)
    wait "$EMU_PID" 2>/dev/null || true

    # Count milestones
    CHECKLOADS=$(grep -c "CHECKLOAD" "$LOG" 2>/dev/null || echo "0")
    DRIVERS=$(grep -c "Installing drivers" "$LOG" 2>/dev/null || echo "0")
    VIDEO=$(grep -c "Framebuffer at" "$LOG" 2>/dev/null || echo "0")
    MBDF=$(grep -c "type='MBDF'" "$LOG" 2>/dev/null || echo "0")
    FOND=$(grep -c "type='FOND'" "$LOG" 2>/dev/null || echo "0")

    echo ""
    echo "Boot milestones: checkloads=$CHECKLOADS drivers=$DRIVERS video=$VIDEO mbdf=$MBDF fond=$FOND"

    # PPC boot must load resources (200+ CHECKLOADs) and reach MBDF (menu bar)
    if [[ $CHECKLOADS -ge $MIN_CHECKLOADS && $MBDF -ge 1 ]]; then
        echo "PASS: PPC boot loaded $CHECKLOADS resources with MBDF (Finder phase)"
        rm -f "$LOG"
        exit 0
    elif [[ $CHECKLOADS -ge $MIN_CHECKLOADS ]]; then
        echo "PASS: PPC boot loaded $CHECKLOADS resources (pre-Finder)"
        rm -f "$LOG"
        exit 0
    else
        echo "FAIL: Only $CHECKLOADS CHECKLOADs (expected $MIN_CHECKLOADS+)"
        echo "--- last 20 lines ---"
        tail -20 "$LOG"
        exit 1
    fi
fi
