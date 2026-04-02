#!/bin/bash
# Regenerate precompiled dyngen ops headers for the KPX PPC JIT.
#
# These headers contain x86_64 machine code extracted from compiled
# C++ source files. The dyngen tool reads ELF .o files and emits
# C headers with inline native instruction sequences.
#
# Run this whenever powerpc_registers or powerpc_cpu struct layout changes.
#
# Usage: ./src/cpu/kpx/regen_dyngen_ops.sh

set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

KPX=src/cpu/kpx
OUT=$KPX/dyngen_precompiled
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

DEFS="-DSHEEPSHAVER=1 -DEMULATED_PPC=1 -DPPC_ENABLE_JIT=1 -DPPC_CHECK_INTERRUPTS=1 -DREAL_ADDRESSING=1 -DKPX_MAX_CPUS=1 -DENABLE_DYNGEN=1 -DENABLE_MON=0"
INCS="-I$KPX/compat -I$KPX/include -I$KPX/src -I$OUT"

# Flags required for clean extractable machine code:
#   -fomit-frame-pointer     : no frame pointer setup in extracted code
#   -finline-functions       : inline small functions into ops
#   -fno-exceptions          : no exception handling overhead
#   -fno-reorder-blocks      : predictable instruction sequences
#   -fno-reorder-blocks-and-partition : prevent .cold sections (dyngen can't handle them)
#   -fno-optimize-sibling-calls : prevent tail calls that confuse extraction
#   -g0                      : no debug info in extracted code
DYNGEN_FLAGS="-O2 -fomit-frame-pointer -finline-functions -finline-limit=10000 -fno-exceptions -g0 -fno-reorder-blocks -fno-optimize-sibling-calls -fno-align-functions -fno-stack-protector -fno-reorder-blocks-and-partition"

echo "=== Building dyngen tool ==="
cc -c -o "$TMPDIR/dyngen.o" "$KPX/src/cpu/jit/dyngen.c" $INCS $DEFS -w
c++ -c -o "$TMPDIR/cxxdemangle.o" "$KPX/src/cpu/jit/cxxdemangle.cpp" $INCS -w
c++ -o "$TMPDIR/dyngen" "$TMPDIR/dyngen.o" "$TMPDIR/cxxdemangle.o"

echo "=== Compiling basic-dyngen-ops ==="
c++ $DEFS $INCS $DYNGEN_FLAGS -w -c \
  "$KPX/src/cpu/jit/basic-dyngen-ops.cpp" \
  -o "$TMPDIR/basic-dyngen-ops.o"

echo "=== Generating basic-dyngen-ops-x86_64.hpp ==="
"$TMPDIR/dyngen" -o "$OUT/basic-dyngen-ops-x86_64.hpp" "$TMPDIR/basic-dyngen-ops.o"
echo "  $(wc -l < "$OUT/basic-dyngen-ops-x86_64.hpp") lines"

echo "=== Compiling ppc-dyngen-ops ==="
c++ $DEFS $INCS $DYNGEN_FLAGS -w -c \
  "$KPX/src/cpu/ppc/ppc-dyngen-ops.cpp" \
  -o "$TMPDIR/ppc-dyngen-ops.o"

echo "=== Generating ppc-dyngen-ops-x86_64.hpp ==="
"$TMPDIR/dyngen" -o "$OUT/ppc-dyngen-ops-x86_64.hpp" "$TMPDIR/ppc-dyngen-ops.o"
echo "  $(wc -l < "$OUT/ppc-dyngen-ops-x86_64.hpp") lines"

echo "=== Done ==="
echo "Regenerated headers in $OUT/"
