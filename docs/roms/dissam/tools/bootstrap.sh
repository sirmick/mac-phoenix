#!/bin/bash
# Fetch upstream reverse-engineering tools used by the disassembly pipeline.
# Each repo is cloned under tools/vendor/ and is NOT committed to this repo.
#
# Usage:
#   ./bootstrap.sh           # clone (or skip if already present)
#   ./bootstrap.sh --update  # pull latest
#
# What these provide:
#   - cy384/68k-mac-rom-maps
#       Pre-built symbolic maps for most classic Mac ROMs (SE, MacII, IIci,
#       Classic, LC, Quadra, ...). load_rom.py imports these into Ghidra so
#       every known init routine gets its real Apple name.
#
#   - gm-stack/classic-mac-rom-ghidra-tools
#       Low-memory globals table (lomem_globals.txt) — 388 named addresses
#       in the $0100..$0D00 range. Also various Ghidra helper scripts.
#
#   - ubuntor/m68k_mac_reversing_tools
#       Ghidra scripts for M68K Mac application reversing (jumptable loader,
#       A-trap markup, thunk propagator). Primarily targeted at THINK C /
#       MPW applications but useful reference for ROM analysis.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VENDOR_DIR="$SCRIPT_DIR/vendor"

REPOS=(
    "cy384/68k-mac-rom-maps"
    "gm-stack/classic-mac-rom-ghidra-tools"
    "ubuntor/m68k_mac_reversing_tools"
)

mkdir -p "$VENDOR_DIR"
cd "$VENDOR_DIR"

UPDATE=0
if [ "${1:-}" = "--update" ]; then
    UPDATE=1
fi

for repo in "${REPOS[@]}"; do
    name="${repo##*/}"
    if [ -d "$name/.git" ]; then
        if [ "$UPDATE" = "1" ]; then
            echo "  updating $name..."
            (cd "$name" && git pull --ff-only)
        else
            echo "  $name already present (use --update to pull)"
        fi
    else
        echo "  cloning $repo..."
        git clone --depth 1 "https://github.com/$repo.git" "$name"
    fi
done

echo ""
echo "vendor tools ready at $VENDOR_DIR"
