#!/usr/bin/env python3
"""
Build a6_skip_test.rom - Specific test for A6=0x02000000 instruction skipping bug

This test verifies the original bug where Unicorn would skip instructions
after setting A6 to the RAM/ROM boundary (0x02000000).
"""

import struct

# ROM size (1MB like Quadra)
ROM_SIZE = 1024 * 1024

# Test ROM magic signature
TEST_ROM_MAGIC = 0x41365350  # "A6SP" in ASCII (A6 Skip test)

# Create ROM buffer
rom = bytearray(ROM_SIZE)

# Header (Mac ROM compatible)
initial_sp = 0x00002000  # Simple stack pointer
initial_pc = 0x0000002A  # Standard Mac ROM entry point

# Write header
struct.pack_into('>I', rom, 0x00, initial_sp)  # Initial SP
struct.pack_into('>I', rom, 0x04, initial_pc)  # Initial PC

# ROM version (Quadra ROM version)
rom_version = 0x067C
struct.pack_into('>H', rom, 0x08, rom_version)

# TEST ROM MAGIC
struct.pack_into('>I', rom, 0x10, TEST_ROM_MAGIC)

# Fill padding with NOPs before entry point
for offset in range(0x0A, 0x10, 2):
    struct.pack_into('>H', rom, offset, 0x4E71)  # NOP
for offset in range(0x14, 0x2A, 2):
    struct.pack_into('>H', rom, offset, 0x4E71)  # NOP

# Test code starts at 0x2A
code = []

# Clear all registers for clean test
# moveq #0, %d0
code.extend([0x7000])  # MOVEQ #0, D0
# moveq #0, %d1
code.extend([0x7200])  # MOVEQ #0, D1
# moveq #0, %d2
code.extend([0x7400])  # MOVEQ #0, D2
# moveq #0, %d3
code.extend([0x7600])  # MOVEQ #0, D3
# moveq #0, %d4
code.extend([0x7800])  # MOVEQ #0, D4
# moveq #0, %d5
code.extend([0x7A00])  # MOVEQ #0, D5
# moveq #0, %d6
code.extend([0x7C00])  # MOVEQ #0, D6
# moveq #0, %d7
code.extend([0x7E00])  # MOVEQ #0, D7

# Test sequence: Set A6 to boundary, then execute several instructions
# Each instruction sets a unique pattern in a register so we can see
# which ones were executed

# Set A6 to a normal address first (should work)
# move.l #0x01000000, %a6
code.extend([0x2C7C, 0x0100, 0x0000])  # MOVE.L #imm32, A6

# Instruction 1 after normal A6 set
# move.l #0x11111111, %d0
code.extend([0x203C, 0x1111, 0x1111])  # MOVE.L #imm32, D0

# Instruction 2 after normal A6 set
# move.l #0x22222222, %d1
code.extend([0x223C, 0x2222, 0x2222])  # MOVE.L #imm32, D1

# Now set A6 to the problematic boundary address
# move.l #0x02000000, %a6
code.extend([0x2C7C, 0x0200, 0x0000])  # MOVE.L #imm32, A6

# === CRITICAL TEST SECTION ===
# These instructions should execute but Unicorn was skipping them

# Instruction 1 after boundary A6 set (WAS BEING SKIPPED)
# move.l #0x33333333, %d2
code.extend([0x243C, 0x3333, 0x3333])  # MOVE.L #imm32, D2

# Instruction 2 after boundary A6 set (WAS BEING SKIPPED)
# move.l #0x44444444, %d3
code.extend([0x263C, 0x4444, 0x4444])  # MOVE.L #imm32, D3

# Instruction 3 after boundary A6 set (WAS BEING SKIPPED)
# move.l #0x55555555, %d4
code.extend([0x283C, 0x5555, 0x5555])  # MOVE.L #imm32, D4

# Instruction 4 after boundary A6 set (WAS BEING SKIPPED)
# move.l #0x66666666, %d5
code.extend([0x2A3C, 0x6666, 0x6666])  # MOVE.L #imm32, D5

# === END CRITICAL SECTION ===

# Compute test result in D7
# Each register that has the expected value adds to the score
# moveq #0, %d7

