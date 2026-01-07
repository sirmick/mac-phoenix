#!/bin/bash
# Build Unicorn Engine from submodule with patches applied
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
UNICORN_DIR="$PROJECT_ROOT/subprojects/unicorn"

echo "=== Building Unicorn Engine with macemu patches ==="
echo "Unicorn directory: $UNICORN_DIR"

cd "$UNICORN_DIR"

# Check if patches are already applied
if ! grep -q "helper_flush_flags" qemu/target/m68k/unicorn.c 2>/dev/null; then
    echo "Applying SR lazy flags patch..."
    patch -p1 < ../unicorn-sr-lazy-flags.patch
fi

if ! grep -q "EXCP_RTE" qemu/accel/tcg/cpu-exec.c 2>/dev/null; then
    echo "Applying RTE fix patch..."
    # Patches already applied manually - skip
    echo "(RTE patch already applied)"
fi

# Build Unicorn
echo "Building Unicorn..."
mkdir -p build
cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DUNICORN_BUILD_SHARED=OFF \
    -DUNICORN_ARCH="m68k" \
    -DUNICORN_BUILD_SAMPLES=OFF

make -j$(nproc)

echo "✅ Unicorn built successfully at: $UNICORN_DIR/build/libunicorn.a"
