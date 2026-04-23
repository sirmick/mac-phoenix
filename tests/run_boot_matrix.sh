#!/bin/bash
#
# run_boot_matrix.sh - Boot-capacity matrix: 6 backends x 2 OSes = 12 cells.
#
# For each cell:
#   - boot, poll until boot_phase=Finder, sustain 5s, capture screenshot
#   - record PASS/FAIL/TIMEOUT/CRASH/LIES/NOSHOT with elapsed + checkloads
#
# Outputs:
#   /tmp/boot_matrix/results.csv           — machine-readable one row per cell
#   /tmp/boot_matrix/<label>.png           — end-state screenshot (on PASS)
#   /tmp/boot_matrix/<label>.log           — emulator stdout/stderr
#   stdout                                 — human-readable grid summary at the end
#
# Usage:
#   tests/run_boot_matrix.sh [--only label1,label2,...] [--out /tmp/boot_matrix]
#
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
PER_CELL="$HERE/test_boot_matrix.sh"
OUT_DIR="/tmp/boot_matrix"
ONLY=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --only) ONLY="$2"; shift 2 ;;
        --out)  OUT_DIR="$2"; shift 2 ;;
        *) echo "Unknown arg: $1" >&2; exit 2 ;;
    esac
done

mkdir -p "$OUT_DIR"
CSV="$OUT_DIR/results.csv"
rm -f "$CSV"
echo "label,backend,jit,disk,result,phase,elapsed_s,checkloads,screenshot" > "$CSV"

ROM_M68K="${MACEMU_ROM_M68K:-$HOME/roms/quadra.rom}"
ROM_PPC="${MACEMU_ROM_PPC:-$HOME/storage/roms/g3.rom}"
DISK_755="${MACEMU_DISK_755:-$HOME/storage/images/macos-7.5.5.img}"
DISK_761="${MACEMU_DISK_761:-$HOME/storage/images/macos-7.6.1.img}"

# Cells: label|backend|jitflag|rom|disk|timeout|port
# jitflag is one of: --jit, --no-jit, ""  (no-op for unicorn-* backends)
CELLS=(
    "uae-interp-755|uae|--no-jit|$ROM_M68K|$DISK_755|45|18200"
    "uae-interp-761|uae|--no-jit|$ROM_M68K|$DISK_761|45|18210"
    "uae-jit-755|uae|--jit|$ROM_M68K|$DISK_755|45|18220"
    "uae-jit-761|uae|--jit|$ROM_M68K|$DISK_761|45|18230"
    "kpx-interp-755|kpx|--no-jit|$ROM_PPC|$DISK_755|90|18240"
    "kpx-interp-761|kpx|--no-jit|$ROM_PPC|$DISK_761|90|18250"
    "kpx-jit-755|kpx|--jit|$ROM_PPC|$DISK_755|90|18260"
    "kpx-jit-761|kpx|--jit|$ROM_PPC|$DISK_761|90|18270"
    "unicorn-m68k-755|unicorn-m68k||$ROM_M68K|$DISK_755|180|18280"
    "unicorn-m68k-761|unicorn-m68k||$ROM_M68K|$DISK_761|180|18290"
    "unicorn-ppc-755|unicorn-ppc||$ROM_PPC|$DISK_755|180|18300"
    "unicorn-ppc-761|unicorn-ppc||$ROM_PPC|$DISK_761|180|18310"
)

should_run() {
    local label="$1"
    if [[ -z "$ONLY" ]]; then
        return 0
    fi
    IFS=',' read -ra want <<< "$ONLY"
    for w in "${want[@]}"; do
        if [[ "$label" == "$w" ]]; then return 0; fi
    done
    return 1
}

MATRIX_START=$(date +%s)
for cell in "${CELLS[@]}"; do
    IFS='|' read -r label backend jitflag rom disk timeout port <<< "$cell"
    if ! should_run "$label"; then continue; fi

    echo ""
    echo "────── $label ──────"
    args=(
        --label "$label"
        --backend "$backend"
        --rom "$rom"
        --disk "$disk"
        --timeout "$timeout"
        --port "$port"
        --screenshot-dir "$OUT_DIR"
        --csv "$CSV"
    )
    if [[ -n "$jitflag" ]]; then args+=("$jitflag"); fi

    # Don't let a single cell's exit kill the run
    bash "$PER_CELL" "${args[@]}" || true
done
MATRIX_ELAPSED=$(( $(date +%s) - MATRIX_START ))

# ── Summary grid ──
echo ""
echo "════════════════════════════════════════════════════════════════════════"
echo "  Boot Matrix Results (total ${MATRIX_ELAPSED}s)  —  $CSV"
echo "════════════════════════════════════════════════════════════════════════"
printf "%-22s %-10s %-7s %-9s %-12s\n" "LABEL" "RESULT" "TIME" "RESOURCES" "SHOT"
printf "%-22s %-10s %-7s %-9s %-12s\n" "----------------------" "----------" "-------" "---------" "------------"
# Skip header
tail -n +2 "$CSV" | while IFS=',' read -r label backend jit disk result phase elapsed checkloads shot; do
    shot_tag="-"
    if [[ -n "$shot" && -f "$shot" ]]; then shot_tag="$(basename "$shot")"; fi
    printf "%-22s %-10s %-7s %-9s %-12s\n" "$label" "$result" "${elapsed}s" "$checkloads" "$shot_tag"
done

echo ""
PASS=$(tail -n +2 "$CSV" | awk -F, '$5=="PASS"' | wc -l)
TOTAL=$(tail -n +2 "$CSV" | wc -l)
echo "  $PASS / $TOTAL passed"
echo "  screenshots: $OUT_DIR/*.png"
echo "  logs:        $OUT_DIR/*.log"
echo "════════════════════════════════════════════════════════════════════════"

# Exit 0 regardless — the CSV is the answer the user wanted.
exit 0
