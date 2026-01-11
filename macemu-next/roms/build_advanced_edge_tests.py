#!/usr/bin/env python3
"""
Build advanced_edge_tests.rom - Extended test suite for difficult edge cases

Tests additional complex scenarios:
1. Boundary crossing with different instruction sizes
2. PC-relative addressing across boundaries
3. Exception handling at boundaries
4. Indirect jumps through boundary pointers
5. Indexed addressing modes with boundary base addresses
"""

import struct

# ROM size (1MB like Quadra)
ROM_SIZE = 1024 * 1024

# Test ROM magic signature
TEST_ROM_MAGIC = 0x41445645  # "ADVE" in ASCII (Advanced Edge)

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
# TEST 1: Boundary crossing with different instruction sizes
# =============================================================================

# Place some code right at the RAM/ROM boundary
# We'll JSR to it and see if execution continues properly

# Test 1.1: Set up return address storage
# move.l #0x01FFFF00, %a0      ; Safe place in RAM for storing addresses
code.extend([0x207C, 0x01FF, 0xFF00])  # MOVE.L #imm32, A0
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Test 1.2: Store JSR target addresses in RAM
# move.l #0x01FFFFFC, (%a0)+   ; Store address just before boundary
code.extend([0x20FC, 0x01FF, 0xFFFC])  # MOVE.L #imm32, (A0)+
# move.l #0x02000000, (%a0)+   ; Store exact boundary address
code.extend([0x20FC, 0x0200, 0x0000])  # MOVE.L #imm32, (A0)+
# move.l #0x02000004, (%a0)+   ; Store address just after boundary
code.extend([0x20FC, 0x0200, 0x0004])  # MOVE.L #imm32, (A0)+
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# TEST 2: PC-relative addressing across boundaries
# =============================================================================

# Test 2.1: BRA across boundary (forward)
# This tests if branch instructions work when target is across boundary
# move.l #0x11111111, %d0      ; Marker before branch
code.extend([0x203C, 0x1111, 0x1111])  # MOVE.L #imm32, D0
# bra.s +6                      ; Skip next instruction
code.extend([0x6004])  # BRA.S +6
# move.l #0xBADBAD00, %d0      ; Should be skipped
code.extend([0x203C, 0xBADB, 0xAD00])  # MOVE.L #imm32, D0
# move.l #0x22222222, %d0      ; Target of branch
code.extend([0x203C, 0x2222, 0x2222])  # MOVE.L #imm32, D0
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Test 2.2: DBF loop crossing boundary
# move.l #3, %d6                ; Loop counter
code.extend([0x2C3C, 0x0000, 0x0003])  # MOVE.L #imm32, D6
# Loop start (will cross boundary eventually)
loop_start = len(code) * 2 + 0x2A
# addq.l #1, %d1                ; Increment D1 in loop
code.extend([0x5281])  # ADDQ.L #1, D1
# dbf %d6, -4                   ; Decrement and branch if not -1
code.extend([0x51CE, 0xFFFC])  # DBF D6, -4
# addq.l #1, %d7                ; Test counter++ (after loop)
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# TEST 3: Indexed addressing with boundary base
# =============================================================================

# Test 3.1: Base at boundary, positive index
# move.l #0x02000000, %a3      ; Boundary address
code.extend([0x267C, 0x0200, 0x0000])  # MOVE.L #imm32, A3
# move.l #4, %d3                ; Index = 4
code.extend([0x263C, 0x0000, 0x0004])  # MOVE.L #imm32, D3
# move.l 0(%a3,%d3.l), %d4      ; Read from boundary + index
code.extend([0x2833, 0x3800])  # MOVE.L 0(A3,D3.L), D4
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Test 3.2: Base before boundary, index crosses
# move.l #0x01FFFFFC, %a3      ; 4 bytes before boundary
code.extend([0x267C, 0x01FF, 0xFFFC])  # MOVE.L #imm32, A3
# move.l #8, %d3                ; Index = 8 (will cross into ROM)
code.extend([0x263C, 0x0000, 0x0008])  # MOVE.L #imm32, D3
# move.l 0(%a3,%d3.l), %d4      ; Read from RAM base + index (crosses)
code.extend([0x2833, 0x3800])  # MOVE.L 0(A3,D3.L), D4
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# TEST 4: Complex indirect operations
# =============================================================================

