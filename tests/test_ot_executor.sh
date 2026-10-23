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
# --no-aslm: Apple's Shared Library Manager not started (the ASLM Loader
# left out of the extension policy), so OT's glue binds through Executor's
# own (romlib/opentransport/ot_slm.cpp). Either way no Apple ASLM code runs
# for an OT client; the log must show Executor's manager.
#
# KEEP_LOG=PATH keeps the emulator's log.
#
# Usage: tests/test_ot_executor.sh [--port N] [--core uae|mame-68k] [--no-aslm]
set -uo pipefail

PORT=18394
CORE=""
NO_ASLM=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) PORT="$2"; shift 2 ;;
        --core) CORE="$2"; shift 2 ;;
        --no-aslm) NO_ASLM=1; shift ;;
        --timeout) shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"
STORAGE="${MACEMU_STORAGE:-$HOME/storage}"
MPW=tools/mpw/mpw
TOOL=tests/guest/ot/OTTest.bin
[[ -f "$STORAGE/images/mpw.img" ]] || { echo "SKIP: no MPW disk (tools/mpw/mpw setup)"; exit 77; }
[[ -f "$STORAGE/images/tests-7.5.5.img" ]] || { echo "SKIP: no tests-7.5.5.img (tools/mpw/mpw setup)"; exit 77; }
[[ -x build/mac-phoenix ]] || { echo "SKIP: no build/mac-phoenix"; exit 77; }
[[ -f "$TOOL" ]] || { echo "FAIL: $TOOL missing"; exit 1; }

export MPW_STATE=$(mktemp -d ${TMPDIR:-/tmp}/ottest.XXXXXX)
export MPW_PORT=$PORT
export MPW_SYSTEM="test-ot${CORE:+-$CORE}${NO_ASLM:+-noaslm}-7.5.5.img"
cleanup() {
    $MPW stop
    [[ -n "${ECHO_PID:-}" ]] && kill "$ECHO_PID" 2>/dev/null
    [[ -n "${KEEP_LOG:-}" ]] && cp "$MPW_STATE/emulator.log" "$KEEP_LOG"
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

echo "=== Open Transport (Executor${CORE:+, $CORE}${NO_ASLM:+, no ASLM}) ==="
if [[ -n "$NO_ASLM" ]]; then
    # The policy override sits beside the System Folder's clean copy, which
    # the first start extracts.
    $MPW start ${CORE:+--core $CORE} >/dev/null || { echo "FAIL: session did not start"; exit 1; }
    $MPW stop >/dev/null
    grep -v -e "^Shared Library Manager" -e "AINI -32700" src/executor/res/extension-policy.txt \
        >"$STORAGE/executor-systems/$MPW_SYSTEM.clean/extension-policy.txt"
fi
$MPW start ${CORE:+--core $CORE} --emu=--executor-ot --emu=--executor-debug --emu=ot \
    || { echo "FAIL: session did not start"; exit 1; }

out=$($MPW run --timeout 240 -C "$HOST/ot" 'OTTest.bin')
echo "$out" | sed 's/^/    /'

FAILS=0
for c in gestalt init utils dns_sync dns_async iface tcp_blocking tcp_nonblocking \
         tcp_async tcp_refused tcp_listen udp atomics tasks options udp_more cancel \
         dns_addr misc inet_misc; do
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

if [[ -n "$NO_ASLM" ]]; then
    want="Shared Library Manager: Executor's"
else
    want="Apple's ASLM: binder wrapped"
fi
if grep -q "\[ot\] $want" "$MPW_STATE/emulator.log" \
        && grep -q "\[ot\] TLibraryManager .* for the client" "$MPW_STATE/emulator.log"; then
    echo "PASS slm ($want; the client's manager is Executor's)"
else
    echo "FAIL slm (expected \"$want\" and a manager of Executor's)"
    FAILS=$((FAILS + 1))
fi

echo "=== $FAILS failed ==="
[[ $FAILS == 0 ]]
