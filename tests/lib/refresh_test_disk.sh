#!/bin/bash
#
# refresh_test_disk.sh NAME
#
# A fresh copy of a disk image for a test to boot (and write to):
# ~/storage/images/test-NAME.img, its path echoed. The source is the built
# image NAME.img when provisioning/build.py made it (NAME.img.json beside it:
# tests-7.5.5, tests-7.6.1, 7.5.5 ...; built first if it isn't there yet),
# else the hand-made baseline NAME.img.orig (macos-9.0.4, system-6.0.8: the
# Systems not baked yet). Executor's System Folder extracted from an earlier
# copy (executor-systems/test-NAME.img{,.clean}) is removed, so a new image's
# System is used. Exits 77 (ctest SKIP) if there's no source. --reflink=auto:
# CoW filesystems avoid a real byte copy.
#
# Usage in a test script:
#     SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
#     DISK="${MACEMU_DISK:-$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" tests-7.5.5)}"
#
set -euo pipefail

NAME="${1:?usage: refresh_test_disk.sh NAME}"
STORAGE="${MACEMU_STORAGE:-$HOME/storage}"
IMAGES="$STORAGE/images"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="$IMAGES/test-${NAME}.img"

if [[ -f "$REPO/provisioning/images/$NAME.json" ]]; then
    # Built from snapshots: (re)build if missing or not up to date.
    python3 "$REPO/provisioning/build.py" "$NAME" >&2 || { echo "refresh_test_disk: build of $NAME failed" >&2; exit 77; }
    SRC="$IMAGES/$NAME.img"
else
    SRC="$IMAGES/$NAME.img.orig"
fi

if [[ ! -f "$SRC" ]]; then
    echo "refresh_test_disk: no source: $SRC" >&2
    exit 77
fi

rm -rf "$STORAGE/executor-systems/test-${NAME}.img" "$STORAGE/executor-systems/test-${NAME}.img.clean"
cp --reflink=auto -f "$SRC" "$DEST"
echo "$DEST"
