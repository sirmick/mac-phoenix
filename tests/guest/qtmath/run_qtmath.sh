#!/bin/bash
#
# run_qtmath.sh — a QTMath cell: QuickTime's $ABC2 math (qtmath.c) run by
# tests/test_guest_suite.sh, its answers against a reference
# (expected_NAME.txt: a Quadra's 7.6.1 with QuickTime 2.5).
#
#   tests/guest/qtmath/run_qtmath.sh NAME [--record] -- SUITE-ARGS...
#
set -uo pipefail

NAME="${1:?usage: run_qtmath.sh NAME [--record] -- SUITE-ARGS...}"
shift
RECORD=""
while [[ $# -gt 0 && "$1" != "--" ]]; do
    case "$1" in
        --record) RECORD=--record ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
    shift
done
[[ "${1:-}" == "--" ]] && shift

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
EXPECTED="$HERE/expected_$NAME.txt"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/qtmath.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

[[ -n "$RECORD" || -f "$EXPECTED" ]] || { echo "SKIP: no reference $EXPECTED (--record makes it)"; exit 77; }

bash "$ROOT/tests/test_guest_suite.sh" --app "$HERE/QTMath.bin" --results qtmath_results.txt \
    --network none --keep-dir "$WORK/keep" "$@"
rc=$?
[[ $rc -eq 77 ]] && exit 77
RESULTS="$WORK/keep/qtmath_results.txt"
[[ -f "$RESULTS" ]] || { echo "FAIL: QTMath wrote no results (test_guest_suite.sh: $rc)"; exit 1; }
grep -q -- '^---' <(tr '\r' '\n' < "$RESULTS") || { echo "FAIL: QTMath did not finish"; exit 1; }

python3 "$ROOT/tests/guest/qt/compare.py" --results "$RESULTS" --expected "$EXPECTED" $RECORD
rc=$?
if [[ $rc -eq 0 ]]; then echo "PASS: QTMath as $NAME"; else echo "FAIL: QTMath differs from $NAME"; fi
exit $rc
