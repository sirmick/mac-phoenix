#!/bin/bash
#
# test_toolbox_suite.sh — Executor's Toolbox tests, as a 68k Mac application.
#
# src/executor/tests builds two ways: natively against Executor's Toolbox
# (ctest label "executor"), and with Retro68 into a Mac application that
# runs the same tests on whatever System it is launched under. This runs
# that application on a backend:
#
#   uae       Apple's ROM and System: the ground truth. A failure here is
#             a test that expects the wrong thing.
#   executor  Executor's Toolbox, running the app like any other.
#
# The app is built here (tests/lib or build-m68k-tests, Retro68 in
# toolchain/retro68), unpacked into a shared folder, launched through the
# bridge, and writes googletest's output to "out" next to itself. Failures
# listed in tests/guest/toolbox_expected_<backend>.txt (one test name per
# line, # comments) are reported but don't fail the run; anything else
# does, and so does an expected failure that now passes (take it off the
# list).
#
# Usage:
#   tests/test_toolbox_suite.sh [--backend uae|executor] [--port N]
#                               [--timeout N] [--filter GTEST_FILTER]
#
# Skips (77) without the Retro68 toolchain, a ROM (uae) or the disk image.
#
# STATUS (2026-10-09, work in progress, not in ctest yet): the app builds,
# installs, launches through the bridge and reports through "out" on both
# backends. Without the Files tests, 56 of 58 pass on real 7.5.5 (UAE);
# QuickDraw.Hilite8/32 fail there (their expectations are wrong). But the
# app crashes in a way that depends on code layout, not on the test:
# Files.GetWDInfo / CreateDeleteDir die before their first statement, an
# identical copy in main.cpp passes, and reordering TEST_SOURCES stops the
# app starting at all. On Executor, Retro68's startup (ApplyRelocations)
# hits an F-line trap. Suspect Retro68 relocation of a ~1.6 MB binary;
# next: debug the startup on Executor (--logtraps, snapshot), or split
# into smaller apps per test file.
#
#   --trace      record A-traps (--trace-atraps; snapshot on timeout)
#   --logtraps   Executor's full trap log to the emulator log
#   KEEP=1       keep the shared folder (app, filter, out)

set -euo pipefail

TIMEOUT=180
PORT=18124
BACKEND="uae"
FILTER=""
TRACE=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --port)    PORT="$2"; shift 2 ;;
        --backend) BACKEND="$2"; shift 2 ;;
        --filter)  FILTER="$2"; shift 2 ;;
        --trace)   TRACE=(--trace-atraps); shift ;;
        --logtraps) TRACE=(--executor-logtraps); shift ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"
BINARY="$PROJECT_ROOT/build/mac-phoenix"
APP_BUILD="$PROJECT_ROOT/build-m68k-tests"
TOOLCHAIN="$PROJECT_ROOT/toolchain/retro68/m68k-apple-macos/cmake/retro68.toolchain.cmake"
EXPECTED="$PROJECT_ROOT/tests/guest/toolbox_expected_${BACKEND}.txt"
APP_NAME="MacPhoenixTests"

[[ -x "$BINARY" ]] || { echo "SKIP: Binary not found: $BINARY"; exit 77; }
[[ -f "$TOOLCHAIN" ]] || { echo "SKIP: Retro68 toolchain not found"; exit 77; }

ROM=""
if [[ "$BACKEND" != "executor" ]]; then
    ROM="${MACEMU_ROM:-$HOME/roms/quadra.rom}"
    [[ -f "$ROM" ]] || { echo "SKIP: ROM not found: $ROM"; exit 77; }
fi
DISK="${MACEMU_DISK:-$(bash tests/lib/refresh_test_disk.sh macos-7.5.5)}"
[[ -f "$DISK" ]] || { echo "SKIP: Disk image not found: $DISK"; exit 77; }

# --- Build the application ---------------------------------------------------
echo -n "Building $APP_NAME (Retro68)..."
if [[ ! -f "$APP_BUILD/CMakeCache.txt" ]]; then
    cmake -S src/executor/tests -B "$APP_BUILD" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" >/dev/null
fi
if ! cmake --build "$APP_BUILD" -j"$(nproc)" --target tests_APPL >/tmp/toolbox_build_$$.log 2>&1; then
    echo " FAIL"; tail -20 /tmp/toolbox_build_$$.log; exit 1
fi
rm -f /tmp/toolbox_build_$$.log
echo " ok"

EXTFS_DIR=$(mktemp -d /tmp/mactoolbox-extfs.XXXXXX)
python3 tests/guest/install_macbinary.py "$APP_BUILD/tests.bin" "$EXTFS_DIR" "$APP_NAME"
OUT="$EXTFS_DIR/out"
rm -f "/tmp/mactoolbox_out_${BACKEND}.txt"
LOG=/tmp/mactoolbox_$$.log

cleanup() {
    if [[ -n "${EMU_PID:-}" ]] && kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    [[ -n "${KEEP:-}" ]] && echo "kept $EXTFS_DIR" || rm -rf "$EXTFS_DIR"
}
trap cleanup EXIT SIGTERM SIGINT

echo "=== Toolbox Suite ($BACKEND) ==="
echo "Disk: $DISK  Port: $PORT"

