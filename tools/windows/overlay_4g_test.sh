#!/bin/bash
# Build tools/windows/overlay_4g_test.cpp for Windows and run it under Wine
# (docker, tools/windows/wine.Dockerfile) in WORKDIR (default
# ~/storage/tmp/overlay-4g; the 6 GB image is sparse). Exit 0: all passed.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${1:-$HOME/storage/tmp/overlay-4g}
DEPS=${WINDOWS_DEPS:-$HOME/storage/deps/windows}
LLVM=$DEPS/llvm-mingw-20231128-ucrt-ubuntu-20.04-x86_64
QT=$DEPS/qt/6.8.2/llvm-mingw_64
mkdir -p "$WORK/.home"
"$LLVM/bin/x86_64-w64-mingw32-clang++" -std=c++17 -O1 -D_FILE_OFFSET_BITS=64 -D_WIN32_WINNT=0x0A00 \
    -DWIN32_LEAN_AND_MEAN -I"$ROOT/executor/host" -I"$ROOT/executor/core/romlib" \
    -I"$ROOT/executor/core/romlib/include" "$ROOT/tools/windows/overlay_4g_test.cpp" \
    "$ROOT/executor/host/overlay.cpp" -o "$WORK/test.exe"
cp "$QT/bin/libc++.dll" "$QT/bin/libunwind.dll" "$WORK/"
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/work/.home -e LANG=C.UTF-8 -e WINEDEBUG=-all \
    -v "$WORK:/work" macphoenix-wine:13 wine /work/test.exe 2>/dev/null
