#!/bin/bash
# Bootstrap MAME for MacPhoenix reference emulation.
#
# Three binaries are needed for the classic-Mac family we care about:
#   mac128    - Mac SE (and Mac Plus, Classic, SE FDHD)
#   macii     - Mac II, IIx, IIcx, SE/30, IIfx
#   maciici   - Mac IIci
#
# Fast path: if ~/mame/ already exists with built binaries, symlink them
# into docs/roms/mame/bin/ so run.sh can find them.
#
# Slow path (first time, no ~/mame): clone MAME source (shallow) into
# docs/roms/mame/src/ and build the subtargets. This takes 15-40 minutes
# and several GB of disk.
#
# The bin/, src/, and log/ directories are all gitignored. Only the
# scripts and README are committed.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="$SCRIPT_DIR/bin"
SRC_DIR="$SCRIPT_DIR/src"
LOG_DIR="$SCRIPT_DIR/log"

EXTERNAL_MAME="${EXTERNAL_MAME:-$HOME/mame}"

mkdir -p "$BIN_DIR" "$LOG_DIR"

# ---------------------------------------------------------------------------
# Fast path: reuse ~/mame if it has the binaries we need
# ---------------------------------------------------------------------------
if [ -x "$EXTERNAL_MAME/macse" ] && \
   [ -x "$EXTERNAL_MAME/macii" ] && \
   [ -x "$EXTERNAL_MAME/maciici" ]; then
    echo "using existing MAME at $EXTERNAL_MAME"
    for b in macse macii maciici; do
        ln -sf "$EXTERNAL_MAME/$b" "$BIN_DIR/$b"
    done
    # Share the rompath too — MAME's ROM files are non-redistributable and
    # must already be placed under $EXTERNAL_MAME/roms/ by the user.
    if [ -d "$EXTERNAL_MAME/roms" ] && [ ! -e "$BIN_DIR/roms" ]; then
        ln -s "$EXTERNAL_MAME/roms" "$BIN_DIR/roms"
    fi
    echo "linked:"
    ls -l "$BIN_DIR"
    exit 0
fi

# ---------------------------------------------------------------------------
# Slow path: clone + build
# ---------------------------------------------------------------------------
echo "no existing MAME at $EXTERNAL_MAME — bootstrapping fresh copy"
echo "(set EXTERNAL_MAME=/path/to/mame to skip this)"
echo ""

if ! command -v make >/dev/null 2>&1; then
    echo "error: 'make' not found. install build tools:" >&2
    echo "  sudo apt install build-essential python3 libsdl2-dev \\" >&2
    echo "       libsdl2-ttf-dev libfontconfig-dev libpulse-dev qtbase5-dev" >&2
    exit 1
fi

if [ ! -d "$SRC_DIR/.git" ]; then
    echo "cloning mame (shallow, ~500MB)..."
    git clone --depth 1 https://github.com/mamedev/mame.git "$SRC_DIR"
fi

cd "$SRC_DIR"

NPROC="$(nproc 2>/dev/null || echo 4)"

build_subtarget() {
    local subtarget="$1"
    local source_file="$2"
    echo ""
    echo "=== building $subtarget ($source_file) ==="
    make SUBTARGET="$subtarget" SOURCES="$source_file" -j"$NPROC"
    if [ -x "$SRC_DIR/$subtarget" ]; then
        ln -sf "$SRC_DIR/$subtarget" "$BIN_DIR/$subtarget"
        echo "  -> $BIN_DIR/$subtarget"
    else
        echo "  WARN: $subtarget binary not produced" >&2
    fi
}

build_subtarget macse   src/mame/apple/mac128.cpp
build_subtarget macii   src/mame/apple/macii.cpp
build_subtarget maciici src/mame/apple/maciici.cpp

echo ""
echo "bootstrap complete. binaries:"
ls -l "$BIN_DIR"
echo ""
echo "next steps:"
echo "  1. put MAME-format ROMs under $BIN_DIR/roms/<machine>/"
echo "     (see docs/roms/mame/README.md for expected filenames)"
echo "  2. ./run.sh macse  (or macii, maciix, macse30, maciici)"
