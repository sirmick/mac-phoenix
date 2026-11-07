#!/bin/bash
# The desktop application (executor/frontends/desktop) offscreen: it starts,
# the System's shell application (the Finder; Executor's Browser on
# Executor's own System) reaches its event loop, and the screen (the 1x
# frame) and the window (the frame through the widget) are written.
#
#   desktop_finder.sh BINARY WORKDIR [SYSTEM_IMAGE [DISK_IMAGE]]
#
# With a System image (Apple's, from the web library), and a disk: the disk
# is mounted from the start, written through its working copy, and the
# image itself stays as it was. Exit 77 (skipped) when an image isn't there.
set -u
BIN=$1
WORK=$2
SYSTEM=${3:-}
DISK=${4:-}

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
[ -n "$DISK" ] && args+=(--disk "$DISK")
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
    ls "$WORK/data/overlays/"*.img > /dev/null 2>&1 || fail "no working copy"
fi
echo PASS
