#!/bin/bash
# build-retro68.sh — CGlueProbe as a PowerPC application built with Retro68
# (RETRO68PPC: the toolchain prefix, default ~/opt/retro68/build-ppc/toolchain)
# against the headers generated from executor/core/multiversal (RETRO68: the
# 68k toolchain they are generated with): writes CGlueProbePPC.bin here.
# PowerPC only: the lowercase routines are InterfaceLib exports; a 68k
# application has them from its own glue.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
RETRO68="${RETRO68:-$HOME/opt/retro68/build/toolchain}"
RETRO68PPC="${RETRO68PPC:-$HOME/opt/retro68/build-ppc/toolchain}"
WORK="${WORK:-$HOME/storage/tmp/cglueprobe-retro68.$$}"
mkdir -p "$WORK"
(cd "$REPO/executor/core/multiversal" && \
    PATH="$RETRO68/bin:$PATH" ruby make-multiverse.rb -G CIncludes -o "$WORK/mv" >/dev/null)
cmake -S "$HERE" -B "$WORK/ppc" -DCMAKE_TOOLCHAIN_FILE="$RETRO68PPC/powerpc-apple-macos/cmake/retroppc.toolchain.cmake" \
    -DMV_OUT="$WORK/mv" >/dev/null
cmake --build "$WORK/ppc" -j2
cp "$WORK/ppc/CGlueProbe.bin" "$HERE/CGlueProbePPC.bin"
echo CGlueProbePPC.bin
rm -rf "$WORK"
