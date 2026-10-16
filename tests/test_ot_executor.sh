#!/bin/bash
#
# test_ot_executor.sh — Executor's native Open Transport (romlib/opentransport)
# through Apple's own 68k OT glue: tests/guest/ot/OTTest.bin, an MPW tool
# linked with OpenTransport.o / OpenTransportApp.o / OpenTptInet.o, run in
# ToolServer on an MPW session (tools/mpw/mpw) with --executor-ot.
#
# Cases (one PASS/FAIL line each from the tool): gestalt, init, utils,
# dns_sync, dns_async, iface, tcp_blocking, tcp_nonblocking, tcp_async,
# tcp_refused, tcp_listen, udp; against a host echo server
# (tests/lib/echo_server.py) named in Host:net_echo.txt. Also fails if a
# function the tool binds is reported as not implemented.
#
# Skips (exit 77) without the MPW disk (tools/mpw/mpw setup) or the 7.5.5
# image. Rebuild the tool after changing OTTest.c:
#   tools/mpw/mpw build tests/guest/ot -o tests/guest/ot
#
# Usage: tests/test_ot_executor.sh [--port N] [--core uae|mame-68k]
set -uo pipefail

PORT=18394
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
TOOL=tests/guest/ot/OTTest.bin
[[ -f "$STORAGE/images/mpw-eto22.img" ]] || { echo "SKIP: no MPW disk (tools/mpw/mpw setup)"; exit 77; }
[[ -f "$STORAGE/images/macos-7.5.5.img" ]] || { echo "SKIP: no macos-7.5.5.img"; exit 77; }
[[ -x build/mac-phoenix ]] || { echo "SKIP: no build/mac-phoenix"; exit 77; }
[[ -f "$TOOL" ]] || { echo "FAIL: $TOOL missing"; exit 1; }

export MPW_STATE=$(mktemp -d /tmp/ottest.XXXXXX)
export MPW_PORT=$PORT
export MPW_SYSTEM="test-ot${CORE:+-$CORE}-7.5.5.img"
cleanup() {
    $MPW stop
    [[ -n "${ECHO_PID:-}" ]] && kill "$ECHO_PID" 2>/dev/null
    rm -rf "$MPW_STATE" "$STORAGE/images/$MPW_SYSTEM" \
        "$STORAGE/executor-systems/$MPW_SYSTEM" "$STORAGE/executor-systems/$MPW_SYSTEM.clean"
}
trap cleanup EXIT

HOST="$MPW_STATE/host"
mkdir -p "$HOST/ot"
python3 tests/lib/echo_server.py "$MPW_STATE/echo_port" &
ECHO_PID=$!
for _ in $(seq 1 50); do [[ -s "$MPW_STATE/echo_port" ]] && break; sleep 0.1; done
echo "127.0.0.1 $(cat "$MPW_STATE/echo_port")" >"$HOST/net_echo.txt"
cp "$TOOL" "$HOST/ot/"

echo "=== Open Transport (Executor${CORE:+, $CORE}) ==="
$MPW start ${CORE:+--core $CORE} --emu=--executor-ot --emu=--executor-debug --emu=ot \
    || { echo "FAIL: session did not start"; exit 1; }

out=$($MPW run --timeout 240 -C "$HOST/ot" 'OTTest.bin')
echo "$out" | sed 's/^/    /'

FAILS=0
for c in gestalt init utils dns_sync dns_async iface tcp_blocking tcp_nonblocking \
         tcp_async tcp_refused tcp_listen udp; do
    if grep -qx "PASS $c" <<<"$out"; then
        echo "PASS $c"
    else
        echo "FAIL $c"
        FAILS=$((FAILS + 1))
    fi
done
grep -q "^DONE 0$" <<<"$out" || { echo "FAIL done (the tool did not finish clean)"; FAILS=$((FAILS + 1)); }
if grep -q "\[ot\].*not implemented" "$MPW_STATE/emulator.log"; then
    grep "\[ot\].*not implemented" "$MPW_STATE/emulator.log" | sed 's/^/    /'
    echo "FAIL implemented"
    FAILS=$((FAILS + 1))
fi

echo "=== $FAILS failed ==="
[[ $FAILS == 0 ]]
