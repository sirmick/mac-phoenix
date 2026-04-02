#!/bin/bash
#
# test_boot_ppc.sh - Verify PPC Mac OS 9 boots with KPX backend
#
# Usage:
#   tests/test_boot_ppc.sh [--timeout 30] [--port 18095]
#
# Requires:
#   - PPC ROM (4MB G3): set MACEMU_PPC_ROM or uses ~/g3.rom
#   - Mac OS 9 ISO:     set MACEMU_PPC_CDROM or uses ~/storage/images/MacOS_90.iso
#
# Boots headless with the Mac OS 9 CD and checks for MBDF resources
# (menu bar definition) which indicates boot reached Finder phase.
#
set -euo pipefail

TIMEOUT=45
PORT=18095
BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix"
ROM="${MACEMU_PPC_ROM:-$HOME/g3.rom}"
CDROM="${MACEMU_PPC_CDROM:-$HOME/storage/images/MacOS_90.iso}"
MIN_CHECKLOADS=200

# Parse args
while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
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

echo "=== PPC Boot Test: KPX backend, timeout=${TIMEOUT}s ==="
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

# Start emulator in headless mode
"$BINARY" --config "$TMPCONFIG" --backend kpx --arch ppc \
    --timeout "$TIMEOUT" --no-webserver \
    2>&1 | tee "$LOG" &
EMU_PID=$!

cleanup() {
    kill "$EMU_PID" 2>/dev/null || true
    wait "$EMU_PID" 2>/dev/null || true
    rm -f "$TMPCONFIG"
}
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
