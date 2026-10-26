#!/bin/bash
# build-retro68.sh — OTTest as a PowerPC application built with Retro68
# (RETRO68: the toolchain prefix with PowerPC, default
# ~/opt/retro68/build-ppc/toolchain) against the headers and glue generated
# from src/executor/multiversal, not Apple's: writes OTTestRetroPPC.bin here
# (committed; ctest ot_retro68_ppc_executor and ot_retro68_ppc_rom use it).
# OTTest.bin and OTTestPPC.bin are build.mpw's, on Apple's headers and glue.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
RETRO68="${RETRO68:-$HOME/opt/retro68/build-ppc/toolchain}"
WORK="${WORK:-${TMPDIR:-/tmp}/ottest-retro68.$$}"
mkdir -p "$WORK"
(cd "$REPO/src/executor/multiversal" && \
    PATH="$RETRO68/bin:$PATH" ruby make-multiverse.rb -G CIncludes -o "$WORK/mv" >/dev/null)
cmake -S "$HERE" -B "$WORK/ppc" \
    -DCMAKE_TOOLCHAIN_FILE="$RETRO68/powerpc-apple-macos/cmake/retroppc.toolchain.cmake" \
    -DMV_OUT="$WORK/mv" >/dev/null
cmake --build "$WORK/ppc" -j4
cp "$WORK/ppc/OTTest.bin" "$HERE/OTTestRetroPPC.bin"
echo OTTestRetroPPC.bin
rm -rf "$WORK"
