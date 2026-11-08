#!/bin/bash
#
# run_qt.sh — a QTTest cell: the movies (tests/lib/qtmovie.py) on Host:,
# QTTest run by tests/test_guest_suite.sh, its results against a
# reference (compare.py: tests/guest/qt/expected_NAME.txt, the movies' own
# pixels, the tones in the sound the Mac played).
#
#   tests/guest/qt/run_qt.sh NAME [--record] [--needs-qt] [--no-audio] -- SUITE-ARGS...
#
# NAME: the reference, expected_NAME.txt, and where the movies QTTest made
# in Apple's codecs are kept, ~/storage/qt/NAME/ (made-*.mov; not git).
# --record: write the reference from this run instead of comparing.
# --needs-qt: exit 77 (skip) when QTTest finds no QuickTime (Gestalt
# 'qtim'), as on Executor until it runs Apple's QuickTime.
# SUITE-ARGS: test_guest_suite.sh's (--backend, --image, --port ...).
#
set -uo pipefail

NAME="${1:?usage: run_qt.sh NAME [--record] [--needs-qt] [--no-audio] -- SUITE-ARGS...}"
shift
RECORD=""
NEEDS_QT=""
AUDIO=1
while [[ $# -gt 0 && "$1" != "--" ]]; do
    case "$1" in
        --record) RECORD=--record ;;
        --needs-qt) NEEDS_QT=1 ;;
        --no-audio) AUDIO="" ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
    shift
done
[[ "${1:-}" == "--" ]] && shift

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
EXPECTED="$HERE/expected_$NAME.txt"
KEEP_STORE="${QT_STORE:-$HOME/storage/qt}/$NAME"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/qttest.XXXXXX")"
trap '[[ -n "${QT_KEEP_WORK:-}" ]] && echo "work: $WORK" || rm -rf "$WORK"' EXIT   # QT_KEEP_WORK=1: keep it

[[ -n "$RECORD" || -f "$EXPECTED" ]] || { echo "SKIP: no reference $EXPECTED (--record makes it)"; exit 77; }

python3 "$ROOT/tests/lib/qtmovie.py" "$WORK/movies" || { echo "FAIL: qtmovie.py"; exit 1; }
HOST_ARGS=()
for f in "$WORK"/movies/*.mov; do HOST_ARGS+=(--host-file "$f"); done
AUDIO_ARGS=()
[[ -n "$AUDIO" ]] && AUDIO_ARGS=(--audio-dump "$WORK/audio.raw")

bash "$ROOT/tests/test_guest_suite.sh" --app "$HERE/QTTest.bin" --results qt_results.txt \
    --network none --keep-dir "$WORK/keep" "${HOST_ARGS[@]}" "${AUDIO_ARGS[@]}" "$@"
rc=$?
[[ $rc -eq 77 ]] && exit 77
RESULTS="$WORK/keep/qt_results.txt"
[[ -f "$RESULTS" ]] || { echo "FAIL: QTTest wrote no results (test_guest_suite.sh: $rc)"; exit 1; }
grep -q -- '^---' <(tr '\r' '\n' < "$RESULTS") || { echo "FAIL: QTTest did not finish"; exit 1; }

if [[ -n "$NEEDS_QT" ]] && ! tr '\r' '\n' < "$RESULTS" | grep -q '^PASS gestalt.qtim '; then
    echo "SKIP: no QuickTime here ($(tr '\r' '\n' < "$RESULTS" | grep -m1 'gestalt.qtim'))"
    exit 77
fi

shopt -s nullglob
made=("$WORK"/keep/made-*.mov)
if [[ ${#made[@]} -gt 0 ]]; then
    mkdir -p "$KEEP_STORE" && cp "${made[@]}" "$KEEP_STORE"/ && echo "kept ${#made[@]} movies in $KEEP_STORE"
fi

echo "--- compare ---"
AUDIO_CMP=()
[[ -n "$AUDIO" && -s "$WORK/audio.raw" ]] && AUDIO_CMP=(--audio "$WORK/audio.raw")
[[ -n "$AUDIO" && ! -s "$WORK/audio.raw" ]] && echo "sound: nothing in the audio dump" && AUDIO_FAIL=1
python3 "$HERE/compare.py" --results "$RESULTS" --expected "$EXPECTED" \
    --manifest "$WORK/movies/manifest.txt" "${AUDIO_CMP[@]}" $RECORD
cmp_rc=$?
[[ -n "${AUDIO_FAIL:-}" ]] && cmp_rc=1
if [[ $cmp_rc -eq 0 ]]; then echo "PASS: QTTest as $NAME"; else echo "FAIL: QTTest differs from $NAME"; fi
exit $cmp_rc
