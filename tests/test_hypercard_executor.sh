#!/bin/bash
# test_hypercard_executor.sh [BINARY] [PORT] — HyperCard 2.4.1 scripted on
# Executor's own Apple Event Manager (8.6: the AppleScript extension doesn't
# install Apple's PACK 8 there). HyperCard fills its own handler tables with
# Pack8 $0830 and runs a 'dosc' script at idle by suspending the current
# event (AESuspendTheCurrentEvent, AEResumeTheCurrentEvent): a "do script"
# over /api/script goes to card 3 of its Home stack and writes the card's
# name to a file on Host:, which must then say "Card 3".
# Skipped without dev-tools-system-7.img or 8.6.img.
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BINARY="${1:-${MACEMU_BINARY:-$PROJECT_ROOT/build/mac-phoenix}}"
PORT="${2:-18894}"
source "$SCRIPT_DIR/lib/ports.sh"; PORT=$(mp_port "$PORT") || exit 1  # a free port (docs/Testing.md "Ports")
IMAGES="${MACEMU_IMAGES:-$HOME/storage/images}"
[[ -x "$BINARY" ]] || { echo "SKIP: no binary $BINARY"; exit 77; }
[[ -f "$IMAGES/dev-tools-system-7.img" ]] || { echo "SKIP: no dev-tools-system-7.img"; exit 77; }
[[ -f "$IMAGES/8.6.img" ]] || { echo "SKIP: no 8.6.img"; exit 77; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/mactest-hypercard.XXXXXX")
mkdir -p "$WORK/host"
cp "$IMAGES/dev-tools-system-7.img" "$WORK/hypercard.img"   # HyperCard writes its stacks
cleanup() {
    [[ -n "${EMU_PID:-}" ]] && kill "$EMU_PID" 2>/dev/null && wait "$EMU_PID" 2>/dev/null
    [[ ${EXIT_CODE:-1} -eq 0 ]] && rm -rf "$WORK" || echo "kept: $WORK"
}
trap cleanup EXIT

"$BINARY" --backend executor --config /dev/null --bridge --port "$PORT" --timeout 240 \
    --executor-system 8.6.img --executor-writable-images --extfs "$WORK/host" \
    --disk "$WORK/hypercard.img" > "$WORK/emulator.log" 2>&1 &
EMU_PID=$!
URL="http://127.0.0.1:$PORT"
api() { curl -sf --max-time 70 "$@"; }
for _ in $(seq 1 50); do api "$URL/api/status" > /dev/null && break; sleep 0.2; done
api -X POST "$URL/api/emulator/start" > /dev/null
EXIT_CODE=1
api -X POST "$URL/api/wait" -d '{"condition":"boot=Finder","timeout":90}' > /dev/null \
    || { echo "FAIL: 8.6 didn't reach the Finder"; exit 1; }
api -X POST "$URL/api/launch" \
    -d '{"path":"Development:Languages ƒ:HyperCard 2.4.1 ƒ:HyperCard"}' > /dev/null \
    || { echo "FAIL: HyperCard: launch"; exit 1; }
api -X POST "$URL/api/wait" -d '{"condition":"app=HyperCard","timeout":60}' > /dev/null \
    || { echo "FAIL: HyperCard not in front"; exit 1; }
sleep 3
F='\"Host:hc-card.txt\"'
SCRIPT="go to card 3\ropen file $F\rwrite the short name of this card to file $F\rclose file $F"
api -X POST "$URL/api/script" -d "{\"creator\":\"WILD\",\"script\":\"$SCRIPT\"}" > /dev/null \
    || { echo "FAIL: the do script event"; exit 1; }
got=""
for _ in $(seq 1 25); do
    got=$(tr -d '\r\n' < "$WORK/host/hc-card.txt" 2>/dev/null)
    [[ -n "$got" ]] && break
    sleep 0.4
done
if grep -qE "fatal|failed assertion" "$WORK/emulator.log"; then
    echo "FAIL: Executor stopped:"; grep -E "fatal|failed assertion" "$WORK/emulator.log" | head -3
elif [[ "$got" == "Card 3" ]]; then
    echo "PASS: HyperCard ran the script: on \"$got\""
    EXIT_CODE=0
else
    echo "FAIL: HyperCard's file says \"$got\" (want Card 3)"
fi
exit $EXIT_CODE
