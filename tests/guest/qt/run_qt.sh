#!/bin/bash
#
# run_qt.sh — a QTTest cell: the movies (tests/lib/qtmovie.py) on Host:,
# QTTest run by tests/test_guest_suite.sh, its results against a
# reference (compare.py: tests/guest/qt/expected_NAME.txt, the movies' own
# pixels, the tones in the sound the Mac played).
#
#   tests/guest/qt/run_qt.sh NAME [--record] [--needs-qt] [--no-audio] [--known FILE] [--disk] -- SUITE-ARGS...
#
# NAME: the reference, expected_NAME.txt, and where the movies QTTest made
# in Apple's codecs are kept, ~/storage/qt/NAME/ (made-*.mov; not git).
# --record: write the reference from this run instead of comparing.
# --needs-qt: exit 77 (skip) when QTTest finds no QuickTime (Gestalt
# 'qtim').
# --known FILE: result names known to differ here (compare.py --known).
# --disk: the movies on our disks instead (disk_movies.sh: Ares, HyperCard,
# 7.6.1's Extensions), frames at fixed times and a play; the sound: at
# least 5 s heard with HyperCard's Simple Movie among them (6 s of sound
# from its start), any without (QT_DISK_HEARD=SECS: that instead);
# exit 77 when none of their images is here. QT_DISK_ONLY="NAME ...": only
# those (disk_movies.sh's names).
# SUITE-ARGS: test_guest_suite.sh's (--backend, --image, --port ...).
#
set -uo pipefail

NAME="${1:?usage: run_qt.sh NAME [--record] [--needs-qt] [--no-audio] -- SUITE-ARGS...}"
shift
RECORD=""
NEEDS_QT=""
AUDIO=1
KNOWN=()
DISK=""
while [[ $# -gt 0 && "$1" != "--" ]]; do
    case "$1" in
        --record) RECORD=--record ;;
        --needs-qt) NEEDS_QT=1 ;;
        --no-audio) AUDIO="" ;;
        --known) KNOWN=(--known "$2"); shift ;;
        --disk) DISK=1 ;;
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

HOST_ARGS=()
MANIFEST=()
if [[ -n "$DISK" ]]; then
    DISK_CACHE="${QT_STORE:-$HOME/storage/qt}/disk-movies"
    names=$(bash "$HERE/disk_movies.sh" "$DISK_CACHE" | tr "\n" " ")
    [[ -n "$names" ]] || { echo "SKIP: none of the disks' movies here (disk_movies.sh)"; exit 77; }
    mkdir -p "$WORK/movies"
    : > "$WORK/movies/qt_disk.txt"
    for n in $names; do
        [[ -n "${QT_DISK_ONLY:-}" && " $QT_DISK_ONLY " != *" $n "* ]] && continue   # debugging: some of them
        HOST_ARGS+=(--host-macbinary "$DISK_CACHE/$n.bin")
        printf '%s\t%s\r' "$n" "$n" >> "$WORK/movies/qt_disk.txt"
    done
    HOST_ARGS+=(--host-file "$WORK/movies/qt_disk.txt")
    # 8.6's Sample Movie is Simple Movie behind a compressed header
    [[ " $names " == *" qt3-sample "* && " $names " == *" hc-simple "* ]] &&
        MANIFEST=(--same-frames qt3-sample=hc-simple)
else
    python3 "$ROOT/tests/lib/qtmovie.py" "$WORK/movies" || { echo "FAIL: qtmovie.py"; exit 1; }
    for f in "$WORK"/movies/*.mov; do HOST_ARGS+=(--host-file "$f"); done
    MANIFEST=(--manifest "$WORK/movies/manifest.txt")
fi
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
if [[ -n "$AUDIO" && -n "$DISK" && -s "$WORK/audio.raw" ]]; then
    heard=0.1
    tr '\r' '\n' < "$WORK/movies/qt_disk.txt" | grep -q '^hc-simple' && heard=5
    AUDIO_CMP=(--audio-heard "$WORK/audio.raw" "${QT_DISK_HEARD:-$heard}")
fi
[[ -n "$AUDIO" && ! -s "$WORK/audio.raw" ]] && echo "sound: nothing in the audio dump" && AUDIO_FAIL=1
python3 "$HERE/compare.py" --results "$RESULTS" --expected "$EXPECTED" \
    ${MANIFEST[@]+"${MANIFEST[@]}"} ${AUDIO_CMP[@]+"${AUDIO_CMP[@]}"} ${KNOWN[@]+"${KNOWN[@]}"} $RECORD
cmp_rc=$?
[[ -n "${AUDIO_FAIL:-}" ]] && cmp_rc=1
if [[ $cmp_rc -eq 0 ]]; then echo "PASS: QTTest as $NAME"; else echo "FAIL: QTTest differs from $NAME"; fi
exit $cmp_rc
