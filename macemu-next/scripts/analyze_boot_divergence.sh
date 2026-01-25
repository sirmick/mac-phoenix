#!/bin/bash
# Analyze where UAE and Unicorn diverge during boot
# Focuses on EmulOps, interrupts, and key events

TIMEOUT=${1:-30}
echo "=========================================="
echo "UAE vs Unicorn Boot Divergence Analysis"
echo "=========================================="
echo "Timeout: ${TIMEOUT} seconds"
echo ""

# Run UAE backend and capture key events
echo "Running UAE backend..."
EMULATOR_TIMEOUT=$TIMEOUT CPU_BACKEND=uae EMULOP_VERBOSE=1 \
    ./build/macemu-next --no-webserver 2>&1 | \
    grep -E "EmulOp.*\(|Timer:|IRQ|interrupt|SCSI|Exception|ERROR|PANIC" | \
    head -1000 > uae_events.log || true

# Run Unicorn backend and capture key events
echo "Running Unicorn backend..."
EMULATOR_TIMEOUT=$TIMEOUT CPU_BACKEND=unicorn EMULOP_VERBOSE=1 \
    ./build/macemu-next --no-webserver 2>&1 | \
    grep -E "EmulOp.*\(|Timer:|IRQ|interrupt|SCSI|Exception|ERROR|PANIC" | \
    head -1000 > unicorn_events.log || true

echo ""
echo "=== EmulOp Progression ==="
echo ""

# Extract just EmulOp names for comparison
echo "UAE EmulOp sequence (unique):"
grep "EmulOp.*(" uae_events.log | sed -E 's/.*\(([^)]+)\).*/\1/' | \
    awk '!seen[$0]++ {print NR": " $0}' | head -20

echo ""
echo "Unicorn EmulOp sequence (unique):"
grep "EmulOp.*(" unicorn_events.log | sed -E 's/.*\(([^)]+)\).*/\1/' | \
    awk '!seen[$0]++ {print NR": " $0}' | head -20

echo ""
echo "=== Interrupt Statistics ==="
echo ""

UAE_TIMER=$(grep -c "Timer:" uae_events.log)
UNICORN_TIMER=$(grep -c "Timer:" unicorn_events.log)
UAE_IRQ=$(grep -c "IRQ" uae_events.log)
UNICORN_IRQ=$(grep -c "IRQ" unicorn_events.log)

echo "Timer interrupts:"
echo "  UAE:     $UAE_TIMER ($(( UAE_TIMER / TIMEOUT )) Hz)"
echo "  Unicorn: $UNICORN_TIMER ($(( UNICORN_TIMER / TIMEOUT )) Hz)"
echo ""
echo "IRQ-related events:"
echo "  UAE:     $UAE_IRQ"
echo "  Unicorn: $UNICORN_IRQ"

echo ""
echo "=== Critical Events ==="
echo ""

echo "UAE errors/panics:"
grep -E "ERROR|PANIC" uae_events.log | head -5 || echo "  None found"

echo ""
echo "Unicorn errors/panics:"
grep -E "ERROR|PANIC" unicorn_events.log | head -5 || echo "  None found"

echo ""
echo "=== Boot Progress Comparison ==="
echo ""

# Check for key boot milestones
for milestone in "RESET" "PATCH_BOOT_GLOBS" "INSTIME" "SCSI_DISPATCH" "EXTFS" "AUDIO"; do
    UAE_HAS=$(grep -c "$milestone" uae_events.log)
    UNI_HAS=$(grep -c "$milestone" unicorn_events.log)

    if [ $UAE_HAS -gt 0 ] && [ $UNI_HAS -gt 0 ]; then
        echo "✅ $milestone: Both reach this point"
    elif [ $UAE_HAS -gt 0 ]; then
        echo "⚠️  $milestone: Only UAE reaches this"
    elif [ $UNI_HAS -gt 0 ]; then
        echo "⚠️  $milestone: Only Unicorn reaches this"
    else
        echo "❌ $milestone: Neither reaches this"
    fi
done

echo ""
echo "=== Last Events Before Timeout ==="
echo ""

echo "UAE last 5 events:"
tail -5 uae_events.log | sed 's/^/  /'

echo ""
echo "Unicorn last 5 events:"
tail -5 unicorn_events.log | sed 's/^/  /'

echo ""
echo "Full event logs saved to:"
echo "  uae_events.log"
echo "  unicorn_events.log"
echo ""
echo "To see detailed traces, use:"
echo "  CPU_TRACE=0-100 for instruction traces"
echo "  CPU_VERBOSE=1 for CPU state dumps"