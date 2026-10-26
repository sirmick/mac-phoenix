#!/bin/bash
# test_multiversal_retro68.sh — the Multiversal Interfaces as Retro68 sees them.
#
# Generates the C headers and glue library from Executor's API definitions
# (src/executor/multiversal, `make-multiverse.rb -G CIncludes`) and builds
# Mac programs with Retro68's m68k compiler against them alone: Apple's
# Universal Interfaces, if the toolchain has them installed, are left out.
#
#   tests/test_multiversal_retro68.sh [--gcc PATH] [--work DIR]
#
# Builds tests/multiversal/mvtest.c into an application (headers, glue
# library, the calls MacPhoenix added to the definitions) and compiles
# Retro68's own samples when its source tree is next to the toolchain.
# Exit 77 (skip) without Retro68 or ruby.
set -uo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
MV="$REPO/src/executor/multiversal"
GCC=""
WORK="${TMPDIR:-/tmp}/multiversal-retro68.$$"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --gcc)  GCC="$2"; shift 2 ;;
        --work) WORK="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

[[ -n "$GCC" ]] || GCC="$(command -v m68k-apple-macos-gcc || true)"
if [[ -z "$GCC" || ! -x "$GCC" ]]; then
    echo "SKIP: no Retro68 m68k-apple-macos-gcc"; exit 77
fi
if ! command -v ruby >/dev/null; then
    echo "SKIP: no ruby"; exit 77
fi

TOOLCHAIN="$(cd "$(dirname "$GCC")/.." && pwd)"
SYSINC="$TOOLCHAIN/m68k-apple-macos/include"
GCCINC="$("$GCC" -print-file-name=include)"

rm -rf "$WORK"
mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

fail=0
pass() { echo "PASS: $1"; }
failed() { echo "FAIL: $1"; fail=1; }

# 1. Generate. The generator compiles the glue library with the same gcc
#    (found on PATH), so a definition that isn't valid C fails here.
echo "--- generating CIncludes from $MV/defs"
if (cd "$MV" && PATH="$(dirname "$GCC"):$PATH" \
        ruby make-multiverse.rb -G CIncludes -o "$WORK/out") > "$WORK/generate.log" 2>&1 \
   && ! grep -qE 'error|No such file' "$WORK/generate.log"; then
    pass "headers and glue library generated ($(ls "$WORK/out/CIncludes" | wc -l) headers)"
else
    grep -E 'error|No such file' "$WORK/generate.log" | head -20
    failed "generation"
    exit 1
fi

# 2. A header directory: the toolchain's own (newlib, Retro68's) without
#    whatever interfaces are linked in, then ours.
VIEW="$WORK/include"
mkdir -p "$VIEW"
for f in "$SYSINC"/*; do
    case "$(readlink "$f")" in
        *Interfaces*/CIncludes/*|*universal/CIncludes/*|*multiversal/CIncludes/*) ;;
        *) ln -s "$f" "$VIEW/" ;;
    esac
done
ln -sf "$WORK/out/CIncludes/"* "$VIEW/"
# -Wno-pointer-sign: "\p" literals are char[], the Toolbox takes unsigned
# char strings.
CFLAGS=(-nostdinc -isystem "$GCCINC" -isystem "$VIEW" -O2 -Wall -Wno-pointer-sign
        -Werror=implicit-function-declaration)

# 3. mvtest: compile and link, with the generated glue library first.
if "$GCC" "${CFLAGS[@]}" -L "$WORK/out/lib68k" -o "$WORK/mvtest.code.bin" \
        "$REPO/tests/multiversal/mvtest.c" > "$WORK/mvtest.log" 2>&1; then
    "$GCC" "${CFLAGS[@]}" -H -fsyntax-only "$REPO/tests/multiversal/mvtest.c" \
        > "$WORK/includes.log" 2>&1
    if grep -q "$VIEW/Multiverse.h" "$WORK/includes.log"; then
        pass "mvtest.c built ($(stat -c %s "$WORK/mvtest.code.bin") bytes)"
    else
        failed "mvtest.c did not include the generated Multiverse.h"
    fi
else
    head -30 "$WORK/mvtest.log"
    failed "mvtest.c"
fi

# 4. MacPhoenix's own guest programs that build against these headers alone
#    (docs/executor/README.md lists what the others still lack).
for s in MacBrowser/MacBrowser.c tests/guest/mpw-hello/hello.c \
         tests/guest/suite/main.c tests/guest/suite/finder.c \
         tests/guest/suite/keyboard.c tests/guest/suite/net.c \
         tests/guest/suite/netdeep.c; do
    if "$GCC" "${CFLAGS[@]}" -w -Werror=implicit-function-declaration \
            -I "$REPO/src/common/guest" -I "$REPO/src/common/include" \
            -fsyntax-only "$REPO/$s" > "$WORK/guest.log" 2>&1; then
        pass "guest program $s"
    else
        head -10 "$WORK/guest.log"
        failed "guest program $s"
    fi
done

# 5. Retro68's samples, when its source tree is there (toolchain at
#    <retro68>/build/toolchain, samples at <retro68>/src/Samples).
SAMPLES="$TOOLCHAIN/../../src/Samples"
if [[ -d "$SAMPLES" ]]; then
    for s in HelloWorld/hello.c Dialog/dialog.c WDEF/wdef.c WDEF/wdefshell.c \
             Launcher/Launcher.c Raytracer/raytracer.c; do
        [[ -f "$SAMPLES/$s" ]] || continue
        if "$GCC" "${CFLAGS[@]}" -w -Werror=implicit-function-declaration \
                -c -o "$WORK/sample.o" "$SAMPLES/$s" > "$WORK/sample.log" 2>&1; then
            pass "Retro68 sample $s"
        else
            head -10 "$WORK/sample.log"
            failed "Retro68 sample $s"
        fi
    done
else
    echo "(no Retro68 source tree at $SAMPLES: samples not compiled)"
fi

exit $fail
