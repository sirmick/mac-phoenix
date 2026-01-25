#!/bin/bash
# Test runner for simple IRQ EmulOp test ROM

ROM_PATH="test_roms/test_irq_simple.rom"
TIMEOUT=2
BUILD_DIR="build"

echo "========================================"
echo "Simple IRQ EmulOp Test"
echo "========================================"
echo ""

# Function to check test results
check_results() {
    local log_file=$1
    local backend=$2

    echo "Results for $backend:"
    echo "-------------------"

    # Check for specific test markers in memory dumps
    if grep -q "Memory at 0x00001000.*53 54 41 52" "$log_file"; then
        echo "✓ Test started (STAR marker found)"
    else
        echo "✗ Test didn't start"
    fi

    if grep -q "Memory at 0x00001004.*49 52 51 21" "$log_file"; then
        echo "✓ IRQ EmulOp (0x7129) worked (IRQ! marker)"
    elif grep -q "Memory at 0x00001004.*4e 4f 49 51" "$log_file"; then
        echo "✗ IRQ EmulOp timeout (NOIQ marker)"
    fi

    if grep -q "Memory at 0x0000100c.*41 4c 4f 4b" "$log_file"; then
        echo "✓ A-line (0xAE29) handled (ALOK marker)"
    elif grep -q "Memory at 0x0000100c.*4e 4f 41 4c" "$log_file"; then
        echo "✗ A-line timeout (NOAL marker)"
    fi

    if grep -q "Memory at 0x00001100.*44 4f 4e 45" "$log_file"; then
        echo "✓ Test completed (DONE marker)"
    fi

    # Count EmulOp calls
    local irq_count=$(grep -c "EmulOp 0x7129" "$log_file" 2>/dev/null || echo 0)
    local aline_count=$(grep -c "opcode 0xae29\|A-line.*ae29" "$log_file" 2>/dev/null || echo 0)

    echo ""
    echo "IRQ EmulOp (0x7129) calls: $irq_count"
    echo "A-line (0xAE29) traps: $aline_count"

    if [ "$irq_count" -gt 1000 ]; then
        echo "⚠️  WARNING: Possible IRQ storm! ($irq_count calls)"
    fi

    # Show memory dump if verbose
    if [ "$VERBOSE" = "1" ]; then
        echo ""
        echo "Memory dump:"
        grep "Memory at 0x0000100" "$log_file" | head -10
    fi

    echo ""
}

# Navigate to macemu-next directory
cd "$(dirname "$0")/.." || exit 1

echo "Current directory: $(pwd)"
echo "ROM file: $ROM_PATH"
echo ""

# Test with UAE backend
echo "Testing with UAE backend..."
echo "----------------------------"
EMULATOR_TIMEOUT=$TIMEOUT CPU_BACKEND=uae EMULOP_VERBOSE=1 \
    $BUILD_DIR/macemu-next --rom "$ROM_PATH" --no-webserver > test_roms/uae_simple_test.log 2>&1 || true
check_results "test_roms/uae_simple_test.log" "UAE"

# Test with Unicorn backend
echo "Testing with Unicorn backend..."
echo "--------------------------------"
EMULATOR_TIMEOUT=$TIMEOUT CPU_BACKEND=unicorn EMULOP_VERBOSE=1 \
    $BUILD_DIR/macemu-next --rom "$ROM_PATH" --no-webserver > test_roms/unicorn_simple_test.log 2>&1 || true
check_results "test_roms/unicorn_simple_test.log" "Unicorn"

# Compare backends
echo "========================================"
echo "Comparison Summary"
echo "========================================"

uae_irq=$(grep -c "EmulOp 0x7129" test_roms/uae_simple_test.log 2>/dev/null || echo 0)
unicorn_irq=$(grep -c "EmulOp 0x7129" test_roms/unicorn_simple_test.log 2>/dev/null || echo 0)

echo ""
echo "IRQ EmulOp frequency:"
echo "  UAE:     $uae_irq calls"
echo "  Unicorn: $unicorn_irq calls"

if [ "$unicorn_irq" -gt "$((uae_irq * 10))" ]; then
    echo ""
    echo "🚨 CONFIRMED: IRQ storm in Unicorn backend!"
    echo "   Unicorn has $(($unicorn_irq / ($uae_irq + 1)))x more calls than UAE"
fi

echo ""
echo "Test logs saved:"
echo "  test_roms/uae_simple_test.log"
echo "  test_roms/unicorn_simple_test.log"
echo ""
echo "To see detailed traces, run with CPU_TRACE=0-100"