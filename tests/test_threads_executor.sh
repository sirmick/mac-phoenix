#!/bin/bash
#
# test_threads_executor.sh — the Thread Manager on Executor, through
# UserLand Frontier.
#
# Frontier's UserTalk runs its scripts and agents on Thread Manager
# threads (thread.evaluate, thread.sleepFor, thread.kill). The test boots
# Executor on a scratch copy of the 7.5.5 image (Frontier saves its
# database on quit), launches Frontier through the bridge and sends it
# UserTalk scripts with /api/script ('misc'/'dosc'). Each script writes
# its findings to a file on the Host volume, the bridge's shared folder,
# which the test reads:
#
#   finite     a thread that yields 20 times and ends
#   endless    an endless thread runs beside the main script
#   kill       thread.kill stops an endless thread (DisposeThread of a
#              parked thread, which unwinds on its own host thread)
#   two        an endless and a finite thread together; Frontier's own
#              scheduling favours the endless one (as on the ROM machine
#              with Apple's Thread Manager), so only progress is checked
#   quit       Frontier quits with an endless thread alive (its threads
#              unwind at ExitToShell) and another application launches
#
# Usage:
#   tests/test_threads_executor.sh [--timeout N] [--port N] [--core uae|mame-68k]
#
# Skips (exit 77) if the 7.5.5 image with Frontier isn't available.
set -euo pipefail

TIMEOUT=240
PORT=18128
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
IMAGE_NAME="test-threads-7.5.5.img"
IMAGE="$STORAGE/images/$IMAGE_NAME"
rm -rf "$STORAGE/executor-systems/$IMAGE_NAME" "$STORAGE/executor-systems/$IMAGE_NAME.clean"   # a new System each run
FRONTIER="Macintosh HD:Frontier 5.0.1:UserLand Frontier™"

if [[ ! -x "$BINARY" ]]; then echo "SKIP: Binary not found: $BINARY"; exit 77; fi
if [[ ! -f "$SOURCE" ]]; then echo "SKIP: $SOURCE not found"; exit 77; fi
if command -v hls >/dev/null 2>&1; then
    if ! (hmount "$SOURCE" >/dev/null 2>&1 && hls ":Frontier 5.0.1" >/dev/null 2>&1); then
        humount >/dev/null 2>&1 || true
        echo "SKIP: no Frontier 5.0.1 on $SOURCE"; exit 77
    fi
    humount >/dev/null 2>&1 || true
fi

# A scratch copy: Frontier writes its database, the Finder its desktop.
cp "$SOURCE" "$IMAGE"

EXTFS_DIR=$(mktemp -d /tmp/macthreadtest-extfs.XXXXXX)
LOG=/tmp/macthreadtest_$$.log
cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    rm -rf "$EXTFS_DIR"
    rm -f "$IMAGE"
}
trap cleanup EXIT SIGTERM SIGINT

FAILS=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; FAILS=$((FAILS + 1)); }

api() { curl -sf --max-time 30 -X POST "http://localhost:$PORT/api/$1" -H 'Content-Type: application/json' --data-binary "$2"; }
app() { curl -sf "http://localhost:$PORT/api/app" | grep -oP '"app"\s*:\s*"\K[^"]+' || true; }
alive() { kill -0 "$EMU_PID" 2>/dev/null && ! grep -q "host signal\|fatal heap error" "$LOG"; }

# run_script NAME 'UserTalk writing Host:NAME.txt' SECONDS -> the file's text
run_script() {
    local name=$1 script=$2 wait=$3
    rm -f "$EXTFS_DIR/$name.txt"
    local body
    body=$(python3 -c 'import json,sys; print(json.dumps({"creator": "LAND", "script": sys.argv[1]}))' "$script")
    api script "$body" >/dev/null || true
    for _ in $(seq 1 "$wait"); do
        [[ -s "$EXTFS_DIR/$name.txt" ]] && break
        alive || break
        sleep 1
    done
    cat "$EXTFS_DIR/$name.txt" 2>/dev/null || true
}
field() { grep -oP "(^| )$1=\K[^ ]+" <<<"$2" || true; }

echo "=== Thread Manager Test (Executor, Frontier) ==="
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

