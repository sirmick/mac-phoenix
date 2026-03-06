#!/bin/bash
# Build script for test ROMs

set -e

# Find m68k toolchain
if command -v m68k-linux-gnu-as &> /dev/null; then
    AS=m68k-linux-gnu-as
    LD=m68k-linux-gnu-ld
    OBJCOPY=m68k-linux-gnu-objcopy
elif command -v m68k-elf-as &> /dev/null; then
    AS=m68k-elf-as
    LD=m68k-elf-ld
    OBJCOPY=m68k-elf-objcopy
else
    echo "Error: No m68k toolchain found."
    echo "On Ubuntu/Debian: sudo apt-get install gcc-m68k-linux-gnu binutils-m68k-linux-gnu"
    exit 1
fi

TEST_NAME=${1:-test_boundary}

echo "Building $TEST_NAME.S..."
echo "Using toolchain: $AS"

# Assemble
$AS -o $TEST_NAME.o $TEST_NAME.S || {
    echo "Assembly failed"
    exit 1
}

# Link at ROM base address
$LD -Ttext=0x02000000 -o $TEST_NAME.elf $TEST_NAME.o || {
    echo "Linking failed"
    exit 1
}

# Convert to raw binary
$OBJCOPY -O binary $TEST_NAME.elf $TEST_NAME.rom || {
    echo "Binary conversion failed"
    exit 1
}

# Pad to 64KB minimum (BasiliskII expects at least 64KB ROM)
SIZE=$(stat -c%s $TEST_NAME.rom 2>/dev/null || stat -f%z $TEST_NAME.rom 2>/dev/null)
if [ $SIZE -lt 65536 ]; then
    echo "Padding ROM to 64KB..."
    dd if=/dev/zero bs=1 count=$((65536 - SIZE)) >> $TEST_NAME.rom 2>/dev/null
fi

# Show ROM info
FINAL_SIZE=$(stat -c%s $TEST_NAME.rom 2>/dev/null || stat -f%z $TEST_NAME.rom 2>/dev/null)
echo "ROM built successfully: $TEST_NAME.rom (${FINAL_SIZE} bytes)"

# Hexdump first few bytes to verify
echo "ROM header (first 32 bytes):"
hexdump -C $TEST_NAME.rom | head -2

echo ""
echo "To test with UAE backend:"
echo "  CPU_BACKEND=uae ./build/macemu-next --rom $TEST_NAME.rom --no-webserver"
echo ""
echo "To test with Unicorn backend:"
echo "  CPU_BACKEND=unicorn ./build/macemu-next --rom $TEST_NAME.rom --no-webserver"