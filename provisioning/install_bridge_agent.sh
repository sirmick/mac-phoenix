#!/bin/bash
#
# install_bridge_agent.sh - Install BridgeAgent.bin into
# :System Folder:Startup Items: on each test disk image so Finder
# auto-launches it at desktop time.
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
AGENT_BIN="$ROOT/tests/guest/bridge/BridgeAgent.bin"
IMAGES_DIR="${IMAGES_DIR:-$HOME/storage/images}"
IMAGES=(
    "macos-7.5.5.img"
    "macos-7.6.1.img"
    "macos-9.0.4.img"
)

if [[ ! -f "$AGENT_BIN" ]]; then
    echo "BridgeAgent.bin not found at $AGENT_BIN" >&2
    echo "Build it first: make -C tests/guest/bridge" >&2
    exit 1
fi

command -v hmount >/dev/null || { echo "hfsutils not installed (apt install hfsutils)" >&2; exit 1; }

humount_all() { humount >/dev/null 2>&1 || true; }
trap humount_all EXIT

for name in "${IMAGES[@]}"; do
    img="$IMAGES_DIR/$name"
    if [[ ! -f "$img" ]]; then
        echo "SKIP $name (not found)"
        continue
    fi

    echo "=== $name ==="
    humount_all
    hmount "$img" >/dev/null

    if ! hls ":System Folder:Startup Items:" >/dev/null 2>&1; then
        hmkdir ":System Folder:Startup Items" 2>/dev/null || true
    fi

    hdel ":System Folder:Startup Items:BridgeAgent" >/dev/null 2>&1 || true
    hcopy -m "$AGENT_BIN" ":System Folder:Startup Items:BridgeAgent"
    hls -l ":System Folder:Startup Items:BridgeAgent"
    humount_all
done

echo "Done."
