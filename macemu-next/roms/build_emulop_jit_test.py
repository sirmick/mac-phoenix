#!/usr/bin/env python3
"""
Build emulop_jit_test.rom - Test EmulOp detection and execution with JIT

This test verifies how EmulOps are handled in JIT mode:
1. EmulOps that are illegal instructions (trigger UC_HOOK_INSN_INVALID)
2. EmulOps that are valid M68K instructions (need UC_HOOK_BLOCK detection)
3. EmulOps at different positions in translation blocks
"""

import struct

# ROM size (1MB like Quadra)
ROM_SIZE = 1024 * 1024

# Test ROM magic signature
TEST_ROM_MAGIC = 0x454A4954  # "EJIT" in ASCII (EmulOp JIT test)

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

# Initialize test counter
# moveq #0, %d7                ; Test counter = 0
code.extend([0x7E00])  # MOVEQ #0, D7

# Clear registers for clean test
# moveq #0, %d0
code.extend([0x7000])  # MOVEQ #0, D0
# moveq #0, %d1
code.extend([0x7200])  # MOVEQ #0, D1

print("=== EmulOps in 0x7100-0x713F range ===")

# =============================================================================
# TEST 1: EmulOp at start of a translation block
# =============================================================================

# Force a new TB with an unconditional jump
# jmp next_block
jump_target_1 = 0x02000000 + len(code) * 2 + 0x2A + 6  # ROM base + PC after JMP instruction (6 bytes for JMP)
code.extend([0x4EF9, (jump_target_1 >> 16) & 0xFFFF, jump_target_1 & 0xFFFF])  # JMP abs32

# EmulOp 0x7103 (RESET) - This is MOVEQ #3, D0 (valid M68K)
# Should be caught by UC_HOOK_BLOCK at TB start
print(f"0x7103 (RESET): MOVEQ #3, D0 - Valid M68K, needs block hook")
code.extend([0x7103])  # M68K_EMUL_OP_RESET

# If we get here, EmulOp was handled and execution resumed
# addq.l #1, %d7                ; Test 1 passed
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# TEST 2: EmulOp in middle of translation block
# =============================================================================

# Some regular instructions first (builds up TB)
# move.l #0x11111111, %d0
code.extend([0x203C, 0x1111, 0x1111])  # MOVE.L #imm32, D0
# move.l #0x22222222, %d1
code.extend([0x223C, 0x2222, 0x2222])  # MOVE.L #imm32, D1

# EmulOp 0x7104 (CLKNOMEM) - This is MOVEQ #4, D0 (valid M68K)
print(f"0x7104 (CLKNOMEM): MOVEQ #4, D0 - Valid M68K, mid-block")
code.extend([0x7104])  # M68K_EMUL_OP_CLKNOMEM

# More instructions after
# move.l #0x33333333, %d2
code.extend([0x243C, 0x3333, 0x3333])  # MOVE.L #imm32, D2
# addq.l #1, %d7                ; Test 2 passed
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# TEST 3: Multiple EmulOps in sequence
# =============================================================================

# Force new TB
jump_target_2 = 0x02000000 + len(code) * 2 + 0x2A + 6  # ROM base + PC after JMP instruction
code.extend([0x4EF9, (jump_target_2 >> 16) & 0xFFFF, jump_target_2 & 0xFFFF])  # JMP abs32

# EmulOp sequence
print(f"0x7105 (READ_XPRAM): MOVEQ #5, D0")
code.extend([0x7105])  # M68K_EMUL_OP_READ_XPRAM
print(f"0x7106 (READ_XPRAM2): MOVEQ #6, D0")
code.extend([0x7106])  # M68K_EMUL_OP_READ_XPRAM2
# addq.l #1, %d7                ; Test 3 passed
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# TEST 4: EmulOps that are actually illegal (0xA-line, 0xF-line style)
# =============================================================================

# A-line trap (0xAxxx) - Always illegal
print(f"0xA000: A-line trap - Always illegal, triggers UC_HOOK_INSN_INVALID")
code.extend([0xA000])  # A-line trap

# If trap was handled properly, execution continues
# addq.l #1, %d7                ; Test 4 passed
code.extend([0x5287])  # ADDQ.L #1, D7

# F-line trap (0xFxxx) - Always illegal
print(f"0xF000: F-line trap - Always illegal, triggers UC_HOOK_INSN_INVALID")
code.extend([0xF000])  # F-line trap

# If trap was handled properly, execution continues
# addq.l #1, %d7                ; Test 5 passed
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# TEST 5: EmulOp after setting A6 to boundary (original bug scenario)
# =============================================================================

# Set A6 to RAM/ROM boundary
# move.l #0x02000000, %a6
code.extend([0x2C7C, 0x0200, 0x0000])  # MOVE.L #imm32, A6

