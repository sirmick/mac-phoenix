#!/bin/bash
# build-retro68.sh — StrProbe built with Retro68 against the headers
# generated from src/executor/multiversal, for 68k (StrProbe.bin: 68k code
# links Apple's GetIndString/GetIndPattern glue from Interface.o, so it
# measures the Mac's answer, not Executor's) and for PowerPC
# (StrProbePPC.bin: InterfaceLib's, Executor's C_GetIndString).
# RETRO68 / RETRO68_PPC: the toolchain prefixes.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
RETRO68="${RETRO68:-$HOME/opt/retro68/build/toolchain}"
RETRO68_PPC="${RETRO68_PPC:-$HOME/opt/retro68/build-ppc/toolchain}"
WORK="${WORK:-$HOME/storage/tmp/strprobe-retro68.$$}"
mkdir -p "$WORK"
(cd "$REPO/src/executor/multiversal" && \
    PATH="$RETRO68/bin:$PATH" ruby make-multiverse.rb -G CIncludes -o "$WORK/mv" >/dev/null)
cmake -S "$HERE" -B "$WORK/m68k" -DCMAKE_TOOLCHAIN_FILE="$RETRO68/m68k-apple-macos/cmake/retro68.toolchain.cmake" \
    -DMV_OUT="$WORK/mv" >/dev/null
cmake --build "$WORK/m68k" -j4
cp "$WORK/m68k/StrProbe.bin" "$HERE/StrProbe.bin"
cmake -S "$HERE" -B "$WORK/ppc" -DCMAKE_TOOLCHAIN_FILE="$RETRO68_PPC/powerpc-apple-macos/cmake/retroppc.toolchain.cmake" \
    -DMV_OUT="$WORK/mv" >/dev/null
cmake --build "$WORK/ppc" -j4
cp "$WORK/ppc/StrProbe.bin" "$HERE/StrProbePPC.bin"
echo StrProbe.bin StrProbePPC.bin
rm -rf "$WORK"
