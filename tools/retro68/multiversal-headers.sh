#!/bin/bash
# multiversal-headers.sh DIR M68K-GCC [PPC-GCC]
#
# The Multiversal Interfaces from Executor's API definitions
# (executor/core/multiversal, `make-multiverse.rb -G CIncludes`) for building
# Mac programs with Retro68 against them alone:
#   DIR/out/CIncludes, RIncludes, lib68k (glue), libppc (with PPC-GCC)
#   DIR/include      the toolchain's own headers (newlib, Retro68's) without
#                    whatever interfaces it has installed, then ours: use
#                    -nostdinc -isystem $(gcc -print-file-name=include)
#                    -isystem DIR/include, and -L DIR/out/lib68k
#   DIR/stamp        touched when done
# The generator compiles the glue with the same gcc (found on PATH).
set -euo pipefail
DIR="${1:?usage: multiversal-headers.sh DIR M68K-GCC [PPC-GCC]}"
GCC="${2:?usage: multiversal-headers.sh DIR M68K-GCC [PPC-GCC]}"
PPC_GCC="${3:-}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
MV="$REPO/executor/core/multiversal"

GENPATH="$(dirname "$GCC"):$PATH"
[[ -n "$PPC_GCC" ]] && GENPATH="$(dirname "$PPC_GCC"):$GENPATH"
rm -rf "$DIR/out" "$DIR/include" "$DIR/stamp"
mkdir -p "$DIR"
if ! (cd "$MV" && PATH="$GENPATH" ruby make-multiverse.rb -G CIncludes -o "$DIR/out") \
        > "$DIR/generate.log" 2>&1 || grep -qE 'error|No such file' "$DIR/generate.log"; then
    grep -E 'error|No such file' "$DIR/generate.log" | head -20 >&2
    echo "multiversal-headers.sh: generation failed ($DIR/generate.log)" >&2
    exit 1
fi

SYSINC="$(cd "$(dirname "$GCC")/.." && pwd)/m68k-apple-macos/include"
mkdir -p "$DIR/include"
for f in "$SYSINC"/*; do
    case "$(readlink "$f")" in
        *Interfaces*/CIncludes/*|*universal/CIncludes/*|*multiversal/CIncludes/*) ;;
        *) ln -sf "$f" "$DIR/include/" ;;
    esac
done
ln -sf "$DIR/out/CIncludes/"* "$DIR/include/"
touch "$DIR/stamp"
