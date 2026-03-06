#!/bin/bash
# Build script for A-line trap test ROM

set -e

echo "Building A-line trap test ROM..."

# Assemble the source
m68k-linux-gnu-as -m68040 -o test_aline_trap.o test_aline_trap.s

# Link it as a raw binary at the ROM address
m68k-linux-gnu-ld -Ttext=0x02000000 --oformat=binary -o test_aline_trap.rom test_aline_trap.o

# Pad to 64KB minimum (some emulators expect at least this size)
truncate -s 64K test_aline_trap.rom

# Show the first few bytes to verify
echo "ROM header (first 32 bytes):"
xxd -l 32 test_aline_trap.rom

echo "ROM built successfully: test_aline_trap.rom"
echo ""
echo "To test with UAE:"
echo "  EMULATOR_TIMEOUT=1 CPU_BACKEND=uae ./build/mac-phoenix --rom test_roms/test_aline_trap.rom --no-webserver"
echo ""
echo "To test with Unicorn:"
echo "  EMULATOR_TIMEOUT=1 CPU_BACKEND=unicorn ./build/mac-phoenix --rom test_roms/test_aline_trap.rom --no-webserver"