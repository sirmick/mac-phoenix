#!/bin/bash
#
# build_retro68.sh - (Re)build the Retro68 toolchain with 68k + PPC + Carbon
# targets, self-contained inside this project tree.
#
# Installs to:
#   <project>/toolchain/retro68/          (prefix: gcc, binutils, RIncludes)
#   <project>/toolchain/retro68-build/    (intermediate build artifacts)
#
# Source lookup order:
#   1. $RETRO68 env var, if set and valid
#   2. <project>/toolchain/retro68-src/
#   3. $HOME/Retro68 (fallback — won't be cloned)
# If none exist, we clone into <project>/toolchain/retro68-src/.
#
# Usage:
#   provisioning/build_retro68.sh               # 68k + PPC + Carbon
#   provisioning/build_retro68.sh --no-carbon   # pass through extra flags
#
# Long-running: ~20-40 min. Rerunning is incremental (same build dir).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC_DIR="$ROOT/toolchain"
PREFIX="$TC_DIR/retro68"
BUILD_DIR="$TC_DIR/retro68-build"
SRC_DEFAULT="$TC_DIR/retro68-src"

# Resolve source dir.
if [[ -n "${RETRO68:-}" && -x "$RETRO68/build-toolchain.bash" ]]; then
    SRC="$RETRO68"
elif [[ -x "$SRC_DEFAULT/build-toolchain.bash" ]]; then
    SRC="$SRC_DEFAULT"
elif [[ -x "$HOME/Retro68/build-toolchain.bash" ]]; then
    SRC="$HOME/Retro68"
else
    echo "Cloning Retro68 into $SRC_DEFAULT..."
    mkdir -p "$TC_DIR"
    git clone --recursive https://github.com/autc04/Retro68.git "$SRC_DEFAULT"
    SRC="$SRC_DEFAULT"
fi

PATCHES_DIR="$ROOT/provisioning/retro68-patches"
if [[ -d "$PATCHES_DIR" ]] && compgen -G "$PATCHES_DIR/*.patch" > /dev/null; then
    echo "=== Applying Retro68 patches from $PATCHES_DIR ==="
    for p in "$PATCHES_DIR"/*.patch; do
        if git -C "$SRC" apply --check --reverse "$p" >/dev/null 2>&1; then
            echo "  [skip] $(basename "$p") — already applied"
        elif git -C "$SRC" apply --check "$p" >/dev/null 2>&1; then
            echo "  [apply] $(basename "$p")"
            git -C "$SRC" apply "$p"
        else
            echo "  [warn] $(basename "$p") does not apply cleanly — skipping" >&2
        fi
    done
    echo
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "=== Retro68 build (68k + PPC + Carbon) ==="
echo "Source:  $SRC"
echo "Build:   $BUILD_DIR"
echo "Prefix:  $PREFIX"
echo

bash "$SRC/build-toolchain.bash" --prefix="$PREFIX/" "$@"

echo
echo "=== Done ==="
echo "Toolchain binaries:"
ls "$PREFIX/bin/" | grep -E '^(m68k|powerpc)-apple-macos-gcc$' || true
echo
echo "Add to PATH:"
echo "  export PATH=\"$PREFIX/bin:\$PATH\""
