#!/bin/bash
#
# test_boot_matrix.sh - Single-cell capacity check: does {backend,arch,disk,jit} boot to Finder?
#
# Boots the emulator with the given config, polls /api/status until boot_phase=="Finder",
# sustains a liveness check for a few seconds (phase still Finder, process alive),
# then captures a PNG screenshot as visual confirmation of the end state.
#
# Emits one CSV line to stdout (or appends to --csv path):
#   label,backend,arch,jit,disk,result,phase,elapsed_s,checkloads,screenshot
#
# Usage:
#   tests/test_boot_matrix.sh \
#     --label uae-m68k-nojit-755 \
#     --backend uae --arch m68k \
#     --rom  ~/roms/quadra.rom \
#     --disk ~/storage/images/macos-7.5.5.img \
#     --no-jit \
#     --timeout 60 --port 18200 \
#     --screenshot-dir /tmp/boot_matrix \
#     [--csv /tmp/boot_matrix/results.csv]
#
set -euo pipefail

LABEL=""
BACKEND="uae"
ARCH="m68k"
ROM=""
DISK=""
JIT_FLAG=""          # one of: "", "--jitexperimental", "--no-jitexperimental", "--ppc-jit", "--no-ppc-jit"
TIMEOUT=60
PORT=18200
SIG_PORT=""
SCREENSHOT_DIR="/tmp/boot_matrix"
CSV=""
SUSTAIN_S=5          # seconds to hold at Finder to confirm it isn't the flag lying

BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --label)           LABEL="$2"; shift 2 ;;
        --backend)         BACKEND="$2"; shift 2 ;;
        --arch)            ARCH="$2"; shift 2 ;;
        --rom)             ROM="$2"; shift 2 ;;
        --disk)            DISK="$2"; shift 2 ;;
        --jit)             JIT_FLAG="--jitexperimental"; shift ;;
        --no-jit)          JIT_FLAG="--no-jitexperimental"; shift ;;
        --ppc-jit)         JIT_FLAG="--ppc-jit"; shift ;;
        --no-ppc-jit)      JIT_FLAG="--no-ppc-jit"; shift ;;
        --timeout)         TIMEOUT="$2"; shift 2 ;;
        --port)            PORT="$2"; shift 2 ;;
        --screenshot-dir)  SCREENSHOT_DIR="$2"; shift 2 ;;
        --csv)             CSV="$2"; shift 2 ;;
        --sustain)         SUSTAIN_S="$2"; shift 2 ;;
        *) echo "Unknown arg: $1" >&2; exit 2 ;;
    esac
done

if [[ -z "$LABEL" ]]; then
    LABEL="${BACKEND}-${ARCH}"
fi
SIG_PORT="$((PORT + 1))"

mkdir -p "$SCREENSHOT_DIR"
LOG="$SCREENSHOT_DIR/${LABEL}.log"
SHOT="$SCREENSHOT_DIR/${LABEL}.png"
rm -f "$LOG" "$SHOT"

# JIT column value for the CSV (human-readable)
JIT_COL="-"
case "$JIT_FLAG" in
    --jitexperimental) JIT_COL="jit" ;;
    --no-jitexperimental) JIT_COL="interp" ;;
    --ppc-jit) JIT_COL="jit" ;;
    --no-ppc-jit) JIT_COL="interp" ;;
    "") JIT_COL="-" ;;
esac

DISK_TAG="$(basename "$DISK" | sed 's/\.img$//')"

emit_csv() {
    local result="$1" phase="$2" elapsed="$3" checkloads="$4" shot_path="$5"
    local line="$LABEL,$BACKEND,$ARCH,$JIT_COL,$DISK_TAG,$result,$phase,$elapsed,$checkloads,$shot_path"
    echo "$line"
    if [[ -n "$CSV" ]]; then
        echo "$line" >> "$CSV"
    fi
}

# Capture screenshot to $SHOT. Retries for up to $1 seconds (default 15) because the
# WebRTC video encoder is async from boot progress — /api/screenshot returns 503 until
# the first frame is produced, which lags on slow backends (Unicorn especially).
# Also useful on TIMEOUT/LIES to capture the visual state at failure.
# Echoes the shot path on success, empty string on failure.
capture_screenshot() {
    local wait_s="${1:-15}"
    local code="000"
    for i in $(seq 1 "$wait_s"); do
        code=$(curl -sS -o "$SHOT" -w '%{http_code}' "http://localhost:$PORT/api/screenshot" 2>/dev/null || echo "000")
        if [[ "$code" == "200" && -s "$SHOT" ]]; then
            echo "$SHOT"
            return 0
        fi
        sleep 1
    done
    rm -f "$SHOT"
    return 1
}

# Pre-flight
if [[ ! -x "$BINARY" ]]; then
    echo "SKIP: Binary not found: $BINARY" >&2
    emit_csv "SKIP" "no-binary" 0 0 ""
    exit 77
fi
if [[ ! -f "$ROM" ]]; then
    echo "SKIP: ROM not found: $ROM" >&2
    emit_csv "SKIP" "no-rom" 0 0 ""
    exit 77
fi
if [[ ! -f "$DISK" ]]; then
    echo "SKIP: Disk not found: $DISK" >&2
    emit_csv "SKIP" "no-disk" 0 0 ""
    exit 77
fi

RAM=32
if [[ "$ARCH" == "ppc" ]]; then RAM=128; fi