# EmulOp right after boundary set
print(f"0x7107 (PATCH_BOOT_GLOBS): MOVEQ #7, D0 - After A6 boundary")
code.extend([0x7107])  # M68K_EMUL_OP_PATCH_BOOT_GLOBS

# Continue with more instructions
# move.l #0x44444444, %d3
code.extend([0x263C, 0x4444, 0x4444])  # MOVE.L #imm32, D3
# addq.l #1, %d7                ; Test 6 passed
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# TEST 6: EmulOp in a conditional path
# =============================================================================

# Test conditional execution with EmulOps
# tst.l %d3
code.extend([0x4A83])  # TST.L D3
# beq skip_emulop               ; Should not branch (D3=0x44444444)
code.extend([0x6704])  # BEQ.S +6

# EmulOp in conditional path
print(f"0x7108 (FIX_BOOTSTACK): MOVEQ #8, D0 - In conditional path")
code.extend([0x7108])  # M68K_EMUL_OP_FIX_BOOTSTACK
# bra continue
code.extend([0x6002])  # BRA.S +4

# skip_emulop:
# move.l #0xBADBAD00, %d4       ; Should not execute
code.extend([0x283C, 0xBADB, 0xAD00])  # MOVE.L #imm32, D4

# continue:
# addq.l #1, %d7                ; Test 7 passed
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# TEST 7: Illegal EmulOp values (outside valid range)
# =============================================================================

# These should be treated as normal MOVEQ instructions, not EmulOps
# 0x7140 is outside EmulOp range (0x7100-0x713F)
print(f"0x7140: MOVEQ #64, D0 - Outside EmulOp range, normal instruction")
code.extend([0x7140])  # MOVEQ #64, D0 (not an EmulOp)

# Check that D0 has 64 (0x40), not an EmulOp side effect
# cmp.b #64, %d0
code.extend([0x0C00, 0x0040])  # CMPI.B #64, D0
# bne error
code.extend([0x6604])  # BNE.S +6
# addq.l #1, %d7                ; Test 8 passed
code.extend([0x5287])  # ADDQ.L #1, D7
# bra done
code.extend([0x6004])  # BRA.S +6

# error:
# move.l #0xBAD00008, %d0       ; Error marker
code.extend([0x203C, 0xBAD0, 0x0008])  # MOVE.L #imm32, D0

# done:

# =============================================================================
# COMPLETION CHECK
# =============================================================================

# Check test counter
# cmp.l #8, %d7                 ; Should be 8 if all tests passed
code.extend([0xBEBC, 0x0000, 0x0008])  # CMP.L #imm32, D7
# bne fail
code.extend([0x6606])  # BNE.S +8

# Success
# move.l #0xDEADBEEF, %d0
code.extend([0x203C, 0xDEAD, 0xBEEF])  # MOVE.L #imm32, D0
# bra shutdown
code.extend([0x6004])  # BRA.S +6

# fail:
# move.l #0xBADC0DE0, %d0       ; Failure marker
code.extend([0x203C, 0xBADC, 0x0DE0])  # MOVE.L #imm32, D0

# shutdown:
# EmulOp: SHUTDOWN
code.extend([0x7101])  # M68K_EMUL_OP_SHUTDOWN

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
with open('emulop_jit_test.rom', 'wb') as f:
    f.write(rom)

print()
print(f"Created emulop_jit_test.rom ({ROM_SIZE} bytes)")
print(f"  Initial SP: 0x{initial_sp:08X}")
print(f"  Initial PC: 0x{initial_pc:08X}")
print(f"  ROM Version: 0x{rom_version:04X} (Quadra)")
print(f"  Test ROM Magic: 0x{TEST_ROM_MAGIC:08X} ('EJIT')")
print(f"  Entry point: offset 0x{initial_pc:X}")
print(f"  Test code size: {len(code) * 2} bytes")
print()
print("Test Description:")
print("  1. EmulOp at TB start (0x7103 RESET)")
print("  2. EmulOp mid-block (0x7104 CLKNOMEM)")
print("  3. Multiple EmulOps in sequence (0x7105, 0x7106)")
print("  4. A-line trap (0xA000)")
print("  5. F-line trap (0xF000)")
print("  6. EmulOp after A6 boundary set (0x7107)")
print("  7. EmulOp in conditional path (0x7108)")
print("  8. Non-EmulOp MOVEQ (0x7140)")
print()
print("Expected Results:")
print("  - D7 = 8 if all tests pass")
print("  - D0 = 0xDEADBEEF for success")
print("  - D0 = 0xBADC0DE0 for failure")
print()
print("How JIT handles EmulOps:")
print("  1. UC_HOOK_BLOCK checks at each TB start for 0x71xx opcodes")
print("  2. UC_HOOK_INSN_INVALID catches truly illegal instructions (A-line, F-line)")
print("  3. Valid M68K instructions in 0x71xx range need block hook detection")