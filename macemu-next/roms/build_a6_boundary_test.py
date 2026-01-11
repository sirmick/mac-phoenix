#!/usr/bin/env python3
"""
Build a6_boundary_test.rom - Minimal test for A6 boundary issue

This ROM tests what happens when A6 is set to 0x02000000 (RAM/ROM boundary)
and then we try to continue execution.
"""

import struct

# ROM size (1MB like Quadra)
ROM_SIZE = 1024 * 1024

# Test ROM magic signature
TEST_ROM_MAGIC = 0x41364254  # "A6BT" in ASCII

# Create ROM buffer
rom = bytearray(ROM_SIZE)

# Header (Mac ROM compatible)
initial_sp = 0x01FFFE00  # Near end of 32MB RAM
initial_pc = 0x0000002A  # Standard Mac ROM entry point

# Write header
struct.pack_into('>I', rom, 0x00, initial_sp)  # Initial SP
struct.pack_into('>I', rom, 0x04, initial_pc)  # Initial PC

# ROM version (Quadra ROM version - required by CheckROM())
rom_version = 0x067C
struct.pack_into('>H', rom, 0x08, rom_version)

# TEST ROM MAGIC
struct.pack_into('>I', rom, 0x10, TEST_ROM_MAGIC)

# Fill padding with NOPs before entry point
for offset in range(0x0A, 0x10, 2):
    struct.pack_into('>H', rom, offset, 0x4E71)  # NOP
for offset in range(0x14, 0x2A, 2):
    struct.pack_into('>H', rom, offset, 0x4E71)  # NOP

# Test code starts at 0x2A (Mac ROM entry)
code = []

# Test 1: Set A6 to a normal RAM address and execute a few instructions
# move.l #0x01000000, %a6    ; A6 = middle of RAM
code.extend([0x2C7C, 0x0100, 0x0000])  # MOVE.L #imm32, A6
# move.l #0x11111111, %d0     ; Marker
code.extend([0x203C, 0x1111, 0x1111])  # MOVE.L #imm32, D0
# move.l #0x22222222, %d1     ; Another marker
code.extend([0x223C, 0x2222, 0x2222])  # MOVE.L #imm32, D1

# Test 2: Now set A6 to the boundary address
# move.l #0x02000000, %a6    ; A6 = RAM/ROM boundary
code.extend([0x2C7C, 0x0200, 0x0000])  # MOVE.L #imm32, A6

# Test 3: Try to execute more instructions after setting boundary
# move.l #0x33333333, %d2     ; This should execute
code.extend([0x243C, 0x3333, 0x3333])  # MOVE.L #imm32, D2
# move.l #0x44444444, %d3     ; And this
code.extend([0x263C, 0x4444, 0x4444])  # MOVE.L #imm32, D3
# move.l #0x55555555, %d4     ; And this
code.extend([0x283C, 0x5555, 0x5555])  # MOVE.L #imm32, D4

# Add a marker we can search for to see if we reached here
# move.l #0xDEADBEEF, %d5     ; Success marker
code.extend([0x2A3C, 0xDEAD, 0xBEEF])  # MOVE.L #imm32, D5

# Signal successful completion with EmulOp
code.extend([0x7100])  # EmulOp: SHUTDOWN

# Infinite loop (shouldn't reach)
code.extend([0x60FE])  # BRA.S -2 (loop to self)

# Write code to ROM at 0x2A
offset = 0x2A
for word in code:
    if isinstance(word, int) and word <= 0xFFFF:
        struct.pack_into('>H', rom, offset, word)
        offset += 2
    else:
        raise ValueError(f"Invalid instruction word: {word}")

# Write ROM file
with open('a6_boundary_test.rom', 'wb') as f:
    f.write(rom)

print(f"Created a6_boundary_test.rom ({ROM_SIZE} bytes)")
print(f"  Initial SP: 0x{initial_sp:08X}")
print(f"  Initial PC: 0x{initial_pc:08X}")
print(f"  ROM Version: 0x{rom_version:04X} (Quadra)")
print(f"  Test ROM Magic: 0x{TEST_ROM_MAGIC:08X} ('A6BT')")
print(f"  Entry point: offset 0x{initial_pc:X}")
print(f"  Test code size: {len(code) * 2} bytes")
print()
print("Test sequence:")
print("  1. Set A6 to 0x01000000 (normal RAM)")
print("  2. Execute D0=0x11111111, D1=0x22222222")
print("  3. Set A6 to 0x02000000 (RAM/ROM boundary)")
print("  4. Try to execute D2=0x33333333, D3=0x44444444, D4=0x55555555")
print("  5. Set D5=0xDEADBEEF as success marker")
print("  6. Call SHUTDOWN EmulOp")
print()
print("Expected: All instructions execute, D5=0xDEADBEEF")
print("Bug: Execution may skip after setting A6 to boundary")