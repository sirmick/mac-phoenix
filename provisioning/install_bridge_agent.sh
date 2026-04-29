#!/bin/bash
#
# install_bridge_agent.sh - Install BridgeAgent.bin into
# :System Folder:Startup Items: on a Mac OS HFS disk image so Finder
# auto-launches it at desktop time.
#
# Usage:
#   install_bridge_agent.sh <image1> [image2 ...]
#
# Disks without a :System Folder: are skipped (probably data disks).
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
AGENT_BIN="${BRIDGE_AGENT_BIN:-$ROOT/BridgeAgent/BridgeAgent.bin}"

# BRIDGE_BPATH (env): Mac-style colon path for the per-instance bridge
# dir, e.g. "Host:MacPhoenix:12345". Gets written into
# :System Folder:Preferences:MacPhoenix.cfg so the guest BridgeAgent
# and MacBrowser both know where to read/write IPC files. Empty →
# legacy mode, no cfg written (guest falls back to a default path).
: "${BRIDGE_BPATH:=}"

if [[ $# -eq 0 ]]; then
    echo "Usage: $0 <image1> [image2 ...]" >&2
    exit 2
fi

if [[ ! -f "$AGENT_BIN" ]]; then
    echo "BridgeAgent.bin not found at $AGENT_BIN" >&2
    echo "Build it first: make -C bridge" >&2
    exit 1
fi

# Generate a small text cfg (one key=value per line, LF-terminated)
# describing the host's bridge dir. Written into each System-Folder-
# bearing disk's :Preferences: so whichever the user ends up booting
# from carries the correct pid.
CFG_TMP=""
if [[ -n "$BRIDGE_BPATH" ]]; then
    CFG_TMP="$(mktemp)"
    trap 'rm -f "$CFG_TMP"' EXIT
    # MacPerl + classic apps read this with <FILE>; CR-terminate so
    # both \n-readers (MacPerl in unix mode) and CR-readers (Toolbox
    # FSRead loops) see one line.
    printf 'bridge_dir=%s\r' "$BRIDGE_BPATH" > "$CFG_TMP"
fi

command -v hmount >/dev/null || { echo "hfsutils not installed (apt install hfsutils)" >&2; exit 1; }

humount_all() { humount >/dev/null 2>&1 || true; }
# Combined cleanup: unmount any mounted HFS volume and rm the cfg
# tempfile. Built up incrementally so a partial trap doesn't lose
# the other handler.
cleanup() {
    humount_all
    [[ -n "$CFG_TMP" ]] && rm -f "$CFG_TMP"
}
trap cleanup EXIT

for img in "$@"; do
    name="$(basename "$img")"
    if [[ ! -f "$img" ]]; then
        echo "SKIP $name (not found)"
        continue
    fi

    humount_all
    if ! hmount "$img" >/dev/null 2>&1; then
        echo "SKIP $name (not an HFS image)"
        continue
    fi

    # hls returns exit 0 for missing dirs when stderr is redirected, so probe
    # existence by parsing `hls -1d` output instead.
    if [[ -z "$(hls -1d ':System Folder' 2>/dev/null)" ]]; then
        echo "SKIP $name (no System Folder — probably a data disk)"
        continue
    fi

    if [[ -z "$(hls -1d ':System Folder:Startup Items' 2>/dev/null)" ]]; then
        hmkdir ":System Folder:Startup Items" 2>/dev/null || true
    fi

    # Upgrade path: hfsutils' hdel can't remove the agent once Finder has
    # launched it (a catalog quirk we can't undo from Linux), and hcopy
    # refuses to overwrite. Workaround: hrename moves it to :Trash: under a
    # unique name, freeing the original path for a fresh hcopy. The orphan
    # lives in the Trash for the user to empty at their leisure; since it's
    # no longer in Startup Items, Finder won't launch it.
    if [[ -n "$(hls -1d ':System Folder:Startup Items:BridgeAgent' 2>/dev/null)" ]]; then
        # HFS filename limit is 31 chars. "Old-BA-<epoch>.<rand>" stays well
        # under while still making collisions in the Trash unlikely.
        stamp="$(date +%s).$RANDOM"
        hrename ":System Folder:Startup Items:BridgeAgent" \
                ":Trash:Old-BA-$stamp" >/dev/null
    fi

    hcopy -m "$AGENT_BIN" ":System Folder:Startup Items:BridgeAgent"
    echo "INSTALLED BridgeAgent → $name:System Folder:Startup Items:"

    # Write per-instance config so the guest knows which pid-keyed
    # bridge dir to use. Idempotent overwrite. Skipped if BRIDGE_BPATH
    # wasn't passed (legacy callers).
    if [[ -n "$CFG_TMP" ]]; then
        if [[ -z "$(hls -1d ':System Folder:Preferences' 2>/dev/null)" ]]; then
            hmkdir ":System Folder:Preferences" 2>/dev/null || true
        fi
        # hcopy refuses to overwrite — delete any prior cfg first.
        # hdel returns nonzero when the file's missing, which is fine.
        hdel ":System Folder:Preferences:MacPhoenix.cfg" 2>/dev/null || true
        hcopy -t "$CFG_TMP" ":System Folder:Preferences:MacPhoenix.cfg"
        echo "INSTALLED MacPhoenix.cfg ($BRIDGE_BPATH) → $name:System Folder:Preferences:"
    fi
    humount_all
done
