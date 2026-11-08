#!/bin/bash
#
# run_qticm.sh — a QTICM cell: QuickTime's Image Compression Manager (qticm.c) run by
# tests/test_guest_suite.sh, its answers against a reference
# (expected_NAME.txt: a Quadra's 7.6.1 with QuickTime 2.5).
#
#   tests/guest/qticm/run_qticm.sh NAME [--record] [--needs-icm] -- SUITE-ARGS...
#
# --needs-icm: exit 77 (skip) when Gestalt has no 'icmp' (Executor until its
# ICM is complete; docs/executor/QuickTime.md).
#
set -uo pipefail

NAME="${1:?usage: run_qticm.sh NAME [--record] -- SUITE-ARGS...}"
shift
RECORD=""
NEEDS_ICM=""
while [[ $# -gt 0 && "$1" != "--" ]]; do
    case "$1" in
        --record) RECORD=--record ;;
        --needs-icm) NEEDS_ICM=1 ;;
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

python3 "$ROOT/tests/guest/qt/compare.py" --results "$RESULTS" --expected "$EXPECTED" $RECORD
rc=$?
if [[ $rc -eq 0 ]]; then echo "PASS: QTICM as $NAME"; else echo "FAIL: QTICM differs from $NAME"; fi
exit $rc
