#!/bin/bash
# Boot trace of one machine in MAME (see README.md here).
#   run_trace.sh <machine> [extra mame args...]
# Env: BT_ROOT (default ~/storage/tmp/boottrace), BT_FLOPPY (800K boot
# floppy, default $BT_ROOT/media/System_Tools.img), BT_NAME (output dir
# name, default <machine>), BT_RAM (default 8m), BT_MAXSECS, BT_TRAPS,
# BT_SNAP ("408004b6=name,..." low-memory dumps at the first execution of each PC).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="$HERE/../bin"
machine="$1"; shift
root="${BT_ROOT:-$HOME/storage/tmp/boottrace}"
name="${BT_NAME:-$machine}"
out="$root/$name"
floppy="${BT_FLOPPY:-$root/media/System_Tools.img}"
rm -rf "$out"; mkdir -p "$out"
cp "$floppy" "$out/boot.img"          # MAME writes to it
export MACTRACE="$out/trace.raw.tsv"
export MACTRACE_TRAPS="${BT_TRAPS:-20000}"
export MACTRACE_SNAP="${BT_SNAP:-}"
export BT_OUT="$out"
start=$(date +%s)
"$BIN/mactrace" "$machine" -rompath "$BIN/roms" -ram "${BT_RAM:-8m}" \
    -flop1 "$out/boot.img" \
    -nothrottle -video none -sound none -skip_gameinfo -nomouse \
    -cfg_directory "$out/cfg" -nvram_directory "$out/nvram" \
    -snapshot_directory "$out" -diff_directory "$out/diff" \
    -autoboot_script "$HERE/boottrace.lua" "$@" > "$out/mame.log" 2>&1 || true
echo "$machine: $(( $(date +%s) - start )) s wall; $(wc -l < "$out/trace.raw.tsv") trace lines"
