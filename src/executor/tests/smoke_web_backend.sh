#!/bin/bash
# End-to-end: the web parent starts `--backend executor` (no ROM) as its IPC
# child, the Browser comes up, and input sent through the API changes the
# screen. Usage: smoke_web_backend.sh <mac-phoenix> <workdir>
set -u
BIN="$1"
OUT="$2"
# A free port per run (a previous run's socket may still be closing).
PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')

if [[ "$(cat /proc/sys/vm/mmap_min_addr 2>/dev/null)" != "0" ]]; then
    echo "SKIP: needs vm.mmap_min_addr=0 (Executor maps guest RAM at address 0)"
    exit 77
fi

rm -rf "$OUT" && mkdir -p "$OUT/storage"
cat > "$OUT/cfg.json" <<JSON
{ "backend": "executor", "codec": "png", "screen": "640x480", "ram_mb": 32,
  "storage_dir": "$OUT/storage" }
JSON

"$BIN" --config "$OUT/cfg.json" --storage-dir "$OUT/storage" --port $PORT --timeout 60 \
    > "$OUT/server.log" 2>&1 &
PID=$!
cleanup() { kill $PID 2>/dev/null; wait $PID 2>/dev/null; }
trap cleanup EXIT

API="http://localhost:$PORT/api"

# Poll until the screen differs from FILE (up to ~6s); leaves it in $2.
wait_change() {
    local ref="$1" out="$2"
    for _ in $(seq 1 30); do
        sleep 0.2
        curl -sf -o "$out" "$API/screenshot" || continue
        cmp -s "$ref" "$out" || return 0
    done
    return 1
}
for _ in $(seq 1 40); do curl -sf "$API/status" >/dev/null && break; sleep 0.25; done
curl -sf -X POST "$API/emulator/start" >/dev/null || { echo "FAIL: start"; tail "$OUT/server.log"; exit 1; }

PHASE=""
for _ in $(seq 1 60); do
    PHASE=$(curl -sf "$API/status" | grep -oP '"boot_phase"\s*:\s*"\K[^"]+')
    [[ "$PHASE" == "desktop" ]] && break
    sleep 0.25
done
[[ "$PHASE" == "desktop" ]] || { echo "FAIL: phase '$PHASE'"; tail -20 "$OUT/server.log"; exit 1; }

APP=$(curl -sf "$API/app")
echo "$APP" | grep -q Browser || { echo "FAIL: app is $APP"; exit 1; }

sleep 0.5
curl -sf -o "$OUT/before.png" "$API/screenshot" || { echo "FAIL: screenshot"; exit 1; }

# Mouse: press on "File", drag to "New Folder...", release. Executor's
# menus are sticky, so releasing on the title would leave the menu open.
curl -sf -X POST "$API/mouse" -d '{"x":50,"y":10}' >/dev/null
sleep 0.3
curl -sf -X POST "$API/mouse" -d '{"button":0,"down":true}' >/dev/null
sleep 0.5
curl -sf -X POST "$API/mouse" -d '{"x":70,"y":27}' >/dev/null
sleep 0.5
curl -sf -o "$OUT/held.png" "$API/screenshot"
curl -sf -X POST "$API/mouse" -d '{"button":0,"down":false}' >/dev/null

POS=$(curl -sf "$API/mouse")
echo "$POS" | grep -q '"x": 70' || { echo "FAIL: mouse at $POS"; exit 1; }
if cmp -s "$OUT/before.png" "$OUT/held.png"; then
    echo "FAIL: pressing on the menu bar did not open the menu"
    exit 1
fi
wait_change "$OUT/held.png" "$OUT/dialog.png" \
    || { echo "FAIL: choosing File > New Folder did not change the screen"; exit 1; }
sleep 1   # let the dialog finish drawing
curl -sf -o "$OUT/dialog.png" "$API/screenshot"

# Keyboard: type into the dialog's name field.
for k in 7 7 7; do   # 'x'
    curl -sf -X POST "$API/keypress" -d "{\"key\":$k}" >/dev/null
    sleep 0.1
done
wait_change "$OUT/dialog.png" "$OUT/typed.png" \
    || { echo "FAIL: typing did not change the screen"; exit 1; }

echo "PASS: executor backend booted to the Browser and took mouse + keyboard input"
