#!/bin/bash
#
# test_aslm_executor.sh — the Apple Shared Library Manager on Executor,
# through Apple's own ASLM test tools run in MPW Shell.
#
# 7.5.5's extension loader runs the System's 'AINI' -32700 (ASLM Loader),
# which starts the Shared Library Manager extension (its .SLM driver, a
# Time Manager task, its library files opened in system mode). The test
# installs the ASLM 1.2 SDK's test libraries in Extensions and its MPW tools
# on a scratch copy of the 7.5.5 image, boots Executor, launches MPW Shell
# through the bridge and runs the tools with /api/script ('misc'/'dosc'),
# their output redirected to the Host volume:
#
#   lmt1       LibraryManagerTest1 -v: C, C++ and Pascal function sets,
#              objects of ExampleLibrary's TExampleClass created by class
#              ID, a library global shared by both objects
#   lmt1again  the same once the library has been unloaded (its file taken
#              out of the resource chain and put back)
#   scheduler  TestTool -c TTestTaskScheduler: tasks scheduled run at the
#              next SystemTask (.SLM's accRun)
#   all        TestTool -a: every test in TestLibrary, no ERROR line
#   after      MPW Shell quits and Note Pad launches (the resource chains
#              of the processes are sound)
#
# The ASLM kit is the SDK's MacBinary files in $STORAGE/aslm-kit (from
# E.T.O. #22, Past&Future:Archive:Apple Shared Library Manager:ASLM
# Debugging Tools): ExampleLibrary, LibraryManagerTest1, TestLibrary,
# TestTool. Skips (exit 77) without the kit, hfsutils or MPW on the image.
#
# Usage:
#   tests/test_aslm_executor.sh [--timeout N] [--port N] [--core uae|mame-68k]
set -euo pipefail

TIMEOUT=300
PORT=18164
BINARY="${MACEMU_BINARY:-$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix}"
CORE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port)    PORT="$2";    shift 2 ;;
        --core|--executor-cpu) CORE="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"
STORAGE="${MACEMU_STORAGE:-$HOME/storage}"
SOURCE="$STORAGE/images/tests-7.5.5.img"   # provisioning/images/tests-7.5.5.json
KIT="$STORAGE/aslm-kit"
IMAGE_NAME="test-aslm-7.5.5.img"
IMAGE="$STORAGE/images/$IMAGE_NAME"
VOLUME="Macintosh HD"
MPW="$VOLUME:MPW:MPW Shell"

if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
if [[ ! -f "$SOURCE" ]]; then echo "SKIP: $SOURCE not found"; exit 77; fi
if ! command -v hcopy >/dev/null 2>&1; then echo "SKIP: hfsutils not installed"; exit 77; fi
for f in ExampleLibrary LibraryManagerTest1 TestLibrary TestTool; do
    [[ -f "$KIT/$f.bin" ]] || { echo "SKIP: ASLM kit file $KIT/$f.bin not found"; exit 77; }
done

# hfsutils keeps its current volume in $HOME; use a private one.
HFSHOME=$(mktemp -d /tmp/macaslmtest-hfs.XXXXXX)
hfs() { HOME="$HFSHOME" "$@"; }

# A scratch copy: the libraries go in Extensions, the tools in a folder.
cp "$SOURCE" "$IMAGE"
rm -rf "$STORAGE/executor-systems/$IMAGE_NAME" "$STORAGE/executor-systems/$IMAGE_NAME.clean"
hfs hmount "$IMAGE" >/dev/null
if ! hfs hls ":MPW:MPW Shell" >/dev/null 2>&1; then
    hfs humount >/dev/null 2>&1 || true
    rm -f "$IMAGE"
    echo "SKIP: no MPW Shell on $SOURCE"; exit 77
fi
for f in ExampleLibrary TestLibrary; do
    hfs hcopy -m "$KIT/$f.bin" ":System Folder:Extensions:"
done
hfs hmkdir ":ASLM Tests"
for f in LibraryManagerTest1 TestTool; do
    hfs hcopy -m "$KIT/$f.bin" ":ASLM Tests:"
done
hfs humount >/dev/null

EXTFS_DIR=$(mktemp -d /tmp/macaslmtest-extfs.XXXXXX)
LOG=/tmp/macaslmtest_$$.log
cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    rm -rf "$EXTFS_DIR" "$HFSHOME"
    rm -f "$IMAGE"
    rm -rf "$STORAGE/executor-systems/$IMAGE_NAME" "$STORAGE/executor-systems/$IMAGE_NAME.clean"
}
trap cleanup EXIT SIGTERM SIGINT

FAILS=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; FAILS=$((FAILS + 1)); }

api() { curl -sf --max-time 30 -X POST "http://localhost:$PORT/api/$1" -H 'Content-Type: application/json' --data-binary "$2"; }
app() { curl -sf "http://localhost:$PORT/api/app" | grep -oP '"app"\s*:\s*"\K[^"]+' || true; }
alive() { kill -0 "$EMU_PID" 2>/dev/null && ! grep -q "host signal\|fatal heap error\|Fatal error" "$LOG"; }

