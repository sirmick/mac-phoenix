#!/bin/bash
# Executor's tab as a directory to serve: the page (web/executor), the
# module, and .gz copies of the large files (tools/standalone sends them to
# browsers that accept gzip).
#
#   tools/web/make_site.sh OUT_DIR [BUILD_WASM_DIR]
#
# tools/standalone embeds it (its Makefile's site; --site DIR serves one).
set -euo pipefail
OUT="$1"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${2:-$ROOT/build-wasm}"
WASM="$BUILD/src/executor/romlib/config/front-ends/wasm"
[[ -f "$WASM/executor-web.wasm" ]] || { echo "no $WASM/executor-web.wasm: build build-wasm first"; exit 1; }
grep -q '^EXECUTOR_SKIA:BOOL=ON' "$BUILD/CMakeCache.txt" ||
    echo "warning: $BUILD has no Skia (-DEXECUTOR_SKIA=ON): text at 2x will be blurred (docs/executor/WebSite.md)"

mkdir -p "$OUT"
cp "$WASM"/{index.html,main.js,worker.js,log.js,overlay.js,sync.js,net.js,keys.js,settings.js,settings.css,disks.js,disks.css,settings.schema.json,cursor.js,audio.js,executor-web.js,executor-web.wasm} "$OUT/"
for f in executor-web.wasm executor-web.js; do
    gzip -9 -k -f "$OUT/$f"
done
du -sh "$OUT/executor-web.wasm.gz" | sed 's/^/  /'
