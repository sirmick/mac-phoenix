#!/bin/bash
#
# test_wne_patch_sanity.sh - Verify BridgeAgent's WaitNextEvent trap-patch
# installs cleanly and fires.
#
# BridgeAgent installs a trap on _WaitNextEvent at startup. The patched
# function bumps two counters in ScratchMem (absolute-addressable host RAM
# visible to the guest), then chains to the original WNE:
#   wne_total  — every time our patched code runs
#   wne_other  — only when the caller's A5 != BridgeAgent's A5 (i.e. the
#                patch is running in another process's context)
#
# Both counters are surfaced via the JSON heartbeat file that BridgeAgent
# rewrites every ~2 seconds into bridge_dir.
#
# Assertions:
#   wne_total > 10   — the patch is installed and firing (doesn't crash)
#
# Informational (not asserted):
#   wne_other        — should grow for the "preemptive" property to hold.
#                      Currently stays at 0 because Process Manager scopes
#                      app-installed Toolbox patches to the owning app's
#                      context. Fixing this needs an INIT-style patch or
#                      another mechanism — see plan for next step.
#
set -euo pipefail

BACKEND="uae"
TIMEOUT=35
PORT=18108
SIG_PORT=18109
BINARY="$(cd "$(dirname "$0")/.." && pwd)/build/mac-phoenix"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --backend) BACKEND="$2"; shift 2 ;;
        --port) PORT="$2"; SIG_PORT="$((PORT + 1))"; shift 2 ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

ROM="${MACEMU_ROM:-$HOME/roms/quadra.rom}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DISK="${MACEMU_DISK:-$(bash "$SCRIPT_DIR/lib/refresh_test_disk.sh" macos-7.5.5)}"

[[ -x "$BINARY" ]] || { echo "SKIP: Binary not found: $BINARY"; exit 77; }
[[ -f "$ROM"    ]] || { echo "SKIP: ROM not found: $ROM";       exit 77; }
[[ -f "$DISK"   ]] || { echo "SKIP: Disk not found: $DISK";     exit 77; }

# Boot test runs mutate the disk image (bridge provisioning writes System
# Folder:Startup Items). Restore from .bak if one exists.
if [[ -f "${DISK}.bak" ]]; then
    cp "${DISK}.bak" "$DISK"
fi

BRIDGE_DIR="$(mktemp -d /tmp/wne-patch-bridge-XXXXXX)"

EMU_PID=""
cleanup() {
    if [[ -n "$EMU_PID" ]]; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
    rm -rf "$BRIDGE_DIR"
}
trap cleanup EXIT

echo "=== WNE Patch Sanity Test: $BACKEND backend ==="
echo "Bridge dir: $BRIDGE_DIR"

"$BINARY" --backend "$BACKEND" --timeout "$TIMEOUT" \
    --config /dev/null --dismiss-shutdown-dialog --headless-http \
    --port "$PORT" --signaling-port "$SIG_PORT" \
    --disk "$DISK" --extfs "$BRIDGE_DIR" --bridge "$ROM" \
    >"$BRIDGE_DIR/emu.log" 2>&1 &
EMU_PID=$!

# Wait for HTTP server
for i in $(seq 1 15); do
    if curl -sf "http://localhost:$PORT/api/status" >/dev/null 2>&1; then break; fi
    sleep 1
done

# Start the CPU
curl -sf -X POST "http://localhost:$PORT/api/emulator/start" >/dev/null

# Wait for BridgeAgent to come online (Finder reached + heartbeat fresh)
echo -n "Waiting for bridge agent..."
CONNECTED=""
for i in $(seq 1 25); do
    STATUS=$(curl -sf "http://localhost:$PORT/api/status" 2>/dev/null || echo "")
    if echo "$STATUS" | grep -q '"bridge_agent_connected": *true'; then
        CONNECTED="yes"
        echo " connected (${i}s)"
        break
    fi
    echo -n "."
    sleep 1
done
if [[ -z "$CONNECTED" ]]; then
    echo ""
    echo "FAIL: bridge_agent_connected never became true"
    echo "--- emulator tail ---"
    tail -30 "$BRIDGE_DIR/emu.log" || true
    echo "--- bridge_dir ---"
    ls -la "$BRIDGE_DIR" || true
    exit 1
fi

# Let Finder's idle loop run for a while so WNE gets hit many times.
sleep 8

HB="$BRIDGE_DIR/bridge_heartbeat"
if [[ ! -f "$HB" ]]; then
    echo "FAIL: no heartbeat file at $HB"
    ls -la "$BRIDGE_DIR"
    exit 1
fi

JSON=$(tr -d '\r\n' < "$HB")
echo "Heartbeat: $JSON"

WNE_TOTAL=$(echo "$JSON" | grep -oP '"wne_total":\s*\K[0-9]+' || echo "")
WNE_OTHER=$(echo "$JSON" | grep -oP '"wne_other":\s*\K[0-9]+' || echo "")

if [[ -z "$WNE_TOTAL" || -z "$WNE_OTHER" ]]; then
    echo "FAIL: counters missing from heartbeat JSON"
    exit 1
fi

PASS=0
FAIL=0
check() {
    if (( $2 )); then
        echo "  OK: $1"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $1"
        FAIL=$((FAIL + 1))
    fi
}

check "wne_total > 10  (got $WNE_TOTAL)"        "WNE_TOTAL > 10"

# Diagnostic only — tracks whether the patch is firing from non-BridgeAgent
# contexts. Currently expected to be 0 (app-scoped trap patch); once we move
# to an INIT or A5-trampoline we expect this to grow.
echo "  INFO: wne_other=$WNE_OTHER (0 is current baseline — tracking the cross-process property)"

echo ""
if (( FAIL == 0 )); then
    echo "PASS: WNE patch installed and firing (wne_total=$WNE_TOTAL wne_other=$WNE_OTHER)"
    exit 0
else
    echo "FAIL: $FAIL of $((PASS + FAIL)) checks failed"
    echo "--- emulator tail ---"
    tail -50 "$BRIDGE_DIR/emu.log" || true
    exit 1
fi