# Check D0 (should be 0x11111111)
# cmp.l #0x11111111, %d0
code.extend([0xB0BC, 0x1111, 0x1111])  # CMP.L #imm32, D0
# bne +2
code.extend([0x6602])  # BNE.S +4
# addq.l #1, %d7
code.extend([0x5287])  # ADDQ.L #1, D7

# Check D1 (should be 0x22222222)
# cmp.l #0x22222222, %d1
code.extend([0xB2BC, 0x2222, 0x2222])  # CMP.L #imm32, D1
# bne +2
code.extend([0x6602])  # BNE.S +4
# addq.l #1, %d7
code.extend([0x5287])  # ADDQ.L #1, D7

# Check D2 (should be 0x33333333 - THIS WAS SKIPPED IN BUG)
# cmp.l #0x33333333, %d2
code.extend([0xB4BC, 0x3333, 0x3333])  # CMP.L #imm32, D2
# bne +2
code.extend([0x6602])  # BNE.S +4
# addq.l #1, %d7
code.extend([0x5287])  # ADDQ.L #1, D7

# Check D3 (should be 0x44444444 - THIS WAS SKIPPED IN BUG)
# cmp.l #0x44444444, %d3
code.extend([0xB6BC, 0x4444, 0x4444])  # CMP.L #imm32, D3
# bne +2
code.extend([0x6602])  # BNE.S +4
# addq.l #1, %d7
code.extend([0x5287])  # ADDQ.L #1, D7

# Check D4 (should be 0x55555555 - THIS WAS SKIPPED IN BUG)
# cmp.l #0x55555555, %d4
code.extend([0xB8BC, 0x5555, 0x5555])  # CMP.L #imm32, D4
# bne +2
code.extend([0x6602])  # BNE.S +4
# addq.l #1, %d7
code.extend([0x5287])  # ADDQ.L #1, D7

# Check D5 (should be 0x66666666 - THIS WAS SKIPPED IN BUG)
# cmp.l #0x66666666, %d5
code.extend([0xBABC, 0x6666, 0x6666])  # CMP.L #imm32, D5
# bne +2
code.extend([0x6602])  # BNE.S +4
# addq.l #1, %d7
code.extend([0x5287])  # ADDQ.L #1, D7

# Set result marker based on D7
# D7 should be 6 if all tests pass
# cmp.l #6, %d7
code.extend([0xBEBC, 0x0000, 0x0006])  # CMP.L #imm32, D7
# bne +6
code.extend([0x6606])  # BNE.S +8
# move.l #0xDEADBEEF, %d0  ; All tests passed
code.extend([0x203C, 0xDEAD, 0xBEEF])  # MOVE.L #imm32, D0
# bra +4
code.extend([0x6004])  # BRA.S +6
# move.l #0xBADC0DE0, %d0  ; Some tests failed
code.extend([0x203C, 0xBADC, 0x0DE0])  # MOVE.L #imm32, D0

# Signal completion with EmulOp
code.extend([0x7101])  # EmulOp: SHUTDOWN

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
with open('a6_skip_test.rom', 'wb') as f:
    f.write(rom)

print(f"Created a6_skip_test.rom ({ROM_SIZE} bytes)")
print(f"  Initial SP: 0x{initial_sp:08X}")
print(f"  Initial PC: 0x{initial_pc:08X}")
print(f"  ROM Version: 0x{rom_version:04X} (Quadra)")
print(f"  Test ROM Magic: 0x{TEST_ROM_MAGIC:08X} ('A6SP')")
print(f"  Entry point: offset 0x{initial_pc:X}")
print(f"  Test code size: {len(code) * 2} bytes")
print()
print("Test Description:")
print("  - Sets A6 to normal address (0x01000000)")
print("  - Executes 2 instructions (set D0, D1)")
print("  - Sets A6 to boundary address (0x02000000)")
print("  - Attempts to execute 4 more instructions (set D2, D3, D4, D5)")
print("  - Checks all registers have expected values")
print()
print("Expected Results:")
print("  - D7 = 6 if all instructions executed")
print("  - D0 = 0xDEADBEEF if all tests pass")
print("  - D0 = 0xBADC0DE0 if some instructions were skipped")
print()
print("Bug Behavior (if present):")
print("  - D7 < 6 (likely 2) if instructions after A6=0x02000000 are skipped")
print("  - D2-D5 will be 0 instead of expected values")