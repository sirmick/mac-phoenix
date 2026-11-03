#!/bin/bash
# test_executor_ppc_break.sh [BINARY] [PORT]: PowerPC breakpoints in an
# Executor session (--executor-ppc-break, /api/executor/ppc-break;
# docs/executor/PPCTrace.md). CLibTest (tests/guest/clib, PowerPC only) on
# Executor's 7.5.5 System, with
#   - a breakpoint on Executor's fopen from the command line, traced: the
#     registers, the back chain into CLibTest, a trace with disassembly
#     that comes back into CLibTest's fragment;
#   - one at CLibTest's main (its code section's offset, from the PEF) by
#     fragment name, added over the API before the launch: pending, then
#     resolved when the fragment is prepared, hit, reported there with a
#     memory dump.
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BINARY="${1:-${MACEMU_BINARY:-$PROJECT_ROOT/build/mac-phoenix}}"
PORT="${2:-18472}"
source "$(dirname "$0")/lib/ports.sh"; PORT=$(mp_port "$PORT") || exit 1  # per-worktree (docs/Testing.md)
APP="$PROJECT_ROOT/tests/guest/clib/CLibTest.bin"
[[ -x "$BINARY" ]] || { echo "SKIP: no binary $BINARY"; exit 77; }
[[ -f "$APP" ]] || { echo "SKIP: no $APP"; exit 77; }
DISK="${MACEMU_DISK:-$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" tests-7.5.5 $$)}"
[[ -f "$DISK" ]] || { echo "SKIP: no test disk"; exit 77; }

EXTFS_DIR=$(mktemp -d "${TMPDIR:-/tmp}/mactest-ppcbreak.XXXXXX")
LOG="${TMPDIR:-/tmp}/mactest_ppcbreak_$$.log"
python3 "$SCRIPT_DIR/guest/install_macbinary.py" "$APP" "$EXTFS_DIR" CLibTest
cleanup() {
    [[ -n "${EMU_PID:-}" ]] && kill "$EMU_PID" 2>/dev/null && wait "$EMU_PID" 2>/dev/null
    rm -rf "$EXTFS_DIR"
    [[ ${EXIT_CODE:-1} -eq 0 ]] && rm -f "$LOG"
}
trap cleanup EXIT

"$BINARY" --backend executor --config /dev/null --headless-http --port "$PORT" --timeout 150 \
    --executor-system "$(basename "$DISK")" --executor-writable-images --extfs "$EXTFS_DIR" \
    --executor-ppc-break '!fopen@40' >"$LOG" 2>&1 &
EMU_PID=$!

FAIL=0
check() {   # check NAME COMMAND...: PASS/FAIL line
    if "${@:2}"; then echo "PASS: $1"; else echo "FAIL: $1"; FAIL=1; fi
}
api() { curl -sf --max-time 10 "$@"; }

for i in $(seq 1 30); do api "http://localhost:$PORT/api/status" >/dev/null && break; sleep 0.5; done
api -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null
for i in $(seq 1 90); do
    api "http://localhost:$PORT/api/status" | grep -q '"bridge_agent_connected": true' && break
    sleep 1
done
# main's code offset: the transition vector main names, in the data section
MAIN=$(python3 - "$APP" "$PROJECT_ROOT/tools/macdecode" <<'PY'
import struct, sys
sys.path.insert(0, sys.argv[2])
import pef
b = open(sys.argv[1], 'rb').read()
p = pef.PEF(b[128:128 + struct.unpack('>I', b[83:87])[0]])   # MacBinary: the data fork
d = p.secs[p.main_sec]['data']
print('%x' % struct.unpack('>I', d[p.main_off:p.main_off + 4])[0])
PY
)
SPEC="CLibTest+$MAIN*0#r1:10"
ADD=$(api -X POST "http://localhost:$PORT/api/executor/ppc-break" -d "{\"add\":\"$SPEC\"}" || echo '{}')
check "API add (pending)" grep -q "\"spec\": \"${SPEC//\*/\\*}\", \"address\": \"pending" <<<"$ADD"
BAD=$(curl -s -X POST "http://localhost:$PORT/api/executor/ppc-break" -d '{"add":"nonsense"}')
check "API rejects a bad spec" grep -q '"error"' <<<"$BAD"

api -X POST "http://localhost:$PORT/api/launch" -d '{"path":"Host:CLibTest"}' >/dev/null \
    || { echo "FAIL: launch CLibTest"; tail -20 "$LOG"; exit 1; }

# The command line's breakpoint: fopen, called from CLibTest.
for i in $(seq 1 60); do grep -q '^\[ppcbreak\] end of trace' "$LOG" && break; sleep 0.5; done
FRAG=$(grep -a -o "^\[CFM\] file '[^']*CLibTest[^']*'" "$LOG" | head -1 | sed "s/.*file '//; s/'$//")
check "routine resolved" grep -aq "^\[ppcbreak\] '!fopen@40': 1 routine(s)" "$LOG"
check "hit reported" grep -aq "^\[ppcbreak\] hit 1 of '!fopen@40' at \\\$[0-9A-F]* Executor's fopen" "$LOG"
check "registers" grep -aq '^  r28 [0-9A-F]\{8\}  r29' "$LOG"
check "back chain into the app" grep -aEq "^    sp \\\$[0-9A-F]{8}  ret \\\$[0-9A-F]{8} ${FRAG:-CLibTest} sect 0" "$LOG"
check "trace back in the app" grep -aEq "^  \\\$[0-9A-F]{8} ${FRAG:-CLibTest} 0\+\\\$[0-9a-f]+ +[a-z]" "$LOG"
check "end of trace" grep -aq '^\[ppcbreak\] end of trace' "$LOG"

# The API's breakpoint: hit at main, the report there.
for i in $(seq 1 40); do
    LIST=$(api "http://localhost:$PORT/api/executor/ppc-break" || echo '{}')
    grep -q "hit 1 of 'CLibTest+" <<<"$LIST" && break
    sleep 0.5
done
check "API breakpoint resolved" grep -q "\"spec\": \"${SPEC//\*/\\*}\", \"address\": \"\\$" <<<"$LIST"
check "API report" grep -q "hit 1 of '${SPEC//\*/\\*}' at "'\$[0-9A-F]* CLibTest sect 0+\$'"$MAIN," <<<"$LIST"
check "API memory dump" grep -q '#r1:10 = ' <<<"$LIST"
REM=$(api -X POST "http://localhost:$PORT/api/executor/ppc-break" -d '{"remove":"all"}' || echo '{}')
check "API remove" grep -q '"breakpoints": \[\]' <<<"$REM"

if [[ $FAIL -ne 0 ]]; then
    echo "--- log ($LOG) ---"
    grep -a '^\[ppcbreak\]\|^\[CFM\]' "$LOG" | tail -20
    EXIT_CODE=1
    exit 1
fi
EXIT_CODE=0
echo "ppc_break_executor: ok"
