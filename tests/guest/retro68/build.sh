#!/bin/bash
# build.sh — RetroTest for 68k and PowerPC with Retro68 (RETRO68: the
# toolchain prefix with both, default ~/opt/retro68/build-ppc/toolchain),
# against the headers generated from executor/core/multiversal/defs. Writes
# RetroTest68K.bin and RetroTestPPC.bin here (committed: the ctest cells
# use them).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
RETRO68="${RETRO68:-$HOME/opt/retro68/build-ppc/toolchain}"
WORK="${WORK:-${TMPDIR:-/tmp}/retrotest.$$}"
mkdir -p "$WORK"
(cd "$REPO/executor/core/multiversal" && \
    PATH="$RETRO68/bin:$PATH" ruby make-multiverse.rb -G CIncludes -o "$WORK/mv" >/dev/null)
for arch in 68k ppc; do
    case $arch in
        68k) tc="$RETRO68/m68k-apple-macos/cmake/retro68.toolchain.cmake"; out=RetroTest68K ;;
        ppc) tc="$RETRO68/powerpc-apple-macos/cmake/retroppc.toolchain.cmake"; out=RetroTestPPC ;;
    esac
    cmake -S "$HERE" -B "$WORK/$arch" -DCMAKE_TOOLCHAIN_FILE="$tc" -DMV_OUT="$WORK/mv" >/dev/null
    cmake --build "$WORK/$arch" -j4
    cp "$WORK/$arch/RetroTest.bin" "$HERE/$out.bin"
    echo "$out.bin"
done
rm -rf "$WORK"
