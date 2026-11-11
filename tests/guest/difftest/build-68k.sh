#!/bin/bash
# build-68k.sh PROBE [CREATOR] — a differential probe as a 68k application
# (PROBE.c alone, no CFragManager), built with Retro68's m68k toolchain
# (RETRO68: the toolchain prefix, default ~/opt/retro68/build/toolchain)
# against the headers generated from executor/core/multiversal: writes
# PROBE.bin here (mman68k.bin, aehdlr68k.bin).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
PROBE=${1:?usage: build-68k.sh PROBE [CREATOR]}
CREATOR=${2:-DfP8}
RETRO68="${RETRO68:-$HOME/opt/retro68/build/toolchain}"
WORK="$(mktemp -d "${TMPDIR:-$HOME/storage/tmp}/difftest-68k.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
(cd "$REPO/executor/core/multiversal" && \
    PATH="$RETRO68/bin:$PATH" ruby make-multiverse.rb -G CIncludes -o "$WORK/mv" >/dev/null)
mkdir -p "$WORK/src"
cat > "$WORK/src/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.16)
project(Probe68k C)
include_directories(BEFORE "$WORK/mv/CIncludes" "$HERE")
link_directories(BEFORE "$WORK/mv/lib68k")
add_application($PROBE "$HERE/$PROBE.c" CREATOR "$CREATOR")
EOF
cmake -S "$WORK/src" -B "$WORK/build" \
    -DCMAKE_TOOLCHAIN_FILE="$RETRO68/m68k-apple-macos/cmake/retro68.toolchain.cmake" >/dev/null
cmake --build "$WORK/build" -j4 >/dev/null
cp "$WORK/build/$PROBE.bin" "$HERE/$PROBE.bin"
echo "$PROBE.bin"
