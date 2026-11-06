#!/bin/bash
# build-retro68.sh — OTTest as PowerPC and 68k applications built with
# Retro68 (RETRO68: the toolchain prefix with both, default
# ~/opt/retro68/build-ppc/toolchain) against the headers and glue generated
# from executor/core/multiversal, not Apple's: writes OTTestRetroPPC.bin and
# OTTestRetro68K.bin here (committed; the ot_retro68_* ctest cells use them).
# OTTest.bin and OTTestPPC.bin are build.mpw's, on Apple's headers and glue.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
RETRO68="${RETRO68:-$HOME/opt/retro68/build-ppc/toolchain}"
WORK="${WORK:-${TMPDIR:-/tmp}/ottest-retro68.$$}"
mkdir -p "$WORK"
(cd "$REPO/executor/core/multiversal" && \
    PATH="$RETRO68/bin:$PATH" ruby make-multiverse.rb -G CIncludes -o "$WORK/mv" >/dev/null)
for arch in ppc 68k; do
    case $arch in
        ppc) tc="$RETRO68/powerpc-apple-macos/cmake/retroppc.toolchain.cmake"; out=OTTestRetroPPC ;;
        68k) tc="$RETRO68/m68k-apple-macos/cmake/retro68.toolchain.cmake"; out=OTTestRetro68K ;;
    esac
    cmake -S "$HERE" -B "$WORK/$arch" -DCMAKE_TOOLCHAIN_FILE="$tc" -DMV_OUT="$WORK/mv" >/dev/null
    cmake --build "$WORK/$arch" -j4
    cp "$WORK/$arch/OTTest.bin" "$HERE/$out.bin"
    echo "$out.bin"
done
rm -rf "$WORK"