echo "=== [$LABEL] backend=$BACKEND arch=$ARCH jit=$JIT_COL disk=$DISK_TAG timeout=${TIMEOUT}s port=$PORT ==="

# --config /dev/null: skip the user's persisted ~/.config/mac-phoenix/config.json so
# CDROMs/extfs from daily use don't bleed in and break boot (e.g. .AppleCD install stall).
# Defaults: no cdrom, network=none (reinforced below), one --disk, audio threads unavoidable.
EMU_ARGS=(
    --config /dev/null
    --backend "$BACKEND"
    --arch "$ARCH"
    --ram "$RAM"
    --disk "$DISK"
    --network none
    --bridge
    --timeout "$((TIMEOUT + 10))"
    --dismiss-shutdown-dialog
    --port "$PORT"
    --signaling-port "$SIG_PORT"
)
if [[ -n "$JIT_FLAG" ]]; then
    EMU_ARGS+=("$JIT_FLAG")
fi
EMU_ARGS+=("$ROM")

"$BINARY" "${EMU_ARGS[@]}" &>"$LOG" &
EMU_PID=$!

cleanup() {
    if kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT SIGTERM SIGINT

# Wait for HTTP server
for i in $(seq 1 40); do
    if curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1; then
        break
    fi
    if ! kill -0 "$EMU_PID" 2>/dev/null; then
        echo "FAIL: emulator exited before HTTP ready"
        tail -20 "$LOG" || true
        emit_csv "FAIL" "no-http" 0 0 ""
        exit 1
    fi
    sleep 0.5
done

# Start CPU
curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null 2>&1 || true

# Poll for Finder
START_TIME=$(date +%s)
LAST_PHASE=""
PHASE="unknown"
CHECKLOADS=0
REACHED=0

while true; do
    ELAPSED=$(( $(date +%s) - START_TIME ))
    if [[ $ELAPSED -ge $TIMEOUT ]]; then
        echo "TIMEOUT at ${ELAPSED}s, last phase=$LAST_PHASE — capturing failure shot"
        SHOT_PATH="$(capture_screenshot 5 || true)"
        emit_csv "TIMEOUT" "$LAST_PHASE" "$ELAPSED" "$CHECKLOADS" "$SHOT_PATH"
        exit 1
    fi

    if ! kill -0 "$EMU_PID" 2>/dev/null; then
        echo "CRASH: emulator exited at ${ELAPSED}s, phase=$LAST_PHASE"
        tail -20 "$LOG" || true
        emit_csv "CRASH" "$LAST_PHASE" "$ELAPSED" "$CHECKLOADS" ""
        exit 1
    fi

    STATUS=$(curl -sf "http://localhost:$PORT/api/status" 2>/dev/null || echo "{}")
    PHASE=$(echo "$STATUS" | grep -oP '"boot_phase"\s*:\s*"\K[^"]+' || echo "unknown")
    CHECKLOADS=$(echo "$STATUS" | grep -oP '"checkload_count"\s*:\s*\K[0-9]+' || echo "0")

    if [[ "$PHASE" != "$LAST_PHASE" ]]; then
        echo "  [$PHASE @ ${ELAPSED}s, $CHECKLOADS resources]"
        LAST_PHASE="$PHASE"
    fi

    if [[ "$PHASE" == "Finder" || "$PHASE" == "desktop" ]]; then
        REACHED=1
        break
    fi

    sleep 1
done

REACH_ELAPSED=$ELAPSED

# Sustain check: phase must stay at Finder/desktop for SUSTAIN_S seconds and process must survive
echo "  sustaining ${SUSTAIN_S}s at $PHASE..."
for i in $(seq 1 "$SUSTAIN_S"); do
    sleep 1
    if ! kill -0 "$EMU_PID" 2>/dev/null; then
        echo "LIES: emulator crashed during sustain"
        tail -20 "$LOG" || true
        emit_csv "LIES" "$PHASE" "$REACH_ELAPSED" "$CHECKLOADS" ""
        exit 1
    fi
    STATUS=$(curl -sf "http://localhost:$PORT/api/status" 2>/dev/null || echo "{}")
    NEW_PHASE=$(echo "$STATUS" | grep -oP '"boot_phase"\s*:\s*"\K[^"]+' || echo "unknown")
    if [[ "$NEW_PHASE" != "Finder" && "$NEW_PHASE" != "desktop" ]]; then
        echo "LIES: phase regressed to $NEW_PHASE — capturing failure shot"
        SHOT_PATH="$(capture_screenshot 5 || true)"
        emit_csv "LIES" "$NEW_PHASE" "$REACH_ELAPSED" "$CHECKLOADS" "$SHOT_PATH"
        exit 1
    fi
done

SHOT_PATH="$(capture_screenshot 15 || true)"
if [[ -z "$SHOT_PATH" ]]; then
    echo "PASS (phase) but screenshot failed after 15s"
    emit_csv "NOSHOT" "$PHASE" "$REACH_ELAPSED" "$CHECKLOADS" ""
    exit 0
fi

echo "PASS: $PHASE in ${REACH_ELAPSED}s, $CHECKLOADS resources, shot=$SHOT_PATH"
emit_csv "PASS" "$PHASE" "$REACH_ELAPSED" "$CHECKLOADS" "$SHOT_PATH"
exit 0
