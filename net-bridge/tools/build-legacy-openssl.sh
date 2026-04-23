#!/usr/bin/env bash
#
# Build a vendored copy of OpenSSL 3.0.13 with SSLv3 support and weak
# ciphers re-enabled, for use by the net-bridge MITM TLS listener.
#
# Why: Ubuntu's stock libssl-dev has SSLv3 compiled out and RC4/3DES
# stripped from the cipher list. The classic-Mac browsers the MITM
# proxy targets (MSIE 4.5 Mac, Netscape 3.x/4.x) need exactly those
# protocols and ciphers. This script produces a static libssl we can
# link into net-bridge without touching the system library.
#
# One-time setup. Subsequent net-bridge builds pick it up via
# OPENSSL_DIR / OPENSSL_STATIC env vars (see bottom of script).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CRATE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
VENDOR_DIR="$CRATE_DIR/vendor"
INSTALL_DIR="$VENDOR_DIR/openssl-legacy"
BUILD_DIR="$VENDOR_DIR/openssl-build"
OPENSSL_VER="3.0.13"
TARBALL="openssl-$OPENSSL_VER.tar.gz"
URL="https://www.openssl.org/source/$TARBALL"
# SHA256 published on openssl.org for 3.0.13.
EXPECTED_SHA="88525753f79d3bec27d2fa7c66aa0b92b3aa9498dafd93d7cfa4b3780cdae313"

have_openssl() {
    [ -f "$INSTALL_DIR/include/openssl/ssl.h" ] && \
    [ -f "$INSTALL_DIR/lib/libssl.a" ] && \
    [ -f "$INSTALL_DIR/lib/libcrypto.a" ]
}

if have_openssl; then
    echo "Legacy OpenSSL already built at: $INSTALL_DIR"
    echo "Remove the directory to force a rebuild."
    exit 0
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

if [ ! -f "$TARBALL" ]; then
    echo "Downloading $URL..."
    curl -fsSL -o "$TARBALL.part" "$URL"
    mv "$TARBALL.part" "$TARBALL"
fi

echo "Verifying sha256..."
echo "$EXPECTED_SHA  $TARBALL" | sha256sum -c -

rm -rf "openssl-$OPENSSL_VER"
tar xf "$TARBALL"
cd "openssl-$OPENSSL_VER"

# --- Configure ---
# Linux x86_64 target. Static only (no shared libs) so the resulting
# net-bridge binary is self-contained. The enable-* flags re-add what
# a stock distro build strips out.
./Configure linux-x86_64 \
    --prefix="$INSTALL_DIR" \
    --openssldir="$INSTALL_DIR/ssl" \
    --libdir=lib \
    no-shared \
    no-dso \
    no-tests \
    no-unit-test \
    enable-ssl3 \
    enable-ssl3-method \
    enable-weak-ssl-ciphers \
    enable-legacy \
    enable-deprecated

# --- Build ---
make -j"$(nproc)"

# --- Install (software only; skip docs/manpages) ---
make install_sw

cat <<EOF

==========================================================================
Legacy OpenSSL ${OPENSSL_VER} installed to:
  ${INSTALL_DIR}

To build net-bridge against it (once per shell session, or add to .envrc):

  export OPENSSL_DIR="${INSTALL_DIR}"
  export OPENSSL_STATIC=1

Then from ${CRATE_DIR}:

  cargo build
==========================================================================
EOF
