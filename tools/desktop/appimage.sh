#!/bin/bash
#
# appimage.sh - the desktop application (executor/frontends/desktop) as a
# Linux AppImage, with linuxdeploy and its Qt plugin.
#
# Usage:
#   tools/desktop/appimage.sh [BUILD_DIR [OUT_DIR]]     (default: build, BUILD_DIR)
#   cmake --build build --target executor-desktop-appimage
#   tools/desktop/appimage.sh --test APPIMAGE           run it in a clean debian:stable
#
# BUILD_DIR's executor-desktop goes into an AppDir with Qt's libraries and
# plugins (platforms: xcb, wayland, offscreen; image formats, icon engines,
# platform themes), then Executor-x86_64.AppImage in OUT_DIR. linuxdeploy
# and linuxdeploy-plugin-qt (continuous) are fetched once into
# ~/storage/deps/linuxdeploy (LINUXDEPLOY_DIR); they run extracted, so no
# FUSE is needed. The AppImage needs the glibc of the system it was built
# on or newer (built here on Debian 13: Debian 13, Ubuntu 25.04 and later);
# sound uses the system's libpulse or libasound when there (miniaudio loads
# them), else none.
#
# --test: a debian:stable container with nothing added (docker), the
# AppImage started offscreen on Executor's own System until it is in its
# event loop, a screenshot taken; exit code as the run's.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
DEPS="${LINUXDEPLOY_DIR:-$HOME/storage/deps/linuxdeploy}"

if [[ "${1:-}" == "--test" ]]; then
    IMG="$(realpath "${2:?usage: appimage.sh --test APPIMAGE}")"
    OUT="$(dirname "$IMG")/appimage-test"
    rm -rf "$OUT" && mkdir -p "$OUT"
    if ! docker info >/dev/null 2>&1 && [[ -z "${APPIMAGE_SG:-}" ]] && grep -qw "$(id -un)" <<<"$(getent group docker)"; then
        exec env APPIMAGE_SG=1 sg docker -c "$(printf '%q ' "$0" --test "$IMG")"
    fi
    rc=0
    docker run --rm --user "$(id -u):$(id -g)" -e HOME=/out -e QT_QPA_PLATFORM=offscreen \
        -e APPIMAGE_EXTRACT_AND_RUN=1 -v "$IMG:/Executor.AppImage:ro" -v "$OUT:/out" debian:stable \
        /Executor.AppImage --data /out/data --screenshot /out/screen.png --screenshot-delay 2 \
        --quit --timeout 60 > "$OUT/log.txt" 2>&1 || rc=$?
    grep '^\[desktop\]' "$OUT/log.txt" || tail -20 "$OUT/log.txt"
    ls -la "$OUT"/*.png 2>/dev/null || true
    exit $rc
fi

BUILD="$(realpath "${1:-$REPO/build}")"
OUT="$(realpath -m "${2:-$BUILD}")"
BIN="$BUILD/executor-desktop"
[[ -x "$BIN" ]] || { echo "appimage: no $BIN (cmake --build $BUILD --target executor-desktop)" >&2; exit 1; }

mkdir -p "$DEPS"
for tool in linuxdeploy/linuxdeploy-x86_64.AppImage linuxdeploy-plugin-qt/linuxdeploy-plugin-qt-x86_64.AppImage; do
    f="$DEPS/$(basename "$tool")"
    if [[ ! -x "$f" ]]; then
        curl -sSfL -o "$f" "https://github.com/linuxdeploy/${tool%%/*}/releases/download/continuous/$(basename "$tool")"
        chmod +x "$f"
    fi
done

APPDIR="$BUILD/Executor.AppDir"
rm -rf "$APPDIR"
SRC="$REPO/executor/frontends/desktop"
export PATH="$DEPS:$PATH"
export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE="${QMAKE:-$(command -v qmake6 || echo /usr/lib/qt6/bin/qmake)}"
# The platform plugins beside xcb (the plugin's default): Wayland, and
# offscreen for headless runs and the tests
export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so;libqwayland-egl.so;libqoffscreen.so"
export LDAI_OUTPUT="$OUT/Executor-x86_64.AppImage"
export LINUXDEPLOY_OUTPUT_VERSION="$(git -C "$REPO" describe --always --dirty 2>/dev/null || echo dev)"
mkdir -p "$OUT"
cd "$BUILD"
# What linuxdeploy's excludelist leaves to the system as on every desktop,
# but a system without one (a server, a container) lacks: libglvnd's
# dispatch libraries (they load the system's GL/EGL driver when there is
# one; the window paints in software, a QImage, either way), the X11, xcb
# and Wayland client libraries, fontconfig (and expat), FreeType and HarfBuzz. All are
# client or format libraries, safe to carry.
GLVND=()
for l in libEGL.so.1 libOpenGL.so.0 libGLX.so.0 libGLdispatch.so.0 \
         libX11.so.6 libX11-xcb.so.1 libxcb.so.1 libICE.so.6 libSM.so.6 libwayland-client.so.0 \
         libfontconfig.so.1 libfreetype.so.6 libharfbuzz.so.0 libexpat.so.1 libcom_err.so.2; do
    p="$(/sbin/ldconfig -p | awk -v l="$l" '$1 == l && /x86-64/ && !done { print $NF; done = 1 }')"
    [[ -n "$p" ]] && GLVND+=(--library "$p")
done
# Fonts for a system with none (the application loads them only then):
# DejaVu Sans (its own free licence, beside it)
FONTS="$APPDIR/usr/share/executor/fonts"
mkdir -p "$FONTS"
for f in DejaVuSans.ttf DejaVuSans-Bold.ttf; do
    [[ -f "/usr/share/fonts/truetype/dejavu/$f" ]] && cp "/usr/share/fonts/truetype/dejavu/$f" "$FONTS/"
done
[[ -f /usr/share/doc/fonts-dejavu-core/copyright ]] && cp /usr/share/doc/fonts-dejavu-core/copyright "$FONTS/LICENSE.DejaVu"
linuxdeploy-x86_64.AppImage --appdir "$APPDIR" \
    --executable "$BIN" "${GLVND[@]}" \
    --desktop-file "$SRC/executor-desktop.desktop" \
    --icon-file "$SRC/executor.svg" \
    --plugin qt --output appimage
ls -la "$LDAI_OUTPUT"
