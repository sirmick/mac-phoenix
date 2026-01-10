#!/bin/bash
# Differential debug script - Compare BasiliskII vs macemu-next boot
# Runs both emulators with identical configs and captures debug output

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
COMPARE_DIR="$SCRIPT_DIR/compare_configs"
OUTPUT_DIR="$SCRIPT_DIR/debug_outputs"

# Ensure output directory exists
mkdir -p "$OUTPUT_DIR"

# Timeout for each run (seconds)
TIMEOUT=${1:-10}

echo "========================================"
echo "Differential Boot Comparison"
echo "========================================"
echo "Timeout: ${TIMEOUT} seconds"
echo "Output directory: $OUTPUT_DIR"
echo ""

# Clean old outputs
rm -f "$OUTPUT_DIR"/basilisk.log "$OUTPUT_DIR"/macemu.log

echo "========================================"
echo "1. Running BasiliskII"
echo "========================================"
echo "Config: $COMPARE_DIR/basilisk.prefs"
echo "Output: $OUTPUT_DIR/basilisk.log"
echo ""

timeout ${TIMEOUT} ~/macemu-dual-cpu/BasiliskII/src/Unix/BasiliskII \
    --config "$COMPARE_DIR/basilisk.prefs" \
    > "$OUTPUT_DIR/basilisk.log" 2>&1 || true

echo "✓ BasiliskII run complete ($(wc -l < "$OUTPUT_DIR/basilisk.log") lines)"
echo ""

echo "========================================"
echo "2. Running macemu-next"
echo "========================================"
echo "Config: $COMPARE_DIR/macemu.json"
echo "Output: $OUTPUT_DIR/macemu.log"
echo ""

timeout ${TIMEOUT} "$PROJECT_ROOT/build/macemu-next" \
    --no-webserver \
    --config "$COMPARE_DIR/macemu.json" \
    > "$OUTPUT_DIR/macemu.log" 2>&1 || true

echo "✓ macemu-next run complete ($(wc -l < "$OUTPUT_DIR/macemu.log") lines)"
echo ""

echo "========================================"
echo "3. Analysis"
echo "========================================"
echo ""

# Count key events
echo "Event Counts:"
echo "-------------"
printf "%-30s %10s %10s\n" "Event" "BasiliskII" "macemu-next"
printf "%-30s %10s %10s\n" "-----" "----------" "-----------"

for event in "DiskOpen" "disk inserted" "DiskPrime" "DiskStatus" "DiskControl" \
             "VideoOpen" "VideoControl" "Illegal Instruction" "Unimplemented trap" \
             "EMUL_OP" "NativeOp"; do
    b_count=$(grep -c "$event" "$OUTPUT_DIR/basilisk.log" 2>/dev/null || echo 0)
    m_count=$(grep -c "$event" "$OUTPUT_DIR/macemu.log" 2>/dev/null || echo 0)
    printf "%-30s %10d %10d" "$event" "$b_count" "$m_count"
    if [ "$b_count" != "$m_count" ]; then
        echo "  ⚠️  MISMATCH"
    else
        echo ""
    fi
done
echo ""

# Look for errors
echo "Errors/Warnings:"
echo "----------------"
echo "BasiliskII errors:"
grep -i "error\|warning\|fail" "$OUTPUT_DIR/basilisk.log" | head -10 || echo "  (none found)"
echo ""
echo "macemu-next errors:"
grep -i "error\|warning\|fail" "$OUTPUT_DIR/macemu.log" | head -10 || echo "  (none found)"
echo ""

# Show first divergence point
echo "First Major Divergence:"
echo "-----------------------"
echo "Last common operation before logs diverge significantly..."
echo "(Compare files manually for details)"
echo ""

echo "Files saved:"
echo "  BasiliskII: $OUTPUT_DIR/basilisk.log"
echo "  macemu-next: $OUTPUT_DIR/macemu.log"
echo ""
echo "Suggested next steps:"
echo "  1. diff -u $OUTPUT_DIR/basilisk.log $OUTPUT_DIR/macemu.log | less"
echo "  2. grep -n 'DiskOpen\\|disk inserted' $OUTPUT_DIR/*.log"
echo "  3. Look for first error/divergence in macemu.log"
echo ""
