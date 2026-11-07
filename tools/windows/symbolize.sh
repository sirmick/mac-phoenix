#!/bin/bash
# Function names for a Windows crash report's stack (crash-STAMP.txt, from
# executor/frontends/desktop/windows_support.cpp): each
# "executor-desktop.exe+0xOFFSET" line through llvm-symbolizer, against the
# unstripped executable of the same build (tools/windows/package.sh keeps it
# as executor-windows-VERSION-debug/executor-desktop.exe beside the zip).
#
#   tools/windows/symbolize.sh crash-STAMP.txt [executor-windows-VERSION-debug/executor-desktop.exe]
set -euo pipefail
REPORT=$1
EXE=${2:-$(dirname "$0")/../../build-windows/executor-desktop.exe}
LLVM=${LLVM_MINGW:-$HOME/storage/deps/windows/llvm-mingw-20231128-ucrt-ubuntu-20.04-x86_64}
SYMBOLIZER=$LLVM/bin/llvm-symbolizer
[ -x "$SYMBOLIZER" ] || SYMBOLIZER=llvm-symbolizer

# the image base the offsets are from (PE ImageBase)
BASE=$("$LLVM/bin/llvm-objdump" -p "$EXE" | awk '/^ImageBase/ { b = $2 } END { print b }')
grep -oE '#[0-9]+ +[^ ]+\+0x[0-9a-f]+' "$REPORT" | while read -r n frame; do
    mod=${frame%%+*}
    off=${frame##*+}
    if [ "$mod" = "$(basename "$EXE" .debug.exe).exe" ] || [ "$mod" = "$(basename "$EXE")" ] || [ "$mod" = executor-desktop.exe ]; then
        addr=$(printf '0x%x' $((0x$BASE + off - 1)))
        echo "$n $frame"
        "$SYMBOLIZER" --obj="$EXE" --demangle --inlining --pretty-print "$addr" | sed 's/^/      /'
    else
        echo "$n $frame"
    fi
done
