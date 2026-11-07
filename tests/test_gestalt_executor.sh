#!/bin/bash
#
# test_gestalt_executor.sh — Executor's Gestalt answers against a Mac's: the
# probe (tests/guest/gestalt: GProbe68k.bin / GProbePPC.bin, its Retro68
# builds) run on Executor, the keys below compared with the reference measured
# on the ROM machine (tests/guest/gestalt/ref). The machine's identity and the
# managers Executor answers as the Mac does; the rest of the table differs by
# design (RAM, ROM, table addresses, managers Executor lacks).
#
# Usage:
#   tests/test_gestalt_executor.sh CONFIG [--port N]
#     7.6.1      the 68k probe on 7.6.1 against the Quadra 650's
#     7.6.1-ppc  the PowerPC probe with --executor-ppc against the G3's 7.6.1
#     8.6        the PowerPC probe on 8.6 against the G3's
#   GESTALT_OUT=FILE keeps the probe's whole output there.
set -uo pipefail

CONFIG="$1"; shift
PORT=18874
while [[ $# -gt 0 ]]; do
    case "$1" in
        --port) PORT="$2"; shift 2 ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done
DIR="$(cd "$(dirname "$0")" && pwd)"
G="$DIR/guest/gestalt"
KEYS="mach proc cput sysa fpu_ mmu_ addr pgsz pclk bclk nreg hdwr pop! micn kbd_ vers nmgr rsrc icon ditl misc sysu"
case "$CONFIG" in
    7.6.1)     ARGS=(--os-version 7.6.1); APP="$G/GProbe68k.bin"; REF="$G/ref/quadra-7.6.1.txt"; SAME_AS=""
               KEYS+=" lmem" ;;  # Executor's low memory is the Quadra's 0x2000 (the G3's 0x3000)
    7.6.1-ppc) ARGS=(--os-version 7.6.1 --executor-ppc); APP="$G/GProbePPC.bin"; REF="$G/ref/g3-7.6.1.txt"; SAME_AS="" ;;
    8.6)       ARGS=(--image 8.6 --executor-ppc); APP="$G/GProbePPC.bin"; REF="$G/ref/g3-8.6.txt"; SAME_AS=""
               KEYS+=" cpuf dlog cntl" ;;
    *) echo "Unknown config: $CONFIG"; exit 1 ;;
esac

KEEP="$(mktemp -d "${TMPDIR:-/tmp}/gestalt-$CONFIG-XXXXXX")"
trap '[[ -n "${GESTALT_OUT:-}" && -f "$KEEP/gestalt.txt" ]] && cp "$KEEP/gestalt.txt" "$GESTALT_OUT"; rm -rf "$KEEP"' EXIT
bash "$DIR/test_guest_suite.sh" --backend executor "${ARGS[@]}" --app "$APP" \
    --results gestalt.txt --network none --timeout 150 --port "$PORT" --keep-dir "$KEEP" \
    > "$KEEP/run.log" 2>&1
grep -q '^SKIP' "$KEEP/run.log" && { grep '^SKIP' "$KEEP/run.log"; exit 77; }
[[ -f "$KEEP/gestalt.txt" ]] || { echo "FAIL: no Host:gestalt.txt"; tail -20 "$KEEP/run.log"; exit 1; }

python3 - "$REF" "$KEEP/gestalt.txt" "$KEYS" "$SAME_AS" <<'PY'
import sys
def load(p):
    d = {}
    for l in open(p, encoding="latin-1").read().replace("\r", "\n").splitlines():
        if len(l) > 5 and l[4] == " ":
            d[l[:4]] = l[5:].split()
    return d
ref, got = load(sys.argv[1]), load(sys.argv[2])
want = {k: ref.get(k) for k in (k.replace("_", " ") for k in sys.argv[3].split())}
for o in filter(None, sys.argv[4].split()):
    k, v = o.split("=")
    want[k] = ["0", v]
bad = 0
for k, w in want.items():
    g = got.get(k)
    if g != w:
        print(f"FAIL: '{k}' {g} (the Mac: {w})"); bad += 1
    else:
        print(f"PASS: '{k}' {' '.join(g)}")
print(f"{len(want) - bad} same, {bad} different")
sys.exit(1 if bad else 0)
PY