FLAGS=(--disk "$DISK")
[[ "$BACKEND" == "executor" ]] && FLAGS=(--executor-system "$(basename "$DISK")" --executor-writable-images)
"$BINARY" --backend "$BACKEND" --timeout "$((TIMEOUT + 10))" \
    --config /dev/null --dismiss-shutdown-dialog --headless-http \
    --port "$PORT" --network none --extfs "$EXTFS_DIR" \
    "${FLAGS[@]}" "${TRACE[@]}" ${ROM:+"$ROM"} &>"$LOG" &
EMU_PID=$!

for i in $(seq 1 40); do
    curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1 && break
    kill -0 "$EMU_PID" 2>/dev/null || { echo "FAIL: emulator exited early"; tail -20 "$LOG"; exit 1; }
    sleep 0.5
done
curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null

echo -n "Booting..."
START=$(date +%s)
until curl -sf "http://localhost:$PORT/api/status" | grep -q '"bridge_agent_connected": true'; do
    if (( $(date +%s) - START > TIMEOUT / 2 )); then
        echo " timeout (no BridgeAgent)"; tail -10 "$LOG"; exit 1
    fi
    sleep 1
done
echo " bridge up ($(( $(date +%s) - START ))s)"

if [[ -n "$FILTER" ]]; then
    # googletest reads GTEST_FILTER from the environment; the Mac has none,
    # so the filter goes in a file the app reads at startup.
    printf '%s\r' "$FILTER" > "$EXTFS_DIR/filter"
fi

echo "Launching $APP_NAME..."
LAUNCH=$(curl -sf --max-time 20 -X POST "http://localhost:$PORT/api/launch" \
    -d "{\"path\":\"Host:$APP_NAME\"}" || echo '{}')
echo "$LAUNCH" | grep -q '"success": true' || { echo "FAIL: launch: $LAUNCH"; exit 1; }

echo -n "Running..."
RUN_START=$(date +%s)
until [[ -f "$OUT" ]] && tr '\r' '\n' < "$OUT" | grep -q '^MACPHOENIX-DONE'; do
    if ! kill -0 "$EMU_PID" 2>/dev/null; then
        echo " emulator died"; break
    fi
    if (( $(date +%s) - START > TIMEOUT )); then
        curl -sf -X POST "http://localhost:$PORT/api/snapshot" -H 'Content-Type: application/json' \
            -d "{\"name\":\"toolbox-${BACKEND}-timeout\"}" >/dev/null || true
        echo " timeout (snapshot: ~/storage/snapshots/toolbox-${BACKEND}-timeout)"; break
    fi
    sleep 1
done
echo " ($(( $(date +%s) - RUN_START ))s)"
grep -a "host signal\|Child crashed" "$LOG" | head -3 || true

if [[ ! -f "$OUT" ]]; then
    echo "FAIL: no output from $APP_NAME"
    tail -20 "$LOG"
    exit 1
fi
tr '\r' '\n' < "$OUT" > "$OUT.unix"
cp "$OUT.unix" "/tmp/mactoolbox_out_${BACKEND}.txt"
grep -E '^\[ +(FAILED|PASSED|SKIPPED) +\]|^\[==========\]|^MACPHOENIX-DONE' "$OUT.unix" | grep -v "^\[  FAILED  \] .*listed below" || true

# --- Compare with the expected failures ---------------------------------------
FAILED=$(grep -E '^\[  FAILED  \] [A-Za-z0-9_]+\.[A-Za-z0-9_/]+' "$OUT.unix" \
    | awk '{print $4}' | sort -u)
EXPECT=""
[[ -f "$EXPECTED" ]] && EXPECT=$(sed -e 's/#.*//' -e 's/[[:space:]]*$//' "$EXPECTED" | grep -v '^$' | sort -u)

UNEXPECTED=$(comm -23 <(echo "$FAILED" | grep -v '^$') <(echo "$EXPECT" | grep -v '^$') || true)
NOWPASS=""
if [[ -n "$EXPECT" ]]; then
    RAN=$(grep -E '^\[       OK \] ' "$OUT.unix" | awk '{print $4}' | sort -u)
    NOWPASS=$(comm -12 <(echo "$EXPECT") <(echo "$RAN") || true)
fi

curl -sf --max-time 10 -X POST "http://localhost:$PORT/api/shutdown" -d '{}' >/dev/null 2>&1 || true
for i in $(seq 1 15); do kill -0 "$EMU_PID" 2>/dev/null || { EMU_PID=""; break; }; sleep 1; done

STATUS=0
if ! tr '\r' '\n' < "$OUT" | grep -q '^MACPHOENIX-DONE'; then
    echo "FAIL: $APP_NAME did not finish (crash or hang); last lines:"
    tail -5 "$OUT.unix"
    STATUS=1
fi
if [[ -n "$UNEXPECTED" ]]; then
    echo "FAIL: unexpected failures:"; echo "$UNEXPECTED" | sed 's/^/  /'
    STATUS=1
fi
if [[ -n "$NOWPASS" ]]; then
    echo "FAIL: expected to fail but passed (remove from $(basename "$EXPECTED")):"
    echo "$NOWPASS" | sed 's/^/  /'
    STATUS=1
fi
[[ $STATUS -eq 0 ]] && echo "PASS: toolbox suite on $BACKEND"
exit $STATUS
