#!/bin/bash
# build-retro68.sh [PROBE ...] — the differential probes as PowerPC
# applications built with Retro68 (RETRO68: the toolchain prefix, default
# ~/opt/retro68/build-ppc/toolchain) against the headers generated from
# executor/core/multiversal: writes <probe>PPC.bin here (all of them, or
# those named).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
RETRO68="${RETRO68:-$HOME/opt/retro68/build-ppc/toolchain}"
WORK="${WORK:-$HOME/storage/tmp/difftest-retro68.$$}"
mkdir -p "$WORK"
(cd "$REPO/executor/core/multiversal" && \
    PATH="$RETRO68/bin:$PATH" ruby make-multiverse.rb -G CIncludes -o "$WORK/mv" >/dev/null)
cmake -S "$HERE" -B "$WORK/ppc" -DCMAKE_TOOLCHAIN_FILE="$RETRO68/powerpc-apple-macos/cmake/retroppc.toolchain.cmake" \
    -DMV_OUT="$WORK/mv" >/dev/null
if [[ $# -gt 0 ]]; then
    for p in "$@"; do cmake --build "$WORK/ppc" -j4 --target "${p}_APPL"; done
else
    cmake --build "$WORK/ppc" -j4
fi
for bin in "$WORK"/ppc/*.bin; do
    name=$(basename "$bin" .bin)
    [[ "$name" == *.code || "$name" == *.r.rsrc ]] && continue
    cp "$bin" "$HERE/${name}PPC.bin"
    echo "${name}PPC.bin"
done
rm -rf "$WORK"
