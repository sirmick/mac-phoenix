#!/bin/bash
# build-retro68.sh — GWorldGallery as a PowerPC application built with Retro68
# (RETRO68: the toolchain prefix, default ~/opt/retro68/build-ppc/toolchain)
# against the headers generated from executor/core/multiversal: writes
# GWorldGalleryPPC.bin here.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
RETRO68="${RETRO68:-$HOME/opt/retro68/build-ppc/toolchain}"
WORK="${WORK:-$HOME/storage/tmp/gworld-retro68.$$}"
mkdir -p "$WORK"
(cd "$REPO/executor/core/multiversal" && \
    PATH="$RETRO68/bin:$PATH" ruby make-multiverse.rb -G CIncludes -o "$WORK/mv" >/dev/null)
cmake -S "$HERE" -B "$WORK/ppc" -DCMAKE_TOOLCHAIN_FILE="$RETRO68/powerpc-apple-macos/cmake/retroppc.toolchain.cmake" \
    -DMV_OUT="$WORK/mv" >/dev/null
cmake --build "$WORK/ppc" -j4
cp "$WORK/ppc/GWorldGallery.bin" "$HERE/GWorldGalleryPPC.bin"
echo GWorldGalleryPPC.bin
rm -rf "$WORK"
