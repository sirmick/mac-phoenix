#!/usr/bin/env bash
#
# Build a vendored copy of OpenSSL 1.0.2u with SSLv2 + SSLv3 + every
# weak/export cipher enabled. This is the only SSL library that still
# supports Netscape-3-international-Mac's export-only cipher set
# (40-bit RC4 / RC2 / DES); modern OpenSSL 3.x and wolfSSL 5.x both
# stripped those ciphers.
#
# OpenSSL 1.0.2 EOL'd in 2019. We're shipping it for a hobby MITM
# proxy on a hobby emulator — security threat model is "let pages
# load," not "transport security." Don't expose this on the internet.
#
# Result: <vendor>/openssl-102-legacy/{include,lib}/ ready for
# net-bridge's build.rs to link against (OPENSSL_DIR=<install-dir>,
# OPENSSL_STATIC=1).
#
# One-time setup. Subsequent net-bridge builds pick it up via
# OPENSSL_DIR env var (CMakeLists.txt sets it automatically).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CRATE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
VENDOR_DIR="$CRATE_DIR/vendor"
INSTALL_DIR="$VENDOR_DIR/openssl-102-legacy"
BUILD_DIR="$VENDOR_DIR/openssl-102-build"
OPENSSL_VER="1.0.2u"
TARBALL="openssl-${OPENSSL_VER}.tar.gz"
URL="https://www.openssl.org/source/${TARBALL}"
# SHA256 published on openssl.org for 1.0.2u (the EOL release).
EXPECTED_SHA="ecd0c6ffb493dd06707d38b14bb4d8c2288bb7033735606569d8f90f89669d16"

have_openssl() {
    [ -f "$INSTALL_DIR/include/openssl/ssl.h" ] && \
    [ -f "$INSTALL_DIR/lib/libssl.a" ] && \
    [ -f "$INSTALL_DIR/lib/libcrypto.a" ]
}

if have_openssl; then
    echo "OpenSSL 1.0.2u already built at: $INSTALL_DIR"
    echo "Remove the directory to force a rebuild."
    exit 0
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# --- Download + verify ---
if [ ! -f "$TARBALL" ]; then
    echo "Downloading $URL"
    curl -fL --proto '=https' --tlsv1.2 -o "$TARBALL" "$URL"
fi
echo "$EXPECTED_SHA  $TARBALL" | sha256sum -c -

SRC_DIR="$BUILD_DIR/openssl-${OPENSSL_VER}"
rm -rf "$SRC_DIR"
tar -xzf "$TARBALL"
cd "$SRC_DIR"

# --- Configure ---
# Linux x86_64 target. Static libs + no-shared so net-bridge ends up
# self-contained. enable-ssl2 + enable-ssl3 + enable-weak-ssl-ciphers
# is the trifecta that brings back NN3-export-Mac compatibility.
./Configure linux-x86_64 \
    --prefix="$INSTALL_DIR" \
    --openssldir="$INSTALL_DIR/ssl" \
    no-shared \
    no-dso \
    no-tests \
    enable-ssl2 \
    enable-ssl3 \
    enable-ssl3-method \
    enable-weak-ssl-ciphers \
    enable-rc2 \
    enable-rc4 \
    enable-md2 \
    enable-md5 \
    enable-idea \
    enable-deprecated \
    -fPIC

# --- Build ---
# 1.0.2's makefile isn't reliably parallel-safe; serial is fine
# (~3 min on this box).
make depend
make -j"$(nproc)"

# --- Install (libs + headers only; skip docs/manpages/tests) ---
make install_sw

cat <<EOF

==========================================================================
OpenSSL ${OPENSSL_VER} installed to:
  ${INSTALL_DIR}

To build net-bridge against it (once per shell session, or add to .envrc):

  export OPENSSL_DIR="${INSTALL_DIR}"
  export OPENSSL_STATIC=1

Then from ${CRATE_DIR}:

  cargo build --release

CMake-driven builds set those automatically when this vendor dir exists.

Verify the cipher list includes the export grades:
  ${INSTALL_DIR}/bin/openssl ciphers -v 'EXP:SSLv2:LOW' | head -10
==========================================================================
EOF
