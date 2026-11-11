#!/usr/bin/env bash
# Build Skia from source for the Windows desktop application
# (docs/executor/HiDPI.md), cross-compiled here with llvm-mingw: JetBrains'
# skia-pack Windows package is MSVC-built and can't link into a mingw-w64
# program (libc++ and the Itanium C++ ABI against MSVC's), and Skia's GN
# knows Windows only through MSVC. tools/skia/mingw/skia-mingw.patch
# teaches it mingw (is_mingw/is_msvc). Same revision and minimal config as
# build-wasm.sh: core, paths, fonts from outlines (FreeType, from memory),
# raster only; no GPU, no image codecs, no Windows font manager. Static.
#
#   tools/skia/build-windows.sh [DEST]
#     DEST: default ~/storage/deps/skia-m144-windows-x64, where
#     -DEXECUTOR_SKIA=ON looks on Windows (tools/windows/build.sh turns it
#     on when this is there)
#     LLVM_MINGW: the toolchain (default ~/storage/deps/windows/llvm-mingw-*,
#     as tools/windows/build.sh)
#
# Prints the directory.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
LLVM_MINGW=${LLVM_MINGW:-$(ls -d "$HOME"/storage/deps/windows/llvm-mingw-*-x86_64 2>/dev/null | grep -v '\.tar' | head -1)}
if [[ ! -x "$LLVM_MINGW/bin/x86_64-w64-mingw32-clang++" ]]; then
    echo "build-windows.sh: no llvm-mingw (LLVM_MINGW=$LLVM_MINGW); tools/windows/build.sh fetches it" >&2
    exit 2
fi

# The revisions of build-wasm.sh (fetch.sh's release m144-e2e6623374-4)
SKIA_COMMIT=e2e66233743d048dc880203b6598fd34d94d9765
FREETYPE_COMMIT=1518bc83d26b434031bd12c706ac3c7dab3902fd
LIBPNG_COMMIT=49363adcfaf098748d7a4c8c624ad8c45a8c3a86
ZLIB_COMMIT=646b7f569718921d7d4b5b8e22572ff6c76f2596

SRC="$HOME/storage/deps/skia-windows-src"
dest=${1:-$HOME/storage/deps/skia-m144-windows-x64}

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
# the mingw patch, once (a tree with it already applied is left as it is)
if ! git apply --reverse --check "$HERE/mingw/skia-mingw.patch" 2>/dev/null; then
    git apply "$HERE/mingw/skia-mingw.patch"
fi
[[ -x bin/gn ]] || python3 bin/fetch-gn >&2

bin="$LLVM_MINGW/bin"
args=(
    is_official_build=true
    is_component_build=false
    target_os=\"win\"
    target_cpu=\"x64\"
    cc=\"$bin/x86_64-w64-mingw32-clang\"
    cxx=\"$bin/x86_64-w64-mingw32-clang++\"
    ar=\"$bin/llvm-ar\"
    skia_enable_tools=false
    skia_enable_ganesh=false
    skia_enable_graphite=false
    skia_use_gl=false
    skia_use_vulkan=false
    skia_use_direct3d=false
    skia_use_dawn=false
    skia_use_angle=false
    skia_use_metal=false
    skia_use_webgl=false
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
    skia_use_system_libpng=false
    skia_use_lua=false
    skia_use_xps=false
    skia_enable_fontmgr_win=false
    skia_enable_fontmgr_win_gdi=false
    skia_enable_fontmgr_custom_directory=false
    skia_enable_fontmgr_custom_embedded=false
    skia_enable_fontmgr_custom_empty=true
    skia_enable_fontmgr_fontconfig=false
    skia_enable_fontmgr_android=false
    skia_use_fontconfig=false
    skia_use_freetype_woff2=false
    skia_use_freetype_svg=false
)
./bin/gn gen out/win --args="${args[*]}" >&2
ninja -C out/win -j"${JOBS:-4}" skia >&2

rm -rf "$dest"
mkdir -p "$dest/out/Release-windows-x64"
cp out/win/lib{skia,freetype2,png,zlib,skcms}.a "$dest/out/Release-windows-x64/"
# the public headers, and skcms's that include/core/SkColorSpace.h includes
find include modules -name '*.h' | cpio -pdm --quiet "$dest"
cp LICENSE "$dest/"
echo "$dest"
