#!/usr/bin/env python3
"""
Build edge_test_suite.rom - Comprehensive test suite for edge cases

Tests various difficult situations:
1. Setting registers to boundary addresses
2. Memory access at boundaries
3. Stack operations at boundaries
4. Jumps/branches across boundaries
5. Indirect addressing through boundary pointers
"""

import struct

# ROM size (1MB like Quadra)
ROM_SIZE = 1024 * 1024

# Test ROM magic signature
TEST_ROM_MAGIC = 0x45444745  # "EDGE" in ASCII

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

# Initialize test counter in D7
# moveq #0, %d7                ; Test counter = 0
code.extend([0x7E00])  # MOVEQ #0, D7

# =============================================================================
# TEST 1: Setting address registers to various boundaries
# =============================================================================

# Test 1.1: Set A0 to last valid RAM address
# move.l #0x01FFFFFC, %a0      ; Last longword of RAM
code.extend([0x207C, 0x01FF, 0xFFFC])  # MOVE.L #imm32, A0
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Test 1.2: Set A1 to RAM/ROM boundary
# move.l #0x02000000, %a1      ; Exact boundary
code.extend([0x227C, 0x0200, 0x0000])  # MOVE.L #imm32, A1
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Test 1.3: Set A2 to first ROM address
# move.l #0x02000004, %a2      ; Just into ROM
code.extend([0x247C, 0x0200, 0x0004])  # MOVE.L #imm32, A2
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Test 1.4: Set A3 to middle of ROM
# move.l #0x02080000, %a3      ; Middle of ROM
code.extend([0x267C, 0x0208, 0x0000])  # MOVE.L #imm32, A3
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Test 1.5: Set A4 to last ROM address
# move.l #0x020FFFFC, %a4      ; Last longword of ROM
code.extend([0x287C, 0x020F, 0xFFFC])  # MOVE.L #imm32, A4
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Test 1.6: Set A5 to unmapped area
# move.l #0x03000000, %a5      ; Beyond ROM
code.extend([0x2A7C, 0x0300, 0x0000])  # MOVE.L #imm32, A5
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Test 1.7: THE PROBLEMATIC ONE - Set A6 to RAM/ROM boundary
# move.l #0x02000000, %a6      ; This causes Unicorn to skip instructions!
code.extend([0x2C7C, 0x0200, 0x0000])  # MOVE.L #imm32, A6
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# TEST 2: Memory reads at boundaries (should execute if bug is fixed)
# =============================================================================

# Test 2.1: Read from last RAM location
# move.l (%a0), %d0             ; Read last RAM longword via A0
code.extend([0x2010])  # MOVE.L (A0), D0
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Test 2.2: Read from boundary address
# move.l (%a1), %d1             ; Read from boundary via A1
code.extend([0x2211])  # MOVE.L (A1), D1
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Test 2.3: Read from first ROM location
# move.l (%a2), %d2             ; Read from ROM via A2
code.extend([0x2412])  # MOVE.L (A2), D2
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# TEST 3: Memory writes at boundaries
# =============================================================================

# Test 3.1: Write to last RAM location
# move.l #0x12345678, (%a0)    ; Write to last RAM
code.extend([0x20BC, 0x1234, 0x5678])  # MOVE.L #imm32, (A0)
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Test 3.2: Try to write to ROM (should be ignored)
# move.l #0xABCDEF00, (%a1)    ; Write to boundary (ROM start)
code.extend([0x22BC, 0xABCD, 0xEF00])  # MOVE.L #imm32, (A1)
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# TEST 4: Stack operations at boundary
# =============================================================================

# Test 4.1: Save original stack pointer
# move.l %a7, %d6               ; Save SP in D6
code.extend([0x2C0F])  # MOVE.L A7, D6

# Test 4.2: Set stack to near boundary
# move.l #0x01FFFFFE, %a7      ; Stack at end of RAM
code.extend([0x2E7C, 0x01FF, 0xFFFE])  # MOVE.L #imm32, A7
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Test 4.3: Push value (will cross into ROM area!)
# move.l #0x55AA55AA, -(%a7)   ; Push, SP will go to 0x01FFFFFA
code.extend([0x2F3C, 0x55AA, 0x55AA])  # MOVE.L #imm32, -(A7)
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Test 4.4: Pop value back
# move.l (%a7)+, %d3            ; Pop into D3
code.extend([0x261F])  # MOVE.L (A7)+, D3
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Test 4.5: Restore stack
# move.l %d6, %a7               ; Restore SP
code.extend([0x2E46])  # MOVE.L D6, A7

# =============================================================================
# TEST 5: Indirect addressing with boundary pointers
# =============================================================================

# Test 5.1: Load effective address at boundary
# lea 0x02000000, %a3          ; Load boundary address
code.extend([0x47F9, 0x0200, 0x0000])  # LEA addr32, A3
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Test 5.2: Use boundary address with offset
# move.l -4(%a3), %d4           ; Read from RAM side of boundary
code.extend([0x282B, 0xFFFC])  # MOVE.L -4(A3), D4
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Test 5.3: Use boundary address with positive offset
# move.l 4(%a3), %d5            ; Read from ROM side of boundary
code.extend([0x2A2B, 0x0004])  # MOVE.L 4(A3), D5
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# COMPLETION MARKER
# =============================================================================

# Set success marker - D7 should be 19 if all tests executed
# move.l #0xDEADBEEF, %d0       ; Success marker
code.extend([0x203C, 0xDEAD, 0xBEEF])  # MOVE.L #imm32, D0

# Signal completion with EmulOp
code.extend([0x7101])  # EmulOp: SHUTDOWN (0x7101, not 0x7100)

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
with open('edge_test_suite.rom', 'wb') as f:
    f.write(rom)

print(f"Created edge_test_suite.rom ({ROM_SIZE} bytes)")
print(f"  Initial SP: 0x{initial_sp:08X}")
print(f"  Initial PC: 0x{initial_pc:08X}")
print(f"  ROM Version: 0x{rom_version:04X} (Quadra)")
print(f"  Test ROM Magic: 0x{TEST_ROM_MAGIC:08X} ('EDGE')")
print(f"  Entry point: offset 0x{initial_pc:X}")
print(f"  Test code size: {len(code) * 2} bytes")
print()
print("Test Suite Contents:")
print("  TEST 1: Setting address registers to boundaries (7 tests)")
print("  TEST 2: Memory reads at boundaries (3 tests)")
print("  TEST 3: Memory writes at boundaries (2 tests)")
print("  TEST 4: Stack operations at boundary (4 tests)")
print("  TEST 5: Indirect addressing with boundaries (3 tests)")
print()
print("Expected Results:")
print("  - D7 = 19 (0x13) if all tests execute")
print("  - D0 = 0xDEADBEEF as success marker")
print()
print("Known Bug:")
print("  - Unicorn skips instructions after setting A6 to 0x02000000")
print("  - D7 will be less than 19 if bug triggers")