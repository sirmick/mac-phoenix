#!/bin/bash

# Test both backends with their specific ROMs
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "========================================
Testing EmulOp Handling
========================================"

# Test UAE
echo -e "\n1. Testing UAE with 0x7129 encoding..."
echo "----------------------------------------"
TIMEOUT=1
IRQ_COUNT=$(env EMULOP_VERBOSE=1 \
    "$SCRIPT_DIR/../build/mac-phoenix" --config /dev/null --backend uae --timeout $TIMEOUT --rom "$SCRIPT_DIR/test_irq_uae.rom" --no-webserver 2>&1 | \
    grep -c "EmulOp.*0x7129" || true)
echo "UAE: $IRQ_COUNT IRQ EmulOp calls in ${TIMEOUT}s"

# Test Unicorn
echo -e "\n2. Testing Unicorn with 0xAE29 encoding..."
echo "-------------------------------------------"
ALINE_COUNT=$(env EMULOP_VERBOSE=1 \
    "$SCRIPT_DIR/../build/mac-phoenix" --config /dev/null --backend unicorn --timeout $TIMEOUT --rom "$SCRIPT_DIR/test_irq_unicorn.rom" --no-webserver 2>&1 | \
    grep -c "A-line.*0xae29" || true)
IRQ_COUNT=$(env EMULOP_VERBOSE=1 \
    "$SCRIPT_DIR/../build/mac-phoenix" --config /dev/null --backend unicorn --timeout $TIMEOUT --rom "$SCRIPT_DIR/test_irq_unicorn.rom" --no-webserver 2>&1 | \
    grep -c "EmulOp.*0x7129" || true)
echo "Unicorn: $ALINE_COUNT A-line traps, $IRQ_COUNT IRQ EmulOp calls in ${TIMEOUT}s"

echo ""
echo "========================================"
echo "Summary"
echo "========================================"
FAIL=0
if [ "$IRQ_COUNT" -gt 0 ] 2>/dev/null; then
    echo "✓ UAE:     $IRQ_COUNT EmulOp calls detected"
else
    echo "✗ UAE:     No EmulOp calls detected"
    FAIL=1
fi
if [ "$ALINE_COUNT" -gt 0 ] 2>/dev/null; then
    echo "✓ Unicorn: $ALINE_COUNT A-line traps detected"
else
    echo "✗ Unicorn: No A-line traps detected"
    FAIL=1
fi
echo "========================================"
exit $FAIL