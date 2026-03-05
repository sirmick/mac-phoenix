#!/bin/bash
# Boot-to-dialog headless test: boots UAE emulator with screenshot driver,
# captures PPM screenshots, hard kills, reboots, captures "not shut down
# cleanly" dialog.
#
# Usage: ./tests/e2e/boot-to-dialog-headless.sh [uae|unicorn]

set -euo pipefail

BACKEND="${1:-uae}"
BUILD_DIR="$(cd "$(dirname "$0")/../../build" && pwd)"
BINARY="$BUILD_DIR/macemu-next"
ROM="/home/mick/quadra.rom"
RESULT_DIR="/tmp/macemu_boot_test_${BACKEND}"
BOOT_TIMEOUT="${BOOT_TIMEOUT:-120}"   # seconds to wait for boot
DIALOG_TIMEOUT="${DIALOG_TIMEOUT:-60}"  # seconds to wait for dialog on 2nd boot

mkdir -p "$RESULT_DIR"
rm -f /tmp/macemu_screen_*.ppm

echo "=== Boot-to-dialog headless test: $BACKEND backend ==="
echo "Binary: $BINARY"
echo "ROM: $ROM"
echo "Results: $RESULT_DIR"

if [ ! -f "$BINARY" ]; then
    echo "ERROR: Binary not found: $BINARY" >&2
    exit 1
fi
if [ ! -f "$ROM" ]; then
    echo "ERROR: ROM not found: $ROM" >&2
    exit 1
fi

convert_screenshots() {
    local pass="$1"
    local dir="$RESULT_DIR/$pass"
    mkdir -p "$dir"
    for ppm in /tmp/macemu_screen_*.ppm; do
        [ -f "$ppm" ] || continue
        local base=$(basename "$ppm" .ppm)
        ffmpeg -y -i "$ppm" "$dir/${base}.png" 2>/dev/null
    done
    local count=$(ls "$dir"/*.png 2>/dev/null | wc -l)
    echo "  Saved $count screenshots to $dir/"
    rm -f /tmp/macemu_screen_*.ppm
}

wait_for_checkloads() {
    local logfile="$1"
    local target="$2"
    local timeout="$3"
    local start=$(date +%s)

    while true; do
        local now=$(date +%s)
        local elapsed=$((now - start))
        if [ "$elapsed" -ge "$timeout" ]; then
            local count=$(grep -c "CHECKLOAD" "$logfile" 2>/dev/null || echo "0")
            echo "  Timeout after ${timeout}s ($count CHECKLOADs)"
            return 1
        fi

        local count=$(grep -c "CHECKLOAD" "$logfile" 2>/dev/null || echo "0")
        if [ "$count" -ge "$target" ]; then
            echo "  Reached $count CHECKLOADs in ${elapsed}s"
            return 0
        fi
        sleep 2
    done
}

# ---- PASS 1: Boot to Finder, then hard kill ----
echo ""
echo "--- Pass 1: Boot to Finder (dirty shutdown) ---"
rm -f /tmp/macemu_screen_*.ppm

CPU_BACKEND="$BACKEND" MACEMU_SCREENSHOTS=1 EMULATOR_TIMEOUT=$BOOT_TIMEOUT \
    "$BINARY" --no-webserver "$ROM" > "$RESULT_DIR/pass1.log" 2>&1 &
EMUPID=$!
echo "  PID: $EMUPID"

# Wait for boot (4000+ CHECKLOADs = past splash screen, into Finder)
CHECKLOAD_TARGET="${CHECKLOAD_TARGET:-4000}"
if wait_for_checkloads "$RESULT_DIR/pass1.log" "$CHECKLOAD_TARGET" "$BOOT_TIMEOUT"; then
    echo "  Boot successful! Waiting 5s for screenshots..."
    sleep 5
else
    echo "  WARNING: Boot may not have completed"
fi

convert_screenshots "pass1"

echo "  Hard killing PID $EMUPID..."
kill -9 "$EMUPID" 2>/dev/null || true
wait "$EMUPID" 2>/dev/null || true
sleep 2

# ---- PASS 2: Reboot - should show "not shut down cleanly" dialog ----
echo ""
echo "--- Pass 2: Reboot (expect 'not shut down cleanly' dialog) ---"
rm -f /tmp/macemu_screen_*.ppm

CPU_BACKEND="$BACKEND" MACEMU_SCREENSHOTS=1 EMULATOR_TIMEOUT=$DIALOG_TIMEOUT \
    "$BINARY" --no-webserver "$ROM" > "$RESULT_DIR/pass2.log" 2>&1 &
EMUPID=$!
echo "  PID: $EMUPID"

# Wait for boot (same target — dialog appears before or at Finder)
if wait_for_checkloads "$RESULT_DIR/pass2.log" "$CHECKLOAD_TARGET" "$DIALOG_TIMEOUT"; then
    echo "  Boot reached dialog checkpoint. Waiting 5s for screenshots..."
    sleep 5
else
    echo "  WARNING: Boot may not have reached dialog"
fi

convert_screenshots "pass2"

echo "  Stopping PID $EMUPID..."
kill "$EMUPID" 2>/dev/null || true
wait "$EMUPID" 2>/dev/null || true

# ---- Summary ----
echo ""
echo "=== Results ==="
echo "Pass 1 (boot to Finder):"
echo "  CHECKLOADs: $(grep -c 'CHECKLOAD' "$RESULT_DIR/pass1.log" 2>/dev/null || echo 0)"
echo "  Screenshots: $(ls "$RESULT_DIR/pass1/"*.png 2>/dev/null | wc -l)"
echo "  Log: $RESULT_DIR/pass1.log"
echo ""
echo "Pass 2 (dirty boot - dialog):"
echo "  CHECKLOADs: $(grep -c 'CHECKLOAD' "$RESULT_DIR/pass2.log" 2>/dev/null || echo 0)"
echo "  Screenshots: $(ls "$RESULT_DIR/pass2/"*.png 2>/dev/null | wc -l)"
echo "  Log: $RESULT_DIR/pass2.log"
echo ""
echo "All screenshots: $RESULT_DIR/"
echo "Done."