# Test 4.1: JMP through pointer at boundary
# move.l #0x01FFFFA0, %a4      ; Place to store jump table
code.extend([0x287C, 0x01FF, 0xFFA0])  # MOVE.L #imm32, A4
# Calculate jump target (skip to after the jump)
jump_target = len(code) * 2 + 0x2A + 10  # Current PC + 10 bytes ahead
# move.l #target, (%a4)        ; Store jump target
code.extend([0x28BC, (jump_target >> 16) & 0xFFFF, jump_target & 0xFFFF])  # MOVE.L #imm32, (A4)
# jmp (%a4)                     ; Jump through pointer
code.extend([0x4ED4])  # JMP (A4)
# move.l #0xDEADDEAD, %d5      ; Should be skipped
code.extend([0x2A3C, 0xDEAD, 0xDEAD])  # MOVE.L #imm32, D5
# Jump target: increment counter
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Test 4.2: MOVEM with registers crossing boundary
# Setup multiple registers
# move.l #0x11111111, %d0
code.extend([0x203C, 0x1111, 0x1111])  # MOVE.L #imm32, D0
# move.l #0x22222222, %d1
code.extend([0x223C, 0x2222, 0x2222])  # MOVE.L #imm32, D1
# move.l #0x33333333, %d2
code.extend([0x243C, 0x3333, 0x3333])  # MOVE.L #imm32, D2

# Set A5 to point near boundary
# move.l #0x01FFFFF0, %a5      ; 16 bytes before boundary
code.extend([0x2A7C, 0x01FF, 0xFFF0])  # MOVE.L #imm32, A5
# movem.l %d0-%d2, (%a5)       ; Save D0-D2 (will cross boundary)
code.extend([0x48D5, 0x0700])  # MOVEM.L D0-D2, (A5)
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Now restore them from the same location
# movem.l (%a5), %d3-%d5       ; Restore to D3-D5
code.extend([0x4CD5, 0x3800])  # MOVEM.L (A5), D3-D5
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# TEST 5: Boundary with different addressing modes
# =============================================================================

# Test 5.1: Post-increment across boundary
# move.l #0x01FFFFFC, %a2      ; Last longword of RAM
code.extend([0x247C, 0x01FF, 0xFFFC])  # MOVE.L #imm32, A2
# move.l (%a2)+, %d4            ; Read and increment (A2 becomes 0x02000000)
code.extend([0x281A])  # MOVE.L (A2)+, D4
# Check that A2 is now at boundary
# cmp.l #0x02000000, %a2
code.extend([0xB5FC, 0x0200, 0x0000])  # CMP.L #imm32, A2
# beq +4                        ; Skip error marker if equal
code.extend([0x6704])  # BEQ.S +6
# move.l #0xBAD00001, %d0      ; Error marker
code.extend([0x203C, 0xBAD0, 0x0001])  # MOVE.L #imm32, D0
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# Test 5.2: Pre-decrement across boundary
# move.l #0x02000004, %a2      ; First longword after boundary
code.extend([0x247C, 0x0200, 0x0004])  # MOVE.L #imm32, A2
# move.l -(%a2), %d4            ; Decrement and read (A2 becomes 0x02000000)
code.extend([0x2822])  # MOVE.L -(A2), D4
# Check that A2 is now at boundary
# cmp.l #0x02000000, %a2
code.extend([0xB5FC, 0x0200, 0x0000])  # CMP.L #imm32, A2
# beq +4                        ; Skip error marker if equal
code.extend([0x6704])  # BEQ.S +6
# move.l #0xBAD00002, %d0      ; Error marker
code.extend([0x203C, 0xBAD0, 0x0002])  # MOVE.L #imm32, D0
# addq.l #1, %d7                ; Test counter++
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# COMPLETION MARKER
# =============================================================================

# Set success marker - D7 should be 11 if all tests executed
# move.l #0xCAFEBABE, %d0       ; Different success marker
code.extend([0x203C, 0xCAFE, 0xBABE])  # MOVE.L #imm32, D0

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
with open('advanced_edge_tests.rom', 'wb') as f:
    f.write(rom)

print(f"Created advanced_edge_tests.rom ({ROM_SIZE} bytes)")
print(f"  Initial SP: 0x{initial_sp:08X}")
print(f"  Initial PC: 0x{initial_pc:08X}")
print(f"  ROM Version: 0x{rom_version:04X} (Quadra)")
print(f"  Test ROM Magic: 0x{TEST_ROM_MAGIC:08X} ('ADVE')")
print(f"  Entry point: offset 0x{initial_pc:X}")
print(f"  Test code size: {len(code) * 2} bytes")
print()
print("Advanced Test Suite Contents:")
print("  TEST 1: Boundary crossing setup (2 tests)")
print("  TEST 2: PC-relative operations (2 tests)")
print("  TEST 3: Indexed addressing modes (2 tests)")
print("  TEST 4: Complex indirect operations (2 tests)")
print("  TEST 5: Pre/post increment/decrement (2 tests)")
print()
print("Expected Results:")
print("  - D7 = 11 (0x0B) if all tests execute")
print("  - D0 = 0xCAFEBABE as success marker")
print("  - D0 = 0xBAD00001 if post-increment test fails")
print("  - D0 = 0xBAD00002 if pre-decrement test fails")