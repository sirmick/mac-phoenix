#!/bin/bash
# Runner script for IRQ EmulOp tests

ROM_PATH="test_roms/test_irq_emulop.rom"
TIMEOUT=2

echo "========================================"
echo "IRQ EmulOp Test Suite"
echo "========================================"
echo ""

# Function to check test results
check_results() {
    local log_file=$1
    local backend=$2

    echo "Results for $backend:"
    echo "-------------------"

    # Check for specific test markers
    if grep -q "STAR" "$log_file"; then
        echo "✓ Test started"
    else
        echo "✗ Test didn't start"
    fi

    if grep -q "IRQ!" "$log_file"; then
        echo "✓ IRQ EmulOp (0x7129) worked"
    elif grep -q "TIME" "$log_file"; then
        echo "✗ IRQ EmulOp timeout"
    fi

    if grep -q "ALOK" "$log_file"; then
        echo "✓ A-line (0xAE29) handled"
    elif grep -q "ALIN" "$log_file"; then
        echo "✗ A-line timeout"
    fi

    if grep -q "LOOP" "$log_file"; then
        echo "✓ Tight loop broken"
    fi

    if grep -q "TIMR" "$log_file"; then
        echo "✓ Timer interrupt delivered"
    elif grep -q "NOIN" "$log_file"; then
        echo "✗ No timer interrupt"
    fi

    if grep -q "PASS" "$log_file"; then
        echo "✓✓ ALL TESTS PASSED"
    elif grep -q "FAIL" "$log_file"; then
        echo "✗✗ TESTS FAILED"
    fi

    # Count IRQ EmulOp calls
    local irq_count=$(grep -c "EmulOp 0x7129" "$log_file" 2>/dev/null || echo 0)
    local aline_count=$(grep -c "opcode 0xae29" "$log_file" 2>/dev/null || echo 0)

    echo ""
    echo "IRQ EmulOp calls: $irq_count"
    echo "A-line traps: $aline_count"

    if [ "$irq_count" -gt 10000 ]; then
        echo "⚠️  WARNING: IRQ storm detected! ($irq_count calls)"
    fi

    echo ""
}

# Test with UAE backend
echo "Testing with UAE backend..."
echo "----------------------------"
EMULATOR_TIMEOUT=$TIMEOUT CPU_BACKEND=uae EMULOP_VERBOSE=1 \
    ./build/macemu-next --rom "$ROM_PATH" --no-webserver > uae_test.log 2>&1 || true
check_results "uae_test.log" "UAE"

# Test with Unicorn backend
echo "Testing with Unicorn backend..."
echo "--------------------------------"
EMULATOR_TIMEOUT=$TIMEOUT CPU_BACKEND=unicorn EMULOP_VERBOSE=1 \
    ./build/macemu-next --rom "$ROM_PATH" --no-webserver > unicorn_test.log 2>&1 || true
check_results "unicorn_test.log" "Unicorn"

# Compare backends
echo "========================================"
echo "Comparison"
echo "========================================"
echo ""

echo "IRQ EmulOp frequency:"
echo "  UAE:     $(grep -c "EmulOp 0x7129" uae_test.log 2>/dev/null || echo 0) calls"
echo "  Unicorn: $(grep -c "EmulOp 0x7129" unicorn_test.log 2>/dev/null || echo 0) calls"

echo ""
echo "Test logs saved:"
echo "  UAE:     uae_test.log"
echo "  Unicorn: unicorn_test.log"
echo ""
echo "Memory dumps at key addresses:"
echo "  0x1000: Start marker (should be 'STAR')"
echo "  0x1004: IRQ test result"
echo "  0x100C: A-line test result"
echo "  0x1014: Tight loop result"
echo "  0x1018: Timer test result"
echo "  0x1020: Interrupt counter"
echo "  0x1100: Final result"
