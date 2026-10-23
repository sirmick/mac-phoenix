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
WASM="${3:-$ROOT/build-wasm}/src/executor/romlib/config/front-ends/wasm"
[[ -f "$WASM/executor-web.wasm" ]] || { echo "no $WASM/executor-web.wasm: build build-wasm first"; exit 1; }

mkdir -p "$OUT"
cp "$WASM"/{index.html,main.js,worker.js,overlay.js,keys.js,cursor.js,audio.js,executor-web.js,executor-web.wasm} "$OUT/"
DISK="boot-$(basename "$SRC")"
python3 "$ROOT/tools/web/make_boot_disk.py" "$SRC" "$OUT/$DISK"
for f in executor-web.wasm executor-web.js "$DISK"; do
    gzip -9 -k -f "$OUT/$f"
done
du -sh "$OUT"/executor-web.wasm.gz "$OUT/$DISK.gz" | sed 's/^/  /'
echo "Open: index.html?disk=$DISK&system=$DISK"
