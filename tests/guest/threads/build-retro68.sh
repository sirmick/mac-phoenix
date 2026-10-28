#!/bin/bash
# build-retro68.sh — ThreadTest as a PowerPC application built with Retro68
# (RETRO68: the toolchain prefix, default ~/opt/retro68/build-ppc/toolchain)
# against the headers generated from src/executor/multiversal: writes
# ThreadTestPPC.bin here.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
RETRO68="${RETRO68:-$HOME/opt/retro68/build-ppc/toolchain}"
WORK="${WORK:-$HOME/storage/tmp/threads-retro68.$$}"
mkdir -p "$WORK"
(cd "$REPO/src/executor/multiversal" && \
    PATH="$RETRO68/bin:$PATH" ruby make-multiverse.rb -G CIncludes -o "$WORK/mv" >/dev/null)
cmake -S "$HERE" -B "$WORK/ppc" -DCMAKE_TOOLCHAIN_FILE="$RETRO68/powerpc-apple-macos/cmake/retroppc.toolchain.cmake" \
    -DMV_OUT="$WORK/mv" >/dev/null
cmake --build "$WORK/ppc" -j4
cp "$WORK/ppc/ThreadTest.bin" "$HERE/ThreadTestPPC.bin"
echo ThreadTestPPC.bin
rm -rf "$WORK"
