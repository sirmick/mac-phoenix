#!/bin/bash
# aehdlr68k.sh [test_guest_suite.sh args...]: the private Apple event
# handler-table probe (aehdlr68k.bin, 68k only: Pack8 $092E $0830 $0831
# $0632 $0833 $042F) on a session, its answers compared with the Quadra
# 650's 7.6.1, Apple's PACK 8 (ref/aehdlr68k-quadra-7.6.1.txt). Built with
# build-68k.sh from aehdlr68k.c. Executor answers with its own Apple Event
# Manager on 8.6; on 7.6.1 the AppleScript extension installs Apple's PACK 8.
# Exit: 0 match, 1 differ, 77 skipped.
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
keep=$(mktemp -d -p "${MACEMU_STORAGE:-$HOME/storage}/tmp" aehdlr68k-XXXX)
trap 'rm -rf "$keep"' EXIT
bash "$REPO/tests/test_guest_suite.sh" "$@" --app "$HERE/aehdlr68k.bin" --results aehdlr.txt \
    --network none --timeout 150 --keep-dir "$keep"
rc=$?
[ $rc -eq 77 ] && exit 77
[ -f "$keep/aehdlr.txt" ] || { echo "FAIL: no aehdlr.txt (suite exit $rc)"; exit 1; }
want=$(grep -v -e '^#' "$HERE/ref/aehdlr68k-quadra-7.6.1.txt")
got=$(tr '\r' '\n' < "$keep/aehdlr.txt" | grep -v -e '^$')
if diff <(echo "$want") <(echo "$got"); then
    echo "PASS: aehdlr68k matches the Quadra"
else
    echo "FAIL: aehdlr68k differs from the Quadra"
    exit 1
fi