echo "Launching Frontier..."
api launch "{\"path\":\"$FRONTIER\"}" >/dev/null || { echo "FAIL: launch"; exit 1; }
# Frontier is up once it answers a script (it runs its start-up scripts
# on threads first).
sleep 15
for _ in $(seq 1 60); do
    out=$(run_script up 'file.writeWholeFile ("Host:up.txt", "up")' 2)
    [[ "$out" == up ]] && break
    alive || break
done
[[ "${out:-}" == up ]] || { echo "FAIL: Frontier does not answer scripts"; tail -20 "$LOG"; exit 1; }

# 1. A finite thread.
r=$(run_script finite 'scratchpad.tmb = 0; local (idb = thread.evaluate ("local (i); for i = 1 to 20 {scratchpad.tmb = scratchpad.tmb + 1; thread.sleepFor (0)}")); local (t0 = clock.ticks ()); while thread.exists (idb) and (clock.ticks () - t0) < 1200 {thread.sleepFor (1)}; file.writeWholeFile ("Host:finite.txt", "b=" + scratchpad.tmb + " exists=" + thread.exists (idb))' 40)
echo "  finite: $r"
[[ "$(field b "$r")" == 20 && "$(field exists "$r")" == false ]] && pass finite || fail finite

# 2. An endless thread beside the main script.
r=$(run_script endless 'scratchpad.tma = 0; scratchpad.tmid = thread.evaluate ("loop {scratchpad.tma = scratchpad.tma + 1; thread.sleepFor (0)}"); thread.sleepFor (2); local (a1 = scratchpad.tma); thread.sleepFor (1); local (a2 = scratchpad.tma); file.writeWholeFile ("Host:endless.txt", "a1=" + a1 + " a2=" + a2 + " exists=" + thread.exists (scratchpad.tmid))' 30)
echo "  endless: $r"
a1=$(field a1 "$r"); a2=$(field a2 "$r")
[[ -n "$a1" && -n "$a2" && "$a1" -gt 10 && "$a2" -gt "$a1" && "$(field exists "$r")" == true ]] && pass endless || fail endless

# 3. Kill it.
r=$(run_script kill 'thread.kill (scratchpad.tmid); thread.sleepFor (1); local (a3 = scratchpad.tma); thread.sleepFor (1); local (a4 = scratchpad.tma); file.writeWholeFile ("Host:kill.txt", "a3=" + a3 + " a4=" + a4 + " exists=" + thread.exists (scratchpad.tmid))' 30)
echo "  kill: $r"
[[ -n "$(field a3 "$r")" && "$(field a3 "$r")" == "$(field a4 "$r")" && "$(field exists "$r")" == false ]] && pass kill || fail kill

# 4. Two threads together.
r=$(run_script two 'scratchpad.tma = 0; scratchpad.tmb = 0; scratchpad.tmid = thread.evaluate ("loop {scratchpad.tma = scratchpad.tma + 1; thread.sleepFor (0)}"); local (idb = thread.evaluate ("local (i); for i = 1 to 20 {scratchpad.tmb = scratchpad.tmb + 1; thread.sleepFor (0)}")); thread.sleepFor (3); file.writeWholeFile ("Host:two.txt", "a=" + scratchpad.tma + " b=" + scratchpad.tmb + " count=" + thread.getCount ())' 30)
echo "  two: $r"
[[ -n "$(field a "$r")" && "$(field a "$r")" -gt 10 && "$(field b "$r")" -ge 1 ]] && pass two || fail two

# 5. Quit with the endless thread still alive, then launch another application.
api quit '{}' >/dev/null || true
for _ in $(seq 1 20); do
    [[ "$(app)" != *Frontier* ]] && break
    alive || break
    sleep 1
done
if alive && [[ "$(app)" != *Frontier* ]]; then
    api launch '{"path":"Macintosh HD:System Folder:Apple Menu Items:Note Pad"}' >/dev/null || true
    for _ in $(seq 1 20); do
        [[ "$(app)" == "Note Pad" ]] && break
        sleep 1
    done
fi
if alive && [[ "$(app)" == "Note Pad" ]]; then pass quit; else fail quit; fi

echo
if [[ $FAILS -eq 0 ]]; then
    echo "All thread tests passed"
    exit 0
fi
echo "FAIL: $FAILS thread test(s) failed (log: $LOG)"
tail -30 "$LOG"
exit 1
