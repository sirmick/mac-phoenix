#!/bin/bash
# build-retro68.sh — PartProbe as a 68k application built with Retro68
# (RETRO68: the toolchain prefix, default ~/opt/retro68/build/toolchain)
# against the headers generated from src/executor/multiversal: writes
# PartProbe.bin here.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
RETRO68="${RETRO68:-$HOME/opt/retro68/build/toolchain}"
WORK="${WORK:-$HOME/storage/tmp/partprobe-retro68.$$}"
mkdir -p "$WORK"
(cd "$REPO/src/executor/multiversal" && \
    PATH="$RETRO68/bin:$PATH" ruby make-multiverse.rb -G CIncludes -o "$WORK/mv" >/dev/null)
cmake -S "$HERE" -B "$WORK/m68k" -DCMAKE_TOOLCHAIN_FILE="$RETRO68/m68k-apple-macos/cmake/retro68.toolchain.cmake" \
    -DMV_OUT="$WORK/mv" >/dev/null
cmake --build "$WORK/m68k" -j4
cp "$WORK/m68k/PartProbe.bin" "$HERE/PartProbe.bin"
echo PartProbe.bin
rm -rf "$WORK"