# run_tool NAME 'MPW command' SECONDS -> the tool's output; NAME.status
# holds MPW's {Status}. Both are emptied first so a previous tool's
# output can't count; "∑" (stdout and stderr) needs MacRoman, hence
# script_b64.
run_tool() {
    local name=$1 cmd=$2 wait=$3
    : >"$EXTFS_DIR/$name.txt"
    : >"$EXTFS_DIR/$name.status"
    local body
    body=$(python3 -c '
import base64, json, sys
name, cmd, vol = sys.argv[1:4]
script = "Directory \"%s:ASLM Tests:\"\r%s ∑ \"Host:%s.txt\"\rEcho {Status} > \"Host:%s.status\"\r" % (vol, cmd, name, name)
print(json.dumps({"creator": "MPS ", "script_b64": base64.b64encode(script.encode("mac_roman")).decode()}))
' "$name" "$cmd" "$VOLUME")
    api script "$body" >/dev/null || true
    for _ in $(seq 1 "$wait"); do
        [[ -s "$EXTFS_DIR/$name.status" ]] && break
        alive || break
        sleep 1
    done
    tr '\r' '\n' <"$EXTFS_DIR/$name.txt"
}
status() { tr -d '\r\n ' <"$EXTFS_DIR/$1.status"; }

echo "=== Shared Library Manager Test (Executor, MPW) ==="
echo "Image: $IMAGE  Port: $PORT  Core: ${CORE:-default}"

EXTRA_FLAGS=(--executor-system "$IMAGE_NAME" --executor-writable-images)
[[ -n "$CORE" ]] && EXTRA_FLAGS+=(--core "$CORE")
"$BINARY" --backend executor --timeout "$((TIMEOUT + 10))" \
    --config /dev/null --dismiss-shutdown-dialog --headless-http \
    --port "$PORT" --extfs "$EXTFS_DIR" "${EXTRA_FLAGS[@]}" &>"$LOG" &
EMU_PID=$!

for _ in $(seq 1 20); do
    curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1 && break
    kill -0 "$EMU_PID" 2>/dev/null || { echo "emulator died"; tail -20 "$LOG"; exit 1; }
    sleep 0.5
done
curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null

echo -n "Booting..."
for _ in $(seq 1 60); do
    compgen -G "$EXTFS_DIR/MacPhoenix/*/bridge_heartbeat" >/dev/null && break
    alive || { echo " died"; tail -20 "$LOG"; exit 1; }
    sleep 1
done
compgen -G "$EXTFS_DIR/MacPhoenix/*/bridge_heartbeat" >/dev/null || { echo " no BridgeAgent"; exit 1; }
echo " ok"
grep -q "extension System: AINI -32700" "$LOG" || { echo "FAIL: the ASLM Loader did not run"; exit 1; }

echo "Launching MPW Shell..."
api launch "{\"path\":\"$MPW\"}" >/dev/null || { echo "FAIL: launch"; exit 1; }
for _ in $(seq 1 30); do
    [[ "$(app)" == "MPW Shell" ]] && break
    sleep 1
done
sleep 5

# 1. Function sets, classes by ID, a library global.
out=$(run_tool lmt1 "LibraryManagerTest1 -v" 60)
echo "$out" | sed 's/^/    /' | head -8
if [[ "$(status lmt1)" == 0 ]] && grep -q "GetGlobalInt() returns: 42" <<<"$out" \
   && grep -q "TExampleClass::Test = true, true, false" <<<"$out"; then
    pass lmt1
else
    fail lmt1
fi

# 2. Again, once ExampleLibrary has been unloaded.
out=$(run_tool lmt1again "LibraryManagerTest1 -v" 60)
if [[ "$(status lmt1again)" == 0 ]] && grep -q "GetGlobalInt() returns: 42" <<<"$out"; then
    pass lmt1again
else
    fail lmt1again
fi

# 3. The task scheduler: .SLM's accRun runs scheduled tasks at SystemTask.
out=$(run_tool scheduler "TestTool -v -c TTestTaskScheduler" 60)
if [[ "$(status scheduler)" == 0 ]] && grep -q "Test complete" <<<"$out" && ! grep -q "ERROR" <<<"$out"; then
    pass scheduler
else
    echo "$out" | grep "ERROR" | sed 's/^/    /' || true
    fail scheduler
fi

# 4. Every test in TestLibrary.
out=$(run_tool all "TestTool -a" 240)
echo "    $(grep -c . <<<"$out") lines, $(grep -c "ERROR" <<<"$out" || true) ERROR"
if [[ "$(status all)" == 0 ]] && grep -q "Testing backwards dependencies" <<<"$out" && ! grep -q "ERROR" <<<"$out"; then
    pass all
else
    echo "$out" | grep "ERROR" | sed 's/^/    /' || true
    fail all
fi

# 5. Quit MPW Shell, launch another application.
api quit '{}' >/dev/null || true
for _ in $(seq 1 30); do
    [[ "$(app)" != "MPW Shell" ]] && break
    alive || break
    sleep 1
done
if alive && [[ "$(app)" != "MPW Shell" ]]; then
    api launch '{"path":"Macintosh HD:System Folder:Apple Menu Items:Note Pad"}' >/dev/null || true
    for _ in $(seq 1 20); do
        [[ "$(app)" == "Note Pad" ]] && break
        sleep 1
    done
fi
if alive && [[ "$(app)" == "Note Pad" ]]; then pass after; else fail after; fi

echo
if [[ $FAILS -eq 0 ]]; then
    echo "All Shared Library Manager tests passed"
    exit 0
fi
echo "FAIL: $FAILS Shared Library Manager test(s) failed (log: $LOG)"
tail -30 "$LOG"
exit 1
