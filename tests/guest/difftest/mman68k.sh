#!/bin/bash
# mman68k.sh [test_guest_suite.sh args...]: the mman probe's 68k build
# (mman68k.bin) on a session, its answers compared with the Quadra 650's
# 7.6.1 (ref/mman68k-quadra-7.6.1.txt; 7.5.5's are the same).
# Built like the PowerPC probes, with the 68k Retro68 toolchain and only
# mman.c (no CFragManager). Exit: 0 match, 1 differ, 77 skipped.
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
keep=$(mktemp -d -p "${MACEMU_STORAGE:-$HOME/storage}/tmp" mman68k-XXXX)
trap 'rm -rf "$keep"' EXIT
bash "$REPO/tests/test_guest_suite.sh" "$@" --app "$HERE/mman68k.bin" --results mman.txt \
    --network none --timeout 150 --keep-dir "$keep"
rc=$?
[ $rc -eq 77 ] && exit 77
[ -f "$keep/mman.txt" ] || { echo "FAIL: no mman.txt (suite exit $rc)"; exit 1; }
want=$(grep -v -e '^#' "$HERE/ref/mman68k-quadra-7.6.1.txt")
got=$(tr '\r' '\n' < "$keep/mman.txt" | grep -v -e '^$')
if diff <(echo "$want") <(echo "$got"); then
    echo "PASS: mman68k matches the Quadra"
else
    echo "FAIL: mman68k differs from the Quadra"
    exit 1
fi
