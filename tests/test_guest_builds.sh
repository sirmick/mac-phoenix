#!/bin/bash
#
# test_guest_builds.sh — the committed guest programs are their sources'
# builds. Each MPW project whose product is committed beside its sources
# is built again with tools/mpw/mpw (a private session) and compared with
# the committed file by tests/lib/macbinary_same.py (what a build makes,
# not when). A difference means a source changed without its product
# being rebuilt:
#
#   tools/mpw/mpw build tests/guest/suite -o tests/guest/suite
#
# Skips (exit 77) without the MPW disk (tools/mpw/mpw setup).
#
# Usage: tests/test_guest_builds.sh [--port N]
set -uo pipefail
PORT=18396
while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) PORT="$2"; shift 2 ;;
        --timeout) shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"
STORAGE="${MACEMU_STORAGE:-$HOME/storage}"
MPW=tools/mpw/mpw
[[ -f "$STORAGE/images/mpw.img" ]] || { echo "SKIP: no MPW disk (tools/mpw/mpw setup)"; exit 77; }
[[ -f "$STORAGE/images/tests-7.5.5.img" ]] || { echo "SKIP: no tests-7.5.5.img (tools/mpw/mpw setup)"; exit 77; }
[[ -x build/mac-phoenix ]] || { echo "SKIP: no build/mac-phoenix"; exit 77; }

export MPW_STATE=$(mktemp -d /tmp/mpwbuilds.XXXXXX)
export MPW_PORT=$PORT
export MPW_SYSTEM="test-mpwbuilds-7.5.5.img"
OUT=$(mktemp -d /tmp/mpwbuilds-out.XXXXXX)
cleanup() {
    $MPW stop >/dev/null 2>&1
    rm -rf "$MPW_STATE" "$OUT" "$STORAGE/images/$MPW_SYSTEM" \
        "$STORAGE/executor-systems/$MPW_SYSTEM" "$STORAGE/executor-systems/$MPW_SYSTEM.clean"
}
trap cleanup EXIT

# project directory : committed product
PROJECTS=(
    "tests/guest/suite:MacPhoenixSuite.bin"
    "tests/guest/ot:OTTest.bin"
)

FAILS=0
for p in "${PROJECTS[@]}"; do
    dir=${p%%:*}
    product=${p#*:}
    mkdir -p "$OUT/$dir"
    if ! $MPW build "$dir" -o "$OUT/$dir" >"$OUT/build.log" 2>&1; then
        echo "FAIL $dir: build failed"
        tail -20 "$OUT/build.log"
        FAILS=$((FAILS + 1))
    elif why=$(python3 tests/lib/macbinary_same.py "$dir/$product" "$OUT/$dir/$product"); then
        echo "PASS $dir/$product"
    else
        echo "FAIL $dir/$product is not its sources' build ($why); rebuild: $MPW build $dir -o $dir"
        FAILS=$((FAILS + 1))
    fi
done
exit $((FAILS > 0))
