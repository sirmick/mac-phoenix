#!/usr/bin/env bash
# Fetch the prebuilt Skia that Executor's high-resolution rendering links
# (docs/executor/HiDPI.md, -DEXECUTOR_SKIA=ON): JetBrains' skia-pack, static
# libraries + headers, BSD-3-Clause (Skia) with FreeType (FTL), libpng,
# zlib, expat inside. Pinned release and SHA-256 below.
#
#   tools/skia/fetch.sh [ARCH] [DEST]
#     ARCH: x64 (default on x86-64), arm64 (default on aarch64), wasm
#     DEST: default ~/storage/deps/skia-m144-<platform>-<arch>
#
# Prints the directory; pass it as -DSKIA_DIR=... (CMake looks there by
# default for this release).
set -euo pipefail

RELEASE=m144-e2e6623374-4
BASE=https://github.com/JetBrains/skia-pack/releases/download/$RELEASE

case "$(uname -m)" in
    aarch64|arm64) default_arch=arm64 ;;
    *) default_arch=x64 ;;
esac
arch=${1:-$default_arch}

case "$arch" in
    x64)   asset=linux-Release-x64;  sha=895628ecaf8f60dee5b4c3b04574a290335408049110a008d79ad4668382a79b ;;
    arm64) asset=linux-Release-arm64; sha=2c66dc45e6ede6fa87682d617a390059894592d3d0cd2d7f72afded830fb3a9e ;;
    wasm)  asset=wasm-Release-wasm;  sha=074f4a5b3e3247bd7422abcc0ddb3e97ea2f92d8cf353d2965c836b5c49e1b21 ;;
    *) echo "usage: $0 [x64|arm64|wasm] [DEST]" >&2; exit 2 ;;
esac

platform=${asset%%-*}
dest=${2:-$HOME/storage/deps/skia-m144-$platform-$arch}
if [[ -f "$dest/.complete" ]]; then
    echo "$dest"
    exit 0
fi

zip="$dest.zip"
mkdir -p "$(dirname "$dest")"
curl -fL --retry 3 -o "$zip" "$BASE/Skia-$RELEASE-$asset.zip" >&2
echo "$sha  $zip" | sha256sum -c - >&2
rm -rf "$dest"
mkdir -p "$dest"
unzip -q "$zip" -d "$dest"
rm -f "$zip"
touch "$dest/.complete"
echo "$dest"
