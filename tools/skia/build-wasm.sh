#!/usr/bin/env bash
# Build Skia from source for the wasm front end (docs/executor/HiDPI.md),
# in place of fetch.sh's prebuilt: JetBrains' skia-pack wasm package is
# built with Emscripten's newer wasm-native setjmp/longjmp + exceptions
# model, which our wasm build's older JS-polyfill model can't link
# against (no local flag bridges the two; see HiDPI.md's "Library"
# section). Building with our own emsdk avoids the mismatch.
#
# A minimal config: core, paths, fonts/glyphs from outlines (FreeType),
# raster backend only -- no GPU, no image codecs (phase 1 draws from
# in-memory font bytes via SkData, never decodes an image). FreeType's
# own color-emoji glyph support still pulls in libpng+zlib unconditionally
# (src/third_party/freetype2/BUILD.gn), so those are built too, minimally.
#
#   source ~/emsdk/emsdk_env.sh   # em++/emcc/emar on PATH; EMSDK set
#   tools/skia/build-wasm.sh [DEST]
#     DEST: default ~/storage/deps/skia-m144-wasm-wasm (fetch.sh's own
#     wasm destination -- this replaces its content, same layout, so
#     -DEXECUTOR_SKIA=ON's existing SKIA_DIR default picks it up as-is)
#
# Prints the directory; pass it as -DSKIA_DIR=... if DEST was overridden.
set -euo pipefail

if [[ -z "${EMSDK:-}" ]]; then
    echo "build-wasm.sh: EMSDK not set -- source ~/emsdk/emsdk_env.sh first" >&2
    exit 2
fi

# Pinned to the same Skia revision as fetch.sh's release (m144-e2e6623374-4:
# milestone 144, short commit e2e6623374) plus the two third-party
# externals FreeType's BUILD.gn always pulls in (libpng, zlib), at the
# revisions DEPS pins for that Skia commit.
SKIA_COMMIT=e2e66233743d048dc880203b6598fd34d94d9765
FREETYPE_COMMIT=1518bc83d26b434031bd12c706ac3c7dab3902fd
LIBPNG_COMMIT=49363adcfaf098748d7a4c8c624ad8c45a8c3a86
ZLIB_COMMIT=646b7f569718921d7d4b5b8e22572ff6c76f2596

SRC="$HOME/storage/deps/skia-wasm-src"
dest=${1:-$HOME/storage/deps/skia-m144-wasm-wasm}

clone_at() {
    local dir=$1 url=$2 commit=$3
    if [[ -f "$dir/.at-$commit" ]]; then
        return
    fi
    rm -rf "$dir"
    mkdir -p "$dir"
    git -C "$dir" init -q
    git -C "$dir" remote add origin "$url"
    git -C "$dir" fetch --depth 1 origin "$commit" >&2
    git -C "$dir" checkout -q FETCH_HEAD
    touch "$dir/.at-$commit"
}

clone_at "$SRC" https://github.com/google/skia.git "$SKIA_COMMIT"
clone_at "$SRC/third_party/externals/freetype" \
    https://chromium.googlesource.com/chromium/src/third_party/freetype2.git "$FREETYPE_COMMIT"
clone_at "$SRC/third_party/externals/libpng" \
    https://skia.googlesource.com/third_party/libpng.git "$LIBPNG_COMMIT"
clone_at "$SRC/third_party/externals/zlib" \
    https://chromium.googlesource.com/chromium/src/third_party/zlib "$ZLIB_COMMIT"

cd "$SRC"
[[ -x bin/gn ]] || python3 bin/fetch-gn >&2

# core, paths, fonts/glyphs from outlines, raster only: no GPU (ganesh/
# graphite/gl/webgl/angle/vulkan/metal/dawn/webgpu), no image codecs (jpeg/
# webp/avif/gav1/dng/piex/wuffs decode or encode -- libpng stays, FreeType's
# own BUILD.gn requires it unconditionally), no SVG/expat, no skottie/pdf,
# no text shaping (harfbuzz/icu -- draw_run in hidpi_skia.cpp positions
# glyphs itself via drawGlyphs, no shaping needed), no WOFF2/OT-SVG fonts
# (not needed for TrueType/sfnt outlines already in memory). -fno-exceptions,
# not -sSUPPORT_LONGJMP=wasm: the JS-polyfill default matches our own wasm
# build's model (see HiDPI.md).
args=(
    is_official_build=true
    is_component_build=false
    target_cpu=\"wasm\"
    skia_emsdk_dir=\"$EMSDK\"
    skia_enable_tools=false
    skia_enable_ganesh=false
    skia_enable_graphite=false
    skia_use_gl=false
    skia_use_webgl=false
    skia_use_angle=false
    skia_use_vulkan=false
    skia_use_metal=false
    skia_use_direct3d=false
    skia_use_dawn=false
    skia_use_webgpu=false
    skia_use_egl=false
    skia_use_libjpeg_turbo_decode=false
    skia_use_libjpeg_turbo_encode=false
    skia_use_libpng_decode=false
    skia_use_libpng_encode=false
    skia_use_libwebp_decode=false
    skia_use_libwebp_encode=false
    skia_use_wuffs=false
    skia_use_dng_sdk=false
    skia_use_piex=false
    skia_use_libavif=false
    skia_use_expat=false
    skia_enable_svg=false
    skia_enable_skottie=false
    skia_enable_pdf=false
    skia_use_harfbuzz=false
    skia_use_icu=false
    skia_enable_skshaper=false
    skia_enable_skparagraph=false
    skia_use_freetype=true
    skia_use_system_freetype2=false
    skia_use_system_zlib=false
    skia_use_zlib=false
    skia_use_system_expat=false
    skia_use_system_libjpeg_turbo=false
    skia_use_system_libpng=false
    skia_use_system_libwebp=false
    skia_use_system_icu=false
    skia_use_lua=false
    skia_enable_fontmgr_custom_directory=false
    skia_enable_fontmgr_custom_embedded=false
    skia_enable_fontmgr_custom_empty=true
    skia_enable_fontmgr_android=false
    skia_enable_fontmgr_fontconfig=false
    skia_use_fonthost_mac=false
    skia_use_fontconfig=false
    skia_use_freetype_woff2=false
    skia_use_freetype_svg=false
    extra_cflags_cc=[\"-fno-exceptions\"]
)
./bin/gn gen out/wasm --args="${args[*]}" >&2
ninja -C out/wasm -j3 skia >&2

rm -rf "$dest"
mkdir -p "$dest/out/Release-wasm-wasm"
cp out/wasm/*.a "$dest/out/Release-wasm-wasm/"
cp -r include "$dest/include"
touch "$dest/.complete"
echo "$dest"
