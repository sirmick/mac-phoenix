#!/bin/bash
# Analyze performance difference between UAE and Unicorn

echo "=========================================="
echo "Performance Analysis: UAE vs Unicorn"
echo "=========================================="
echo ""

# Test 1: Count total EmulOps in fixed time
echo "=== EmulOp Throughput (5 seconds) ==="
echo ""

echo -n "UAE: "
UAE_COUNT=$(env EMULATOR_TIMEOUT=5 CPU_BACKEND=uae EMULOP_VERBOSE=1 \
    ./build/macemu-next --no-webserver 2>&1 | \
    grep -c "Executing 0x7")
echo "$UAE_COUNT EmulOps ($(( UAE_COUNT / 5 ))/sec)"

echo -n "Unicorn: "
UNICORN_COUNT=$(env EMULATOR_TIMEOUT=5 CPU_BACKEND=unicorn EMULOP_VERBOSE=1 \
    ./build/macemu-next --no-webserver 2>&1 | \
    grep -c "Executing 0x7")
echo "$UNICORN_COUNT EmulOps ($(( UNICORN_COUNT / 5 ))/sec)"

echo ""
if [ $UAE_COUNT -gt 0 ] && [ $UNICORN_COUNT -gt 0 ]; then
    RATIO=$(echo "scale=2; $UAE_COUNT / $UNICORN_COUNT" | bc)
    echo "UAE is ${RATIO}x faster"
fi

echo ""
echo "=== Boot Progress Check ==="
echo ""

# Test 2: Check which EmulOps are reached
echo "UAE reaches in 5s:"
env EMULATOR_TIMEOUT=5 CPU_BACKEND=uae EMULOP_VERBOSE=1 \
    ./build/macemu-next --no-webserver 2>&1 | \
    grep "Executing 0x7" | \
    sed -E 's/.*Executing (0x[0-9a-f]+).*/\1/' | \
    sort -u | paste -sd' '

echo ""
echo "Unicorn reaches in 5s:"
env EMULATOR_TIMEOUT=5 CPU_BACKEND=unicorn EMULOP_VERBOSE=1 \
    ./build/macemu-next --no-webserver 2>&1 | \
    grep "Executing 0x7" | \
    sed -E 's/.*Executing (0x[0-9a-f]+).*/\1/' | \
    sort -u | paste -sd' '

echo ""
echo "=== Specific OpCode Analysis ==="
echo ""

# Test 3: Focus on CLKNOMEM (0x7104)
echo "CLKNOMEM (0x7104) frequency:"
echo -n "  UAE: "
UAE_CLKNOMEM=$(env EMULATOR_TIMEOUT=5 CPU_BACKEND=uae EMULOP_VERBOSE=1 \
    ./build/macemu-next --no-webserver 2>&1 | \
    grep -c "Executing 0x7104")
echo "$UAE_CLKNOMEM calls"

echo -n "  Unicorn: "
UNICORN_CLKNOMEM=$(env EMULATOR_TIMEOUT=5 CPU_BACKEND=unicorn EMULOP_VERBOSE=1 \
    ./build/macemu-next --no-webserver 2>&1 | \
    grep -c "Executing 0x7104")
echo "$UNICORN_CLKNOMEM calls"

if [ $UNICORN_CLKNOMEM -gt 0 ] && [ $UAE_CLKNOMEM -gt 0 ]; then
    echo ""
    echo "⚠️  Unicorn is spending excessive time in CLKNOMEM!"
    echo "   This suggests the execution loop may not be optimal for this code pattern."
fi

echo ""
echo "=== Hook Overhead Analysis ==="
echo ""

# Test 4: Check hook calls
echo "Unicorn hook calls in 5s:"
env EMULATOR_TIMEOUT=5 CPU_BACKEND=unicorn CPU_VERBOSE=1 \
    ./build/macemu-next --no-webserver 2>&1 | \
    grep -c "hook_" | \
    xargs -I {} echo "  {} hook calls"

echo ""
echo "=== Recommendations ==="
echo ""

if [ $UNICORN_CLKNOMEM -gt $UAE_CLKNOMEM ]; then
    echo "1. Unicorn is stuck in CLKNOMEM loop longer than UAE"
    echo "2. This may be due to:"
    echo "   - Translation block invalidation on each iteration"
    echo "   - Excessive interrupt checking"
    echo "   - Hook overhead"
    echo "3. Consider:"
    echo "   - Increasing batch size for this code region"
    echo "   - Caching translation blocks better"
    echo "   - Reducing interrupt check frequency"
fi