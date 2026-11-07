#!/bin/bash
# build-retro68.sh — CfgTest as 68k and PowerPC applications built with
# Retro68 (RETRO68: the toolchain prefix with both, default
# ~/opt/retro68/build-ppc/toolchain) against the headers and glue generated
# from executor/core/multiversal: writes CfgTest68K.bin and CfgTestPPC.bin
# here (committed; the otcfg_* ctest cells use them).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
RETRO68="${RETRO68:-$HOME/opt/retro68/build-ppc/toolchain}"
WORK="${WORK:-${TMPDIR:-/tmp}/cfgtest-retro68.$$}"
mkdir -p "$WORK"
(cd "$REPO/executor/core/multiversal" && \
    PATH="$RETRO68/bin:$PATH" ruby make-multiverse.rb -G CIncludes -o "$WORK/mv" >/dev/null)
for arch in 68k ppc; do
    case $arch in
        68k) tc="$RETRO68/m68k-apple-macos/cmake/retro68.toolchain.cmake"; out=CfgTest68K ;;
        ppc) tc="$RETRO68/powerpc-apple-macos/cmake/retroppc.toolchain.cmake"; out=CfgTestPPC ;;
    esac
    cmake -S "$HERE" -B "$WORK/$arch" -DCMAKE_TOOLCHAIN_FILE="$tc" -DMV_OUT="$WORK/mv" >/dev/null
    cmake --build "$WORK/$arch" -j4
    cp "$WORK/$arch/CfgTest.bin" "$HERE/$out.bin"
    echo "$out.bin"
done
rm -rf "$WORK"
