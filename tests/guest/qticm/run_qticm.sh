#!/bin/bash
#
# run_qticm.sh — a QTICM cell: QuickTime's Image Compression Manager (qticm.c) run by
# tests/test_guest_suite.sh, its answers against a reference
# (expected_NAME.txt: a Quadra's 7.6.1 with QuickTime 2.5).
#
#   tests/guest/qticm/run_qticm.sh NAME [--record] [--needs-icm] [--known FILE] -- SUITE-ARGS...
#
# --needs-icm: exit 77 (skip) when Gestalt has no 'icmp' (docs/executor/QuickTime.md).
# --known FILE: result names (one per line, "#" comments) known to differ
# from the reference, with the reason in FILE: shown, not failed.
#
# The codec names 'raw ', 'rle ', 'smc ' end in a space; compare.py names a
# line by its second word, so they are compared as raw_, rle_, smc_.
#
set -uo pipefail

NAME="${1:?usage: run_qticm.sh NAME [--record] -- SUITE-ARGS...}"
shift
RECORD=""
NEEDS_ICM=""
KNOWN=""
while [[ $# -gt 0 && "$1" != "--" ]]; do
    case "$1" in
        --record) RECORD=--record ;;
        --needs-icm) NEEDS_ICM=1 ;;
        --known) KNOWN="$2"; shift ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
    shift
done
[[ "${1:-}" == "--" ]] && shift

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
EXPECTED="$HERE/expected_$NAME.txt"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/qticm.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

[[ -n "$RECORD" || -f "$EXPECTED" ]] || { echo "SKIP: no reference $EXPECTED (--record makes it)"; exit 77; }

bash "$ROOT/tests/test_guest_suite.sh" --app "$HERE/QTICM.bin" --results qticm_results.txt \
    --network none --keep-dir "$WORK/keep" "$@"
rc=$?
[[ $rc -eq 77 ]] && exit 77
RESULTS="$WORK/keep/qticm_results.txt"
[[ -f "$RESULTS" ]] || { echo "FAIL: QTICM wrote no results (test_guest_suite.sh: $rc)"; exit 1; }
grep -q -- '^---' <(tr '\r' '\n' < "$RESULTS") || { echo "FAIL: QTICM did not finish"; exit 1; }

if [[ -n "$NEEDS_ICM" ]] && tr '\r' '\n' < "$RESULTS" | grep -q "^# no Gestalt 'icmp'"; then
    echo "SKIP: no ICM here (Gestalt 'icmp')"
    exit 77
fi

if [[ -n "$RECORD" ]]; then
    python3 "$ROOT/tests/guest/qt/compare.py" --results "$RESULTS" --expected "$EXPECTED" --record
    exit $?
fi
norm() { tr '\r' '\n' < "$1" | sed -E 's/\.(raw|rle|smc) /.\1_/'; }
norm "$RESULTS" > "$WORK/results.txt"
norm "$EXPECTED" > "$WORK/expected.txt"
if [[ -n "$KNOWN" ]]; then
    grep -v '^#' "$KNOWN" | awk 'NF {print $1}' > "$WORK/known.txt"
    awk 'NR == FNR {k[$1] = 1; next} ($2 in k) {print}' "$WORK/known.txt" "$WORK/results.txt" | sed 's/^/known: /'
    for f in results expected; do
        awk 'NR == FNR {k[$1] = 1; next} !($2 in k) {print}' "$WORK/known.txt" "$WORK/$f.txt" > "$WORK/$f.k"
        mv "$WORK/$f.k" "$WORK/$f.txt"
    done
fi
python3 "$ROOT/tests/guest/qt/compare.py" --results "$WORK/results.txt" --expected "$WORK/expected.txt"
rc=$?
if [[ $rc -eq 0 ]]; then echo "PASS: QTICM as $NAME"; else echo "FAIL: QTICM differs from $NAME"; fi
exit $rc
