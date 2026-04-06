#!/bin/bash
# Headless Ghidra driver: import each Mac ROM, run analysis + annotation
# scripts, and export an annotated assembly listing to docs/roms/dissam/<machine>/rom.lst.
#
# Usage:
#   ./export_listing.sh              # all ROMs
#   ./export_listing.sh iici         # single ROM
#
# Override ROM root:
#   ROM_ROOT=/path/to/roms ./export_listing.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DISSAM_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$DISSAM_DIR/../../.." && pwd)"

ROM_ROOT="${ROM_ROOT:-$HOME/storage/roms}"
PROJECT_ROOT="$DISSAM_DIR/ghidra"
GHIDRA_HEADLESS="${GHIDRA_HEADLESS:-/snap/ghidra/35/ghidra_12.0_PUBLIC/support/analyzeHeadless}"

if [ ! -x "$GHIDRA_HEADLESS" ]; then
    echo "error: analyzeHeadless not found at $GHIDRA_HEADLESS" >&2
    echo "       set GHIDRA_HEADLESS env var to override" >&2
    exit 1
fi

# Ensure Java is available (Ghidra snap bundles its own JDK)
if ! command -v java &>/dev/null; then
    GHIDRA_JAVA="/snap/ghidra/current/usr/lib/jvm/java-21-openjdk-amd64/bin"
    if [ -d "$GHIDRA_JAVA" ]; then
        export PATH="$GHIDRA_JAVA:$PATH"
    else
        echo "error: java not found in PATH and no Ghidra-bundled JDK at $GHIDRA_JAVA" >&2
        exit 1
    fi
fi

mkdir -p "$PROJECT_ROOT"

# machine | rom-file-relative-to-ROM_ROOT | base-hex | processor-spec
declare -A ROM_FILE=(
    [se]="256KB ROMs/1987-03 - B2E362A8 - Mac SE.ROM"
    [macii]="256KB ROMs/1987-03 - 97851DB6 - MacII (800k v1).ROM"
    [maciix]="256KB ROMs/1988-09 - 97221136 - Mac II FDHD & IIx & IIcx.ROM"
    [iici]="512KB ROMs/1989-09 - 368CADFE - Mac IIci.ROM"
)
declare -A ROM_BASE=(
    [se]="0x00400000"
    [macii]="0x40800000"
    [maciix]="0x40800000"
    [iici]="0x40800000"
)
declare -A ROM_PROC=(
    [se]="68000:BE:32:default"
    [macii]="68000:BE:32:MC68020"
    [maciix]="68000:BE:32:MC68030"
    [iici]="68000:BE:32:MC68030"
)

process_rom() {
    local machine="$1"
    local rom_file="$ROM_ROOT/${ROM_FILE[$machine]}"
    local base="${ROM_BASE[$machine]}"
    local proc="${ROM_PROC[$machine]}"
    local out_dir="$DISSAM_DIR/$machine"
    local out_lst="$out_dir/rom.lst"

    if [ ! -f "$rom_file" ]; then
        echo "error: ROM not found: $rom_file" >&2
        return 1
    fi

    mkdir -p "$out_dir"

    echo ""
    echo "=================================================="
    echo "  $machine  base=$base  proc=$proc"
    echo "  rom: $rom_file"
    echo "=================================================="

    # Stage the ROM under a safe ASCII name (snap/Ghidra choke on spaces +
    # special chars in import paths on some setups).
    local staged="$PROJECT_ROOT/_staging_${machine}.rom"
    cp -f "$rom_file" "$staged"

    "$GHIDRA_HEADLESS" \
        "$PROJECT_ROOT" "$machine" \
        -import "$staged" \
        -overwrite \
        -loader BinaryLoader \
        -loader-baseAddr "$base" \
        -processor "$proc" \
        -cspec default \
        -scriptPath "$SCRIPT_DIR" \
        -postScript load_rom.py "$machine" \
        -postScript export_listing.py "$out_lst" \
        -deleteProject

    rm -f "$staged"

    if [ -f "$out_lst" ]; then
        local lines
        lines=$(wc -l < "$out_lst")
        echo "  -> $out_lst ($lines lines)"
    else
        echo "  WARN: $out_lst was not produced" >&2
    fi
}

if [ $# -gt 0 ]; then
    for m in "$@"; do
        process_rom "$m"
    done
else
    for m in se macii maciix iici; do
        process_rom "$m"
    done
fi

echo ""
echo "done."
