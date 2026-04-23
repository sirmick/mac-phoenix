#!/bin/bash
#
# refresh_test_disk.sh IMG_BASE
#
# Copies ~/storage/images/<IMG_BASE>.img.bak to ~/storage/images/test-<IMG_BASE>.img
# and echoes the destination path. Uses --reflink=auto so CoW filesystems
# (btrfs, xfs, apfs) avoid a real byte copy. Exits 77 (ctest SKIP code) if
# the backup is missing.
#
# Usage in a test script:
#     SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
#     DISK="${MACEMU_DISK:-$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" macos-7.5.5)}"
#
set -euo pipefail

IMG_BASE="${1:?usage: refresh_test_disk.sh IMG_BASE}"
SRC="$HOME/storage/images/${IMG_BASE}.img.bak"
DEST="$HOME/storage/images/test-${IMG_BASE}.img"

if [[ ! -f "$SRC" ]]; then
    echo "refresh_test_disk: backup not found: $SRC" >&2
    exit 77
fi

cp --reflink=auto -f "$SRC" "$DEST"
echo "$DEST"
