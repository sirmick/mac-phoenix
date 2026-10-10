#!/bin/bash
# boot_screenshot.sh - boot a ROM machine on a given 68k core and save a
# screenshot once the desktop is up (or the timeout passes).
#
# Usage: tests/boot_screenshot.sh --core NAME --out FILE.png [--se] [--port N] [--timeout S] [--log FILE]
#   --se  the Mac SE (System 6.0.8 disk, 4 MB, 512x342) instead of the Quadra
#   --ppc the Power Mac G3 (kpx backend; --core kpx | mame-ppc)
set -euo pipefail
CORE=uae; OUT=/tmp/boot.png; PORT=18116; TIMEOUT=40; LOG=/tmp/boot_screenshot.log; SE=0; PPC=0; EXTRA=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --core) CORE="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --log) LOG="$2"; shift 2 ;;
        --se) SE=1; shift ;;
        --ppc) PPC=1; shift ;;
        --jit|--no-jit) EXTRA+=("$1"); shift ;;
        *) echo "unknown arg $1"; exit 1 ;;
    esac
done
HERE="$(cd "$(dirname "$0")" && pwd)"
BACKEND=(--backend uae)
if [[ $PPC == 1 ]]; then
    ROM="${MACEMU_ROM:-$HOME/storage/roms/g3.rom}"
    DISK=$(bash "$HERE/lib/refresh_test_disk.sh" macos-7.5.5)
    MACHINE=(--ram 128)
    BACKEND=(--backend kpx)
elif [[ $SE == 1 ]]; then
    ROM="${MACEMU_SE_ROM:-$HOME/storage/roms/256KB ROMs/1987-03 - B2E362A8 - Mac SE.ROM}"
    DISK=$(bash "$HERE/lib/refresh_test_disk.sh" system-6.0.8)
    MACHINE=(--ram 4 --screen 512x342)
else
    ROM="${MACEMU_ROM:-$HOME/roms/quadra.rom}"
    DISK=$(bash "$HERE/lib/refresh_test_disk.sh" macos-7.5.5)
    MACHINE=()
fi
"$HERE/../build/mac-phoenix" "${BACKEND[@]}" --port "$PORT" --core "$CORE" "${MACHINE[@]}" "${EXTRA[@]}" --disk "$DISK" --timeout "$TIMEOUT" --rom "$ROM" > "$LOG" 2>&1 &
PID=$!
for i in $(seq 1 100); do
    if curl -s "localhost:$PORT/api/status" > /dev/null 2>&1; then break; fi
    sleep 0.2
done
START=$(date +%s.%N)
curl -s -X POST "localhost:$PORT/api/emulator/start" > /dev/null
PHASE=""
for i in $(seq 1 $((TIMEOUT * 4))); do
    S=$(curl -s "localhost:$PORT/api/status" || true)
    if echo "$S" | grep -q '"boot_phase": *"desktop"'; then PHASE=desktop; break; fi
    sleep 0.25
done
END=$(date +%s.%N)
echo "phase=${PHASE:-timeout} elapsed=$(echo "$END - $START" | bc)s status=$S"
sleep 2
curl -s "localhost:$PORT/api/screenshot" -o "$OUT"
ls -la "$OUT"
kill "$PID" 2>/dev/null || true
wait "$PID" 2>/dev/null || true
grep -m5 "\[CPU\]\|core" "$LOG" || true
