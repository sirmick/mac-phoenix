#!/bin/bash
# Executor's desktop application for Windows x86-64, cross-built here
# (cmake/ExecutorWindows.cmake): llvm-mingw and Qt's own llvm-mingw build
# of Qt 6, fetched once into ~/storage/deps/windows; moc, rcc and uic are
# the build machine's Qt of the same version (Debian 13: qt6-base-dev-tools,
# 6.8.2).
#
#   tools/windows/build.sh [BUILD_DIR]     # default build-windows
#   -> BUILD_DIR/executor-desktop.exe
#
# HiDPI (Skia) when its Windows build is there (tools/skia/build-windows.sh
# makes it: ~/storage/deps/skia-m144-windows-x64, or SKIA_WINDOWS=DIR);
# without it the application draws at 1x and says so in its log.
#
# Wine smoke test and the zip: tools/windows/package.sh.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${1:-$ROOT/build-windows}
DEPS=${WINDOWS_DEPS:-$HOME/storage/deps/windows}
LLVM_MINGW_RELEASE=20231128          # LLVM 17.0.6: the toolchain Qt 6.8's llvm-mingw build was made with
LLVM_MINGW=$DEPS/llvm-mingw-$LLVM_MINGW_RELEASE-ucrt-ubuntu-20.04-x86_64
QT_VERSION=6.8.2
QT=$DEPS/qt/$QT_VERSION/llvm-mingw_64
mkdir -p "$DEPS"

if [ ! -x "$LLVM_MINGW/bin/x86_64-w64-mingw32-clang++" ]; then
    echo "== llvm-mingw $LLVM_MINGW_RELEASE"
    tarball=llvm-mingw-$LLVM_MINGW_RELEASE-ucrt-ubuntu-20.04-x86_64.tar.xz
    curl -fL -o "$DEPS/$tarball" \
        "https://github.com/mstorsjo/llvm-mingw/releases/download/$LLVM_MINGW_RELEASE/$tarball"
    tar -C "$DEPS" -xf "$DEPS/$tarball"
fi

if [ ! -f "$QT/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    echo "== Qt $QT_VERSION win64_llvm_mingw (aqtinstall)"
    [ -x "$DEPS/venv/bin/aqt" ] || { python3 -m venv "$DEPS/venv" && "$DEPS/venv/bin/pip" install -q aqtinstall; }
    (cd "$DEPS" && ./venv/bin/aqt install-qt windows desktop $QT_VERSION win64_llvm_mingw -O qt)
fi

host_qt=$(/usr/lib/qt6/libexec/moc -v 2>/dev/null | awk '{ print $2 }')
if [ "$host_qt" != "$QT_VERSION" ]; then
    echo "warning: the build machine's moc is Qt ${host_qt:-(none)}, the Windows Qt $QT_VERSION" >&2
fi

SKIA_WINDOWS=${SKIA_WINDOWS:-$HOME/storage/deps/skia-m144-windows-x64}
if [ -f "$SKIA_WINDOWS/out/Release-windows-x64/libskia.a" ]; then
    SKIA=(-DEXECUTOR_SKIA=ON -DSKIA_DIR="$SKIA_WINDOWS")
else
    echo "note: no Windows Skia in $SKIA_WINDOWS (tools/skia/build-windows.sh): HiDPI off" >&2
    SKIA=(-DEXECUTOR_SKIA=OFF)
fi

if [ ! -f "$BUILD/CMakeCache.txt" ]; then
    cmake -S "$ROOT" -B "$BUILD" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/windows/llvm-mingw.cmake" \
        -DLLVM_MINGW="$LLVM_MINGW" -DQT_WINDOWS="$QT" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo "${SKIA[@]}"
else
    cmake "$BUILD" "${SKIA[@]}" > /dev/null   # the version (git describe) again
fi
cmake --build "$BUILD" -j"${JOBS:-4}" --target executor-desktop
ls -l "$BUILD/executor-desktop.exe"
