#!/usr/bin/env bash
#
# Build a vendored copy of wolfSSL with all the legacy crypto enabled
# that the net-bridge MITM TLS listener needs to talk to classic-Mac
# browsers (Netscape 2/3/4, MSIE 4.5 Mac).
#
# Why wolfSSL and not OpenSSL: OpenSSL 3.x truly removed the export-grade
# (40-bit RC4/RC2/DES) ciphers and SSLv2 — even with `enable-weak-ssl-
# ciphers` and the legacy provider, those cipher suites are gone and
# can't be brought back. wolfSSL keeps them as compile-time toggles.
#
# Result: <vendor>/wolfssl-legacy/{include,lib}/ ready for net-bridge's
# build.rs to link against (WOLFSSL_DIR=<install-dir>).
#
# One-time setup. Subsequent net-bridge builds pick it up via
# WOLFSSL_DIR env var (see CMakeLists.txt + net-bridge/build.rs).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CRATE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
VENDOR_DIR="$CRATE_DIR/vendor"
INSTALL_DIR="$VENDOR_DIR/wolfssl-legacy"
BUILD_DIR="$VENDOR_DIR/wolfssl-build"
WOLFSSL_VER="5.9.1-stable"
TARBALL="wolfssl-${WOLFSSL_VER}.tar.gz"
URL="https://github.com/wolfSSL/wolfssl/archive/refs/tags/v${WOLFSSL_VER}.tar.gz"

have_wolfssl() {
    [ -f "$INSTALL_DIR/include/wolfssl/ssl.h" ] && \
    [ -f "$INSTALL_DIR/lib/libwolfssl.a" ]
}

if have_wolfssl; then
    echo "wolfSSL already built at: $INSTALL_DIR"
    echo "Remove the directory to force a rebuild."
    exit 0
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# --- Download ---
if [ ! -f "$TARBALL" ]; then
    echo "Downloading $URL"
    curl -fL --proto '=https' --tlsv1.2 -o "$TARBALL" "$URL"
fi

SRC_DIR="$BUILD_DIR/wolfssl-${WOLFSSL_VER}"
rm -rf "$SRC_DIR"
tar -xzf "$TARBALL"
cd "$SRC_DIR"

# --- autogen (release tarballs don't always ship configure) ---
./autogen.sh

# --- Configure ---
# All the legacy bits. NN3.0.4 export Mac may only offer 40-bit RC4 and
# 40-bit RC2; NN4 / MSIE 4.5 add 3DES and IDEA. We enable everything.
#
# `--enable-static --disable-shared` so net-bridge gets a self-contained
# binary with no runtime libwolfssl.so dependency.
#
# `--enable-keygen` so we can mint leaf certs through wolfCrypt later if
# we want to drop the openssl crate dep entirely (deferred).
#
# `--disable-examples` skips wolfCLU / sample servers we don't ship.
./configure \
    --prefix="$INSTALL_DIR" \
    --enable-static \
    --disable-shared \
    --disable-examples \
    --disable-crypttests \
    --enable-oldtls \
    --enable-sslv3 \
    --enable-tls13 \
    --enable-arc4 \
    --enable-rc2 \
    --enable-des3 \
    --enable-des3-tls-suites \
    --enable-md5 \
    --enable-keygen \
    --enable-certgen \
    --enable-certreq \
    --enable-certext \
    --enable-opensslextra \
    --enable-opensslall \
    --enable-singlethreaded \
    --enable-sni \
    --enable-secure-renegotiation \
    CFLAGS="-fPIC -DWOLFSSL_STATIC_RSA -DWOLFSSL_ALLOW_TLS_SHA1 -DOPENSSL_EXTRA"

# --- Build ---
make -j"$(nproc)"

# --- Install (libs + headers only; no docs) ---
make install

cat <<EOF

==========================================================================
wolfSSL ${WOLFSSL_VER} installed to:
  ${INSTALL_DIR}

To build net-bridge against it (once per shell session, or add to .envrc):

  export WOLFSSL_DIR="${INSTALL_DIR}"

Then from ${CRATE_DIR}:

  cargo build

CMake-driven builds pick this up automatically via the same logic that
used to set OPENSSL_DIR.
==========================================================================
EOF
