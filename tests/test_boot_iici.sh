#!/bin/bash
#
# test_boot_iici.sh - the Mac IIci boots System 6.0.8 to a drawn desktop
#
# Usage:
#   tests/test_boot_iici.sh [--timeout 40] [--rom PATH] [--port 18420] [--core uae|mame-68k]
#
# The IIci profile: 24-bit mode, 8 MB, 68030 + FPU (machine_profile.cpp).
# Passes when the Finder has drawn the Trash label (/api/text): reaching
# the "Finder" phase isn't enough -- with the ROM stuck in 32-bit mode the
# Finder came up, put up "Finder is older version" and relaunched forever.
#
set -euo pipefail

TIMEOUT=40
PORT=18420
CORE=""
ROM="${MACEMU_IICI_ROM:-$HOME/storage/roms/iici.rom}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BINARY="${MACEMU_BINARY:-$SCRIPT_DIR/../build/mac-phoenix}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --rom) ROM="$2"; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
        --core) CORE="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

[[ -x "$BINARY" ]] || { echo "SKIP: binary not found: $BINARY"; exit 77; }
[[ -f "$ROM" ]] || { echo "SKIP: IIci ROM not found: $ROM (set MACEMU_IICI_ROM)"; exit 77; }
DISK="${MACEMU_IICI_DISK:-$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" 6.0.8 $$)}" || exit 77
[[ -f "$DISK" ]] || { echo "SKIP: 6.0.8 disk not found: $DISK"; exit 77; }

LOG="${TMPDIR:-/tmp}/macemu_test_iici_$$.log"
echo "=== IIci boot test: ${CORE:-uae} core, timeout ${TIMEOUT}s, port $PORT ==="
"$BINARY" --backend uae ${CORE:+--core "$CORE"} --config /dev/null --ram 8 \
    --timeout "$((TIMEOUT + 10))" --port "$PORT" \
    --disk "$DISK" "$ROM" &>"$LOG" &
EMU_PID=$!
trap 'kill $EMU_PID 2>/dev/null; wait $EMU_PID 2>/dev/null; true' EXIT

API="http://localhost:$PORT/api"
curl -sf --retry 20 --retry-connrefused --retry-delay 1 -X POST "$API/emulator/start" >/dev/null \
    || { echo "FAIL: emulator didn't come up"; tail -20 "$LOG"; exit 1; }

RESULT=$(curl -s -X POST -d "{\"condition\":\"text=Trash\",\"timeout\":$TIMEOUT}" "$API/wait")
STATUS=$(curl -s "$API/status")
grep -q 'Selected: iici' "$LOG" \
    || { echo "FAIL: IIci profile not selected"; grep MachineProfile "$LOG"; exit 1; }
if [[ "$RESULT" == *'"ok":true'* ]]; then
    echo "PASS: desktop drawn ($(grep -c 'abe5 installed' "$LOG") Finder launch(es))"
    exit 0
fi
echo "FAIL: no desktop: $RESULT"
echo "status: $STATUS"
tail -15 "$LOG"
exit 1
