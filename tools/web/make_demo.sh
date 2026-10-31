#!/bin/bash
# A directory with Executor's tab and a boot disk, ready for
# tools/web/serve.py (or any static host that sends .gz files as gzip).
#
#   tools/web/make_demo.sh OUT_DIR SYSTEM_IMAGE [BUILD_WASM_DIR]
#
# SYSTEM_IMAGE: an HFS image with a System Folder (e.g. 7.5.5.img); the
# demo gets a slim boot disk cut from it (make_boot_disk.py), boot-NAME.
# Open OUT_DIR's index.html?disk=boot-NAME&system=boot-NAME.
set -euo pipefail
OUT="$1"
SRC="$2"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

"$ROOT/tools/web/make_site.sh" "$OUT" "${3:-$ROOT/build-wasm}"
DISK="boot-$(basename "$SRC")"
python3 "$ROOT/tools/web/make_boot_disk.py" "$SRC" "$OUT/$DISK"
gzip -9 -k -f "$OUT/$DISK"
du -sh "$OUT/$DISK.gz" | sed 's/^/  /'
echo "Open: index.html?disk=$DISK&system=$DISK"
