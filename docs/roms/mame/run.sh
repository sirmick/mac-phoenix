#!/bin/bash
# Unified runner for MAME reference emulation of classic Macs.
#
# Usage:
#   ./run.sh <machine> [extra-mame-args...]
#
# Machines:
#   macse     - Mac SE (mac128 binary)
#   macsefd   - Mac SE FDHD (mac128 binary)
#   macii     - Mac II (macii binary)
#   maciix    - Mac IIx (macii binary)
#   maciicx   - Mac IIcx (macii binary)
#   macse30   - Mac SE/30 (macii binary)
#   maciici   - Mac IIci (maciici binary)
#
# Environment overrides:
#   MAME_DISK      path to hard-disk image (default per-machine below)
#   MAME_RAM       RAM size e.g. 4M, 8M (default per-machine)
#   MAME_WINDOW    1 = windowed (default), 0 = fullscreen
#   MAME_THROTTLE  1 = throttled (default), 0 = unthrottled + exit timer
#   MAME_SECONDS   seconds to run when unthrottled (default 30)
#   MAME_BIOS      ROM revision bios selector (e.g. 'default', 'original')
#   MAME_DEBUG     1 = launch with -debug (attaches MAME's built-in debugger)
#   MAME_LOG       1 = redirect console output to log/<machine>-<ts>.log
#
# Examples:
#   ./run.sh macse                          # windowed, throttled
#   MAME_DEBUG=1 ./run.sh maciici           # launch debugger
#   MAME_THROTTLE=0 MAME_SECONDS=60 ./run.sh macii   # 60s capture
#   ./run.sh maciix -nb9 "" -bitb1 socket.0 # pass extra MAME args

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="$SCRIPT_DIR/bin"
LOG_DIR="$SCRIPT_DIR/log"

if [ $# -lt 1 ]; then
    sed -n '1,30p' "$0" | grep -E '^#' | sed 's/^# \?//'
    exit 1
fi

MACHINE="$1"
shift

# machine -> binary (MAME subtarget)
declare -A MACHINE_BIN=(
    [macse]=macse
    [macsefd]=macse
    [macplus]=macse
    [macii]=macii
    [maciihmu]=macii
    [maciix]=macii
    [maciicx]=macii
    [macse30]=macii
    [maciici]=maciici
)

# default RAM size per machine (largest supported)
declare -A MACHINE_RAM=(
    [macse]=4M
    [macsefd]=4M
    [macplus]=4M
    [macii]=8M
    [maciihmu]=8M
    [maciix]=8M
    [maciicx]=8M
    [macse30]=8M
    [maciici]=8M
)

# default disk image per machine
declare -A MACHINE_DISK=(
    [macse]="$HOME/storage/images/macos-7.5.5.img"
    [macsefd]="$HOME/storage/images/macos-7.5.5.img"
    [macplus]="$HOME/storage/images/macos-7.5.5.img"
    [macii]="$HOME/storage/images/macos-7.5.5.img"
    [maciihmu]="$HOME/storage/images/macos-7.5.5.img"
    [maciix]="$HOME/storage/images/macos-7.5.5.img"
    [maciicx]="$HOME/storage/images/macos-7.5.5.img"
    [macse30]="$HOME/storage/images/macos-7.5.5.img"
    [maciici]="$HOME/storage/images/macos-7.5.5.img"
)

if [ -z "${MACHINE_BIN[$MACHINE]:-}" ]; then
    echo "error: unknown machine '$MACHINE'" >&2
    echo "valid: ${!MACHINE_BIN[*]}" >&2
    exit 1
fi

bin="${MACHINE_BIN[$MACHINE]}"
bin_path="$BIN_DIR/$bin"

if [ ! -x "$bin_path" ]; then
    echo "error: $bin_path not found or not executable" >&2
    echo "run ./bootstrap.sh first" >&2
    exit 1
fi

ram="${MAME_RAM:-${MACHINE_RAM[$MACHINE]}}"
disk="${MAME_DISK:-${MACHINE_DISK[$MACHINE]}}"

args=(
    "$MACHINE"
    -rompath "$BIN_DIR/roms"
    -ram "$ram"
    -skip_gameinfo
)

if [ -f "$disk" ]; then
    # Some Mac drivers use -harddisk1, newer ones just -harddisk
    # Try to detect: maciici uses -harddisk (CHD), others -harddisk1 (raw img)
    case "$MACHINE" in
        maciici)
            # maciici expects a CHD; if a .img is given, MAME may still accept
            # it but the idiomatic path is a CHD under storage/images
            if [ "${disk##*.}" = "chd" ]; then
                args+=(-harddisk "$disk")
            else
                args+=(-harddisk1 "$disk")
            fi
            ;;
        *)
            args+=(-harddisk1 "$disk")
            ;;
    esac
else
    echo "(no disk at $disk; booting to ROM)"
fi

# Suppress NuBus cards on Mac II family (we want a minimal config for
# differential debugging; NuBus video varies per-card and complicates things)
case "$MACHINE" in
    macii|maciihmu|maciix|maciicx|macse30)
        args+=(-nb9 "" -nba "" -nbb "" -nbc "" -nbd "" -nbe "")
        ;;
esac

# Window mode
if [ "${MAME_WINDOW:-1}" = "1" ]; then
    args+=(-window -nomax)
fi

# Throttle
if [ "${MAME_THROTTLE:-1}" = "0" ]; then
    args+=(-nothrottle -seconds_to_run "${MAME_SECONDS:-30}")
fi

# BIOS selector
if [ -n "${MAME_BIOS:-}" ]; then
    args+=(-bios "$MAME_BIOS")
fi

# Debugger
if [ "${MAME_DEBUG:-0}" = "1" ]; then
    args+=(-debug)
fi

# Pass through any remaining user args
args+=("$@")

echo "running: $bin_path ${args[*]}"
cd "$BIN_DIR"

if [ "${MAME_LOG:-0}" = "1" ]; then
    mkdir -p "$LOG_DIR"
    log_file="$LOG_DIR/${MACHINE}-$(date +%Y%m%d-%H%M%S).log"
    echo "logging to $log_file"
    exec "$bin_path" "${args[@]}" 2>&1 | tee "$log_file"
else
    exec "$bin_path" "${args[@]}"
fi
