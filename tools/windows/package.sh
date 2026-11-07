#!/bin/bash
# The Windows zip: executor-windows-VERSION.zip with the desktop application
# (executor-desktop.exe, cross-built by tools/windows/build.sh, and the Qt
# and C++ runtime DLLs it needs), the Go server with the tab
# (executor-standalone.exe, as executor/server's make dist builds it),
# README.txt (tools/windows/README.txt), the licences and SHA256SUMS.
#
#   tools/windows/package.sh [--out DIR] [--wasm-build DIR] [--no-build]
#
# --out: where the zip goes (default ~/storage/tmp/windows-try); beside it,
# executor-windows-VERSION-debug/executor-desktop.exe, the unstripped
# executable tools/windows/symbolize.sh reads crash reports' stacks with.
# --wasm-build: the WebAssembly build the server embeds (default build-wasm,
# else the main checkout's). Smoke test of the zip: tools/windows/smoke.sh.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$HOME/storage/tmp/windows-try
WASM_BUILD=$ROOT/build-wasm
BUILD_IT=1
while [ $# -gt 0 ]; do
    case $1 in
        --out) OUT=$2; shift ;;
        --wasm-build) WASM_BUILD=$2; shift ;;
        --no-build) BUILD_IT= ;;
        *) echo "usage: $0 [--out DIR] [--wasm-build DIR] [--no-build]" >&2; exit 2 ;;
    esac
    shift
done
[ -f "$WASM_BUILD/executor/frontends/wasm/executor-web.wasm" ] || WASM_BUILD=$HOME/src/mac-phoenix/build-wasm
[ -f "$WASM_BUILD/executor/frontends/wasm/executor-web.wasm" ] || { echo "no wasm build (--wasm-build)" >&2; exit 1; }

DEPS=${WINDOWS_DEPS:-$HOME/storage/deps/windows}
LLVM_MINGW=$DEPS/llvm-mingw-20231128-ucrt-ubuntu-20.04-x86_64
QT=$DEPS/qt/6.8.2/llvm-mingw_64
BUILD=$ROOT/build-windows
VERSION=$(git -C "$ROOT" describe --always --dirty --tags)
NAME=executor-windows-$VERSION

[ -n "$BUILD_IT" ] && "$ROOT/tools/windows/build.sh" "$BUILD"

STAGE=$(mktemp -d "${TMPDIR:-$HOME/storage/tmp}/winpkg.XXXXXX")
trap 'rm -rf "$STAGE"' EXIT
P=$STAGE/$NAME
mkdir -p "$P"/{platforms,styles,imageformats,licenses}

echo "== executor-desktop.exe ($VERSION)"
"$LLVM_MINGW/bin/llvm-strip" -o "$P/executor-desktop.exe" "$BUILD/executor-desktop.exe"
for dll in Qt6Core Qt6Gui Qt6Widgets libc++ libunwind; do
    cp "$QT/bin/$dll.dll" "$P/"
done
cp "$LLVM_MINGW/x86_64-w64-mingw32/bin/libwinpthread-1.dll" "$P/"
cp "$QT/plugins/platforms/qwindows.dll" "$QT/plugins/platforms/qoffscreen.dll" "$P/platforms/"
cp "$QT/plugins/styles/qmodernwindowsstyle.dll" "$P/styles/"
cp "$QT/plugins/imageformats/"{qico,qjpeg,qgif}.dll "$P/imageformats/"

# Every DLL any of them imports is in the package or Windows' own
for f in "$P"/*.exe "$P"/*.dll "$P"/*/*.dll; do
    "$LLVM_MINGW/bin/llvm-objdump" -p "$f" | awk '/DLL Name:/ { print $3 }'
