#!/bin/bash
# The desktop application (executor/frontends/desktop) offscreen: it starts,
# the System's shell application (the Finder; Executor's Browser on
# Executor's own System) reaches its event loop, and the screen (the 1x
# frame) and the window (the frame through the widget) are written.
#
#   desktop_finder.sh BINARY WORKDIR [SYSTEM_IMAGE [DISK_IMAGE VOLUME]]
#
# With a System image (Apple's, from the web library), and a disk: the disk
# is mounted from the start, a file written on it (VOLUME:desktop-test, the
# touch request), the image itself as it was and the write in its overlay,
# which a second run finds again. Exit 77 (skipped) when an image isn't
# there.
set -u
BIN=$1
WORK=$2
SYSTEM=${3:-}
DISK=${4:-}
VOLUME=${5:-}

for img in "$SYSTEM" "$DISK"; do
    if [ -n "$img" ] && [ ! -f "$img" ]; then
        echo "SKIP: $img not there"
        exit 77
    fi
done

rm -rf "$WORK"
mkdir -p "$WORK"
args=(--data "$WORK/data" --screenshot "$WORK/screen.png" --screenshot-delay 3 --quit --timeout 120)
[ -n "$SYSTEM" ] && args+=(--system "$SYSTEM")
[ -n "$DISK" ] && args+=(--disk "$DISK" --request "touch $VOLUME:desktop-test")
[ -n "$DISK" ] && sum_before=$(sha1sum < "$DISK")

QT_QPA_PLATFORM=offscreen "$BIN" "${args[@]}" > "$WORK/log.txt" 2>&1
rc=$?
grep '^\[desktop\]' "$WORK/log.txt"

fail() {
    echo "FAIL: $*"
    exit 1
}
[ $rc -eq 0 ] || fail "exit code $rc"
grep -q 'is in its event loop' "$WORK/log.txt" || fail "the application never reached its event loop"
for png in screen.png screen-window.png; do
    [ -f "$WORK/$png" ] || fail "no $png"
    # a screen of one colour compresses to a few hundred bytes
    size=$(stat -c %s "$WORK/$png")
    [ "$size" -gt 4000 ] || fail "$png is $size bytes: blank?"
done
if [ -n "$SYSTEM" ]; then
    grep -q 'Finder is in its event loop' "$WORK/log.txt" || fail "not the Finder"
fi
if [ -n "$DISK" ]; then
    [ "$(sha1sum < "$DISK")" = "$sum_before" ] || fail "$DISK was written"
    grep -q "^\[desktop\] request touch $VOLUME:desktop-test: 0" "$WORK/log.txt" || fail "the touch failed"
    ls "$WORK/data/overlays/"*.overlay > /dev/null 2>&1 || fail "no overlay"
    # again: the overlay's changes are found (a version, blocks)
    QT_QPA_PLATFORM=offscreen "$BIN" --data "$WORK/data" --system "$SYSTEM" --disk "$DISK" \
        --screenshot "$WORK/again.png" --screenshot-delay 1 --quit --timeout 120 > "$WORK/again.txt" 2>&1
    grep "^\[desktop\] $DISK: changes" "$WORK/again.txt"
    grep -q "^\[desktop\] $DISK: changes in .*\.overlay (version [1-9]" "$WORK/again.txt" \
        || fail "the second run didn't find the changes"
    [ "$(sha1sum < "$DISK")" = "$sum_before" ] || fail "$DISK was written"
fi
echo PASS
