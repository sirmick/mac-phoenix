#!/bin/bash
#
# test_mpw_toolchain.sh — Apple's MPW on Executor through tools/mpw/mpw:
# a session (7.5.5, the MPW disk, ToolServer) on a private state directory,
# port and System copy.
#
#   startup   MPW's Startup ran to the end: {Commands} has the Tools folder
#             (its `Exists` tests are command substitution, MPW.Pipe read
#             back through a second access path: hfsFile.cpp)
#   run       a command's output and {Status}
#   build     tests/guest/mpw-hello with SC and Link, fetched as MacBinary
#   tool      the built tool runs from the Host volume (Link's output gets
#             its date at close: localvolume.cpp), arguments, a file round
#             trip, its exit status
#   error     a compile error fails the build and comes back in gcc form
#   again     a second build after the host deleted files the guest had
#             listed (simplecnidmapper.cpp)
#
# Skips (exit 77) without the MPW disk (tools/mpw/mpw setup) or the 7.5.5
# image.
#
# Usage: tests/test_mpw_toolchain.sh [--port N] [--core uae|mame-68k]
set -uo pipefail

PORT=18392
CORE=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) PORT="$2"; shift 2 ;;
        --core) CORE="$2"; shift 2 ;;
        --timeout) shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"
STORAGE="${MACEMU_STORAGE:-$HOME/storage}"
MPW=tools/mpw/mpw
[[ -f "$STORAGE/images/mpw.img" ]] || { echo "SKIP: no MPW disk (tools/mpw/mpw setup)"; exit 77; }
[[ -f "$STORAGE/images/tests-7.5.5.img" ]] || { echo "SKIP: no tests-7.5.5.img (tools/mpw/mpw setup)"; exit 77; }
[[ -x build/mac-phoenix ]] || { echo "SKIP: no build/mac-phoenix"; exit 77; }

export MPW_STATE=$(mktemp -d ${TMPDIR:-/tmp}/mpwtest.XXXXXX)
export MPW_PORT=$PORT
export MPW_SYSTEM="test-mpw${CORE:+-$CORE}-7.5.5.img"
WORK=$(mktemp -d ${TMPDIR:-/tmp}/mpwtest-src.XXXXXX)
cleanup() {
    $MPW stop
    rm -rf "$MPW_STATE" "$WORK" "$STORAGE/images/$MPW_SYSTEM" \
        "$STORAGE/executor-systems/$MPW_SYSTEM" "$STORAGE/executor-systems/$MPW_SYSTEM.clean"
}
trap cleanup EXIT

FAILS=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; FAILS=$((FAILS + 1)); }

echo "=== MPW toolchain (Executor${CORE:+, $CORE}) ==="
$MPW start ${CORE:+--core $CORE} || { echo "FAIL: session did not start"; exit 1; }

out=$($MPW run 'Echo {Commands}')
if grep -q "MPW:MPW:Tools:" <<<"$out"; then pass startup; else echo "    $out"; fail startup; fi

out=$($MPW run 'Echo one two; Echo three')
status=$?
if [[ $status == 0 && "$out" == $'one two\nthree' ]]; then pass run; else echo "    [$status] $out"; fail run; fi

cp -r tests/guest/mpw-hello "$WORK/"
if $MPW build "$WORK/mpw-hello" && [[ -s "$WORK/mpw-hello/build/hello.bin" ]]; then
    pass build
else
    fail build
fi

HOST="$MPW_STATE/host"
mkdir -p "$HOST/run"
cp "$WORK/mpw-hello/build/hello.bin" "$HOST/run/"
out=$($MPW run -C "$HOST/run" 'hello.bin a "b c"')
status=$?
if [[ $status == 0 ]] && grep -q 'hello from MPW \[a\] \[b c\]' <<<"$out" \
   && grep -q "file round trip ok" <<<"$out"; then
    pass tool
else
    echo "    [$status] $out"; fail tool
fi
$MPW run -C "$HOST/run" 'hello.bin fail' >/dev/null
status=$?
[[ $status == 7 ]] || { echo "    exit status $status, wanted 7"; fail tool_status; }

mkdir -p "$WORK/bad"
printf 'int main(void)\n{\n    return undeclared_thing;\n}\n' >"$WORK/bad/bad.c"
printf '# products: bad\nSC bad.c -o bad.c.o\n' >"$WORK/bad/build.mpw"
out=$($MPW build "$WORK/bad" 2>&1)
status=$?
if [[ $status != 0 ]] && grep -q "$WORK/bad/bad.c:3: Error: undefined identifier 'undeclared_thing'" <<<"$out"; then
    pass error
else
    echo "$out" | sed 's/^/    /'; fail error
fi

# The host removes what the last build left; the guest lists the folder again.
rm -f "$HOST/mpw-hello/hello.c.o" "$HOST/mpw-hello/%hello.c.o" "$HOST/mpw-hello/hello" "$HOST/mpw-hello/%hello"
if $MPW build "$WORK/mpw-hello" >/dev/null && $MPW run 'Echo still here' | grep -q "still here"; then
    pass again
else
    fail again
fi

echo "=== $FAILS failed ==="
[[ $FAILS == 0 ]]