done | sort -u | while read -r dll; do
    lower=$(echo "$dll" | tr 'A-Z' 'a-z')
    case $lower in
        api-ms-win-*|kernel32.dll|user32.dll|gdi32.dll|advapi32.dll|shell32.dll|ole32.dll|oleaut32.dll|\
        comdlg32.dll|winspool.drv|uuid.dll|ws2_32.dll|dbghelp.dll|imm32.dll|winmm.dll|version.dll|\
        netapi32.dll|userenv.dll|uxtheme.dll|dwmapi.dll|d3d9.dll|d3d11.dll|d3d12.dll|dxgi.dll|\
        dwrite.dll|d2d1.dll|shlwapi.dll|setupapi.dll|authz.dll|mpr.dll|wtsapi32.dll|ntdll.dll|\
        shcore.dll|crypt32.dll|bcrypt.dll|secur32.dll|iphlpapi.dll|ucrtbase.dll|opengl32.dll|\
        comctl32.dll|cfgmgr32.dll|propsys.dll|hid.dll|dnsapi.dll|ncrypt.dll|avrt.dll) ;;
        *) [ -f "$P/$dll" ] || { echo "missing from the package: $dll" >&2; exit 1; } ;;
    esac
done

echo "== executor-standalone.exe (the tab from $WASM_BUILD)"
make -s -C "$ROOT/executor/server" site WASM_BUILD="$WASM_BUILD"
(cd "$ROOT/executor/server" && CGO_ENABLED=0 GOOS=windows GOARCH=amd64 \
    go build -trimpath -ldflags "-s -w -X main.version=$VERSION" -o "$P/executor-standalone.exe" .)

echo "== README, licences"
sed "s/@VERSION@/$VERSION/g" "$ROOT/tools/windows/README.txt" > "$P/README.txt"
cp "$ROOT/LICENSE" "$P/licenses/MacPhoenix-GPL-2.0.txt"
cp "$ROOT/LICENSE-MIT" "$P/licenses/MacPhoenix-MIT.txt"
cp "$ROOT/LICENSING.md" "$P/licenses/LICENSING.md"
cp "$ROOT/executor/LICENSE-CONTRIBUTIONS" "$P/licenses/Executor-MIT.txt"
cp /usr/share/common-licenses/LGPL-3 "$P/licenses/Qt-LGPL-3.0.txt"
cp /usr/share/common-licenses/GPL-3 "$P/licenses/Qt-GPL-3.0.txt"
cp "$LLVM_MINGW/LICENSE.TXT" "$P/licenses/llvm-mingw-libc++-libunwind-winpthreads.txt"
cat > "$P/licenses/README.txt" <<EOF
Qt 6.8.2 (Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll and the plugins) is used
under the GNU LGPL version 3 (Qt-LGPL-3.0.txt, with Qt-GPL-3.0.txt), as
The Qt Company builds it, unmodified; you may replace these DLLs with your
own build of Qt 6.8. Its source: https://download.qt.io/archive/qt/6.8/6.8.2/
libc++.dll and libunwind.dll (LLVM 17, Apache-2.0 with LLVM exceptions) and
libwinpthread-1.dll (mingw-w64, MIT/ZPL) come from llvm-mingw 20231128:
llvm-mingw-libc++-libunwind-winpthreads.txt; https://github.com/mstorsjo/llvm-mingw
MacPhoenix and Executor: LICENSING.md (GPL-2.0-or-later as a whole; Executor
and code written for MacPhoenix also MIT). Their source at version
$VERSION comes from whoever gave you this zip, on request.
EOF

(cd "$P" && find . -type f ! -name SHA256SUMS | sed 's|^\./||' | sort | xargs sha256sum > SHA256SUMS)

mkdir -p "$OUT"
rm -f "$OUT/$NAME.zip"
python3 - "$STAGE" "$NAME" "$OUT/$NAME.zip" <<'EOF'
import os, sys, zipfile
stage, name, out = sys.argv[1:]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for dirpath, dirs, files in os.walk(os.path.join(stage, name)):
        dirs.sort()
        for f in sorted(files):
            full = os.path.join(dirpath, f)
            z.write(full, os.path.relpath(full, stage))
EOF
mkdir -p "$OUT/$NAME-debug"
cp "$BUILD/executor-desktop.exe" "$OUT/$NAME-debug/executor-desktop.exe"
(cd "$OUT" && sha256sum "$NAME.zip" > "$NAME.zip.sha256")
ls -l "$OUT/$NAME.zip"
cat "$OUT/$NAME.zip.sha256"
