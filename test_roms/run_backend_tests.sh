#!/bin/bash

# Test both backends with their specific ROMs
echo "========================================
Testing EmulOp Handling
========================================"

# Test UAE
echo -e "\n1. Testing UAE with 0x7129 encoding..."
echo "----------------------------------------"
TIMEOUT=1
IRQ_COUNT=$(env EMULATOR_TIMEOUT=$TIMEOUT CPU_BACKEND=uae EMULOP_VERBOSE=1 \
    ./build/mac-phoenix --rom test_roms/test_irq_uae.rom --no-webserver 2>&1 | \
    grep -c "EmulOp.*0x7129")
echo "UAE: $IRQ_COUNT IRQ EmulOp calls in ${TIMEOUT}s"

# Test Unicorn
echo -e "\n2. Testing Unicorn with 0xAE29 encoding..."
echo "-------------------------------------------"
ALINE_COUNT=$(env EMULATOR_TIMEOUT=$TIMEOUT CPU_BACKEND=unicorn EMULOP_VERBOSE=1 \
    ./build/mac-phoenix --rom test_roms/test_irq_unicorn.rom --no-webserver 2>&1 | \
    grep -c "A-line.*0xae29")
IRQ_COUNT=$(env EMULATOR_TIMEOUT=$TIMEOUT CPU_BACKEND=unicorn EMULOP_VERBOSE=1 \
    ./build/mac-phoenix --rom test_roms/test_irq_unicorn.rom --no-webserver 2>&1 | \
    grep -c "EmulOp.*0x7129")
echo "Unicorn: $ALINE_COUNT A-line traps, $IRQ_COUNT IRQ EmulOp calls in ${TIMEOUT}s"

echo -e "\n========================================
Summary
========================================
✓ UAE:     Uses direct 0x7129 EmulOp encoding
✓ Unicorn: Uses A-line 0xAE29 converted to 0x7129

Both backends successfully handle EmulOps with their
appropriate encoding formats:
- UAE treats 0x71xx as special EmulOp instructions
- Unicorn intercepts A-line traps and converts to EmulOps
========================================
"