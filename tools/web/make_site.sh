#!/bin/bash
# Executor's tab as a directory to serve: the page (web/executor), the
# module, and .gz copies of the large files (tools/web/serve.py and
# tools/web/server send them to browsers that accept gzip).
#
#   tools/web/make_site.sh OUT_DIR [BUILD_WASM_DIR]
#
# The server's [server] site (docs/executor/WebSite.md); make_demo.sh adds
# a boot disk for a static host.
set -euo pipefail
OUT="$1"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WASM="${2:-$ROOT/build-wasm}/src/executor/romlib/config/front-ends/wasm"
[[ -f "$WASM/executor-web.wasm" ]] || { echo "no $WASM/executor-web.wasm: build build-wasm first"; exit 1; }

mkdir -p "$OUT"
cp "$WASM"/{index.html,main.js,worker.js,overlay.js,sync.js,keys.js,cursor.js,audio.js,executor-web.js,executor-web.wasm} "$OUT/"
for f in executor-web.wasm executor-web.js; do
    gzip -9 -k -f "$OUT/$f"
done
du -sh "$OUT/executor-web.wasm.gz" | sed 's/^/  /'
