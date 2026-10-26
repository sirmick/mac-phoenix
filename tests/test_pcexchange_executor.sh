#!/bin/bash
#
# test_pcexchange_executor.sh — a DOS disk through PC Exchange on Executor's
# File System Manager (docs/executor/FSM.md).
#
# Makes a FAT16 image (tests/lib/fat_image.py), boots Executor on Apple's
# System with it as a second disk: Executor's own file systems don't read
# it, PC Exchange (on in the extension policy) installs its DOS file system
# with InstallFS and mounts the drive. Then, through the bridge (the
# BridgeAgent's File Manager calls, 68k code on the DOS volume):
#   - a file, a multi-cluster file and a folder are copied off the DOS
#     volume to the Host folder and compared with what the image holds;
#   - a folder with a 70 KB file and a text file is copied onto it;
# the guest shuts down (the volume unmounted, PC Exchange writing back),
# and the image must be a consistent FAT with the new files in it.
#
# Usage:
#   tests/test_pcexchange_executor.sh [--timeout N] [--port N]
#                                     [--system tests-7.6.1|tests-7.5.5]
#
# 7.5.5's PC Exchange (2.0.5) is off in the extension policy (STRAGGLERS
# 62); --system tests-7.5.5 needs it allowed in the System's
# <data_dir>/extension-policy.txt.
#
# Skips (exit 77) if the System image isn't available.
set -euo pipefail

TIMEOUT=90
PORT=18450
SYSTEM=tests-7.6.1
BINARY="${MACEMU_BINARY:-$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port)    PORT="$2";    shift 2 ;;
        --system)  SYSTEM="$2";  shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FAT="$SCRIPT_DIR/lib/fat_image.py"

DISK="${MACEMU_DISK:-$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" "$SYSTEM" $$ 2>/dev/null || true)}"
if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
if [[ -z "${DISK:-}" || ! -f "$DISK" ]]; then echo "SKIP: Disk not found: ${DISK:-<unset>}"; exit 77; fi

WORK=$(mktemp -d ${TMPDIR:-/tmp}/macpcxtest.XXXXXX)
EXTFS_DIR="$WORK/host"
DOS="$WORK/dos.img"
LOG="${TMPDIR:-/tmp}/macpcxtest_$$.log"
mkdir -p "$EXTFS_DIR"
cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT SIGTERM SIGINT

PASS=0
FAIL=0
pass() { echo "  PASS: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

api() { curl -sf --max-time 60 -X POST "http://localhost:$PORT/api/$1" -H 'Content-Type: application/json' -d "$2"; }
copy() { api copy "{\"from\":\"$1\",\"to\":\"$2\"}" | grep -q '"success": true'; }

echo "=== PC Exchange on Executor's File System Manager ($SYSTEM) ==="
python3 "$FAT" make "$DOS" PCDISK

# What the guest should read: the image's own bytes.
python3 "$FAT" cat "$DOS" README.TXT > "$WORK/README.TXT"
python3 "$FAT" cat "$DOS" DATA.BIN > "$WORK/DATA.BIN"
python3 "$FAT" cat "$DOS" DOCS/NOTE.TXT > "$WORK/NOTE.TXT"
# What it writes: a folder with a file of several clusters and a short one.
mkdir -p "$EXTFS_DIR/MACDIR"
python3 -c "import sys; sys.stdout.buffer.write(bytes((i * 13 + 5) & 0xFF for i in range(70000)))" \
    > "$EXTFS_DIR/MACDIR/BIG.BIN"
printf 'Written by the Mac.\r' > "$EXTFS_DIR/MACDIR/WRITTEN.TXT"

"$BINARY" --backend executor --timeout "$((TIMEOUT + 30))" \
    --config /dev/null --dismiss-shutdown-dialog --headless-http \
    --port "$PORT" --extfs "$EXTFS_DIR" --executor-system "$(basename "$DISK")" \
    --executor-writable-images --disk "$DOS" &>"$LOG" &
EMU_PID=$!

echo -n "HTTP up..."
for i in $(seq 1 20); do
    curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1 && { echo " ok"; break; }
    kill -0 "$EMU_PID" 2>/dev/null || { echo " emulator died"; tail -20 "$LOG"; exit 1; }
    sleep 0.5
done
curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null

echo -n "Booting..."
START=$(date +%s)
while ! curl -sf "http://localhost:$PORT/api/status" 2>/dev/null | grep -q '"bridge_agent_connected": true'; do
    if [[ $(( $(date +%s) - START )) -ge $TIMEOUT ]]; then
        echo " no bridge agent"; tail -30 "$LOG"; exit 1
    fi
    sleep 1
done
echo " bridge up ($(( $(date +%s) - START ))s)"

if grep -q 'FSM: DOS installed' "$LOG"; then pass "PC Exchange installed its DOS file system"
else fail "PC Exchange installed its DOS file system"; fi
if grep -q 'FSM: drive [0-9]* mounted by DOS: 0' "$LOG"; then pass "the DOS disk mounted"
else fail "the DOS disk mounted"; grep -a 'FSM' "$LOG" || true; fi

# Reading: off the DOS volume, compared with the image's bytes.
for item in README.TXT DATA.BIN; do
    if copy "PCDISK:$item" "Host:" && cmp -s "$EXTFS_DIR/$item" "$WORK/$item"; then
        pass "read $item ($(stat -c %s "$WORK/$item") bytes)"
    else
        fail "read $item"
    fi
done
if copy "PCDISK:DOCS" "Host:" && cmp -s "$EXTFS_DIR/DOCS/NOTE.TXT" "$WORK/NOTE.TXT"; then
    pass "read the DOCS folder"
else
    fail "read the DOCS folder"
fi

# Writing: a folder onto it.
if copy "Host:MACDIR" "PCDISK:"; then pass "copied a folder onto the DOS volume"
else fail "copied a folder onto the DOS volume"; fi

# Shut down: the volume is unmounted, PC Exchange writes back.
api shutdown '{}' >/dev/null || true
for i in $(seq 1 30); do
    kill -0 "$EMU_PID" 2>/dev/null || break
    sleep 1
done
if kill -0 "$EMU_PID" 2>/dev/null; then
    fail "the guest shut down"
    kill "$EMU_PID" 2>/dev/null || true
    wait "$EMU_PID" 2>/dev/null || true
else
    pass "the guest shut down"
fi
EMU_PID=""

if PROBLEMS=$(python3 "$FAT" check "$DOS"); then pass "the FAT is consistent"
else fail "the FAT is consistent: $PROBLEMS"; fi
for item in BIG.BIN WRITTEN.TXT; do
    if python3 "$FAT" cat "$DOS" "MACDIR/$item" > "$WORK/out" 2>/dev/null \
        && cmp -s "$WORK/out" "$EXTFS_DIR/MACDIR/$item"; then
        pass "MACDIR/$item is on the disk"
    else
        fail "MACDIR/$item is on the disk"
    fi
done

echo "=== $PASS passed, $FAIL failed ==="
if [[ $FAIL -gt 0 ]]; then
    echo "Log: $LOG"
    exit 1
fi
rm -f "$LOG"
