#!/usr/bin/env python3
"""
Build mmio_test.rom - Test MMIO as alternative to EmulOps

This test shows how MMIO (Memory-Mapped I/O) can replace EmulOps
for more reliable emulator communication in JIT mode.
"""

import struct

# ROM size (1MB like Quadra)
ROM_SIZE = 1024 * 1024

# Test ROM magic signature
TEST_ROM_MAGIC = 0x4D4D494F  # "MMIO" in ASCII

# MMIO base address (high memory, unlikely to conflict)
MMIO_BASE = 0xFF000000

# MMIO command offsets
CMD_SHUTDOWN    = 0x00
CMD_RESET       = 0x04
CMD_GET_TICKS   = 0x08
CMD_DEBUG_PRINT = 0x0C
CMD_READ_XPRAM  = 0x10
CMD_WRITE_XPRAM = 0x14
CMD_TEST_ECHO   = 0x18  # Write a value, read it back

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

print("=== MMIO Test Suite ===")
print(f"MMIO Base: 0x{MMIO_BASE:08X}")
print()

# =============================================================================
# TEST 1: Basic MMIO Write (Debug Print)
# =============================================================================
print("Test 1: MMIO Debug Print")

# move.l #0x11111111, %d0      ; Value to print
code.extend([0x203C, 0x1111, 0x1111])  # MOVE.L #imm32, D0
# move.l %d0, (MMIO_BASE + CMD_DEBUG_PRINT)
code.extend([0x23C0])  # MOVE.L D0, abs.L
code.extend([(MMIO_BASE + CMD_DEBUG_PRINT) >> 16, (MMIO_BASE + CMD_DEBUG_PRINT) & 0xFFFF])
# addq.l #1, %d7                ; Test 1 completed
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# TEST 2: MMIO Read (Get Timer Ticks)
# =============================================================================
print("Test 2: MMIO Read Timer")

# move.l (MMIO_BASE + CMD_GET_TICKS), %d1
code.extend([0x2239])  # MOVE.L abs.L, D1
code.extend([(MMIO_BASE + CMD_GET_TICKS) >> 16, (MMIO_BASE + CMD_GET_TICKS) & 0xFFFF])
# tst.l %d1                     ; Check we got something
code.extend([0x4A81])  # TST.L D1
# beq fail                      ; Skip if zero (unlikely for timer)
code.extend([0x6704])  # BEQ.S +6
# addq.l #1, %d7                ; Test 2 completed
code.extend([0x5287])  # ADDQ.L #1, D7
# bra continue
code.extend([0x6002])  # BRA.S +4
# fail:
# nop                           ; Failed to get timer
code.extend([0x4E71])  # NOP
# continue:

# =============================================================================
# TEST 3: MMIO Echo Test (Write then Read)
# =============================================================================
print("Test 3: MMIO Echo Test")

# move.l #0xCAFEBABE, %d2       ; Test value
code.extend([0x243C, 0xCAFE, 0xBABE])  # MOVE.L #imm32, D2
# move.l %d2, (MMIO_BASE + CMD_TEST_ECHO)  ; Write to echo register
code.extend([0x23C2])  # MOVE.L D2, abs.L
code.extend([(MMIO_BASE + CMD_TEST_ECHO) >> 16, (MMIO_BASE + CMD_TEST_ECHO) & 0xFFFF])
# move.l (MMIO_BASE + CMD_TEST_ECHO), %d3  ; Read it back
code.extend([0x2639])  # MOVE.L abs.L, D3
code.extend([(MMIO_BASE + CMD_TEST_ECHO) >> 16, (MMIO_BASE + CMD_TEST_ECHO) & 0xFFFF])
# cmp.l %d2, %d3                ; Should match
code.extend([0xB682])  # CMP.L D2, D3
# bne fail2
code.extend([0x6604])  # BNE.S +6
# addq.l #1, %d7                ; Test 3 completed
code.extend([0x5287])  # ADDQ.L #1, D7
# bra continue2
code.extend([0x6002])  # BRA.S +4
# fail2:
# nop                           ; Echo failed
code.extend([0x4E71])  # NOP
# continue2:

# =============================================================================
# TEST 4: MMIO in middle of instruction sequence (no TB issues)
# =============================================================================
print("Test 4: MMIO mid-block execution")

# Regular instructions building up a TB
# move.l #0x10000000, %a0
code.extend([0x207C, 0x1000, 0x0000])  # MOVE.L #imm32, A0
# move.l #0x20000000, %a1
code.extend([0x227C, 0x2000, 0x0000])  # MOVE.L #imm32, A1
# move.l #0x30000000, %a2
code.extend([0x247C, 0x3000, 0x0000])  # MOVE.L #imm32, A2

# MMIO operation in middle of block
# move.l #0x44444444, (MMIO_BASE + CMD_DEBUG_PRINT)
code.extend([0x23FC, 0x4444, 0x4444])  # MOVE.L #imm32, abs.L
code.extend([(MMIO_BASE + CMD_DEBUG_PRINT) >> 16, (MMIO_BASE + CMD_DEBUG_PRINT) & 0xFFFF])

# More instructions after MMIO
# move.l #0x40000000, %a3
code.extend([0x267C, 0x4000, 0x0000])  # MOVE.L #imm32, A3
# move.l #0x50000000, %a4
code.extend([0x287C, 0x5000, 0x0000])  # MOVE.L #imm32, A4

# Check that all registers were set (MMIO didn't break execution flow)
# cmp.l #0x10000000, %a0
code.extend([0xB1FC, 0x1000, 0x0000])  # CMPA.L #imm32, A0
# bne fail3
code.extend([0x6614])  # BNE.S +22
# cmp.l #0x50000000, %a4
code.extend([0xB9FC, 0x5000, 0x0000])  # CMPA.L #imm32, A4
# bne fail3
code.extend([0x660C])  # BNE.S +14
# addq.l #1, %d7                ; Test 4 completed
code.extend([0x5287])  # ADDQ.L #1, D7
# bra continue3
code.extend([0x6008])  # BRA.S +10
# fail3:
# move.l #0xBAD00004, %d0       ; Error marker
code.extend([0x203C, 0xBAD0, 0x0004])  # MOVE.L #imm32, D0
# bra continue3
code.extend([0x6002])  # BRA.S +4
# continue3:
# nop
code.extend([0x4E71])  # NOP

# =============================================================================
# TEST 5: MMIO in conditional path
# =============================================================================
print("Test 5: MMIO in conditional execution")

# Set condition
# moveq #1, %d4
code.extend([0x7801])  # MOVEQ #1, D4
# tst.b %d4
code.extend([0x4A04])  # TST.B D4
# beq skip_mmio                 ; Won't branch (D4=1)
code.extend([0x6708])  # BEQ.S +10

# MMIO in conditional path
# move.l #0x55555555, (MMIO_BASE + CMD_DEBUG_PRINT)
code.extend([0x23FC, 0x5555, 0x5555])  # MOVE.L #imm32, abs.L
code.extend([(MMIO_BASE + CMD_DEBUG_PRINT) >> 16, (MMIO_BASE + CMD_DEBUG_PRINT) & 0xFFFF])
# bra done_cond
code.extend([0x6006])  # BRA.S +8

# skip_mmio:
# move.l #0xBAD00005, %d0       ; Should not execute
code.extend([0x203C, 0xBAD0, 0x0005])  # MOVE.L #imm32, D0

# done_cond:
# addq.l #1, %d7                ; Test 5 completed
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# COMPLETION CHECK
# =============================================================================

# Check test counter
# cmp.l #5, %d7                 ; Should be 5 if all tests passed
code.extend([0xBEBC, 0x0000, 0x0005])  # CMP.L #imm32, D7
# bne fail_final
code.extend([0x6606])  # BNE.S +8

# Success
# move.l #0xDEADBEEF, %d0
code.extend([0x203C, 0xDEAD, 0xBEEF])  # MOVE.L #imm32, D0
# bra shutdown
code.extend([0x6004])  # BRA.S +6

# fail_final:
# move.l #0xBADC0DE0, %d0       ; Failure marker
code.extend([0x203C, 0xBADC, 0x0DE0])  # MOVE.L #imm32, D0

# shutdown:
# Use traditional EmulOp for shutdown (for now)
code.extend([0x7101])  # EmulOp: SHUTDOWN

# Alternative: Use MMIO for shutdown
# move.l #1, (MMIO_BASE + CMD_SHUTDOWN)
# code.extend([0x23FC, 0x0000, 0x0001])  # MOVE.L #1, abs.L
# code.extend([(MMIO_BASE + CMD_SHUTDOWN) >> 16, (MMIO_BASE + CMD_SHUTDOWN) & 0xFFFF])

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
with open('mmio_test.rom', 'wb') as f:
    f.write(rom)

print()
print(f"Created mmio_test.rom ({ROM_SIZE} bytes)")
print(f"  Initial SP: 0x{initial_sp:08X}")
print(f"  Initial PC: 0x{initial_pc:08X}")
print(f"  ROM Version: 0x{rom_version:04X} (Quadra)")
print(f"  Test ROM Magic: 0x{TEST_ROM_MAGIC:08X} ('MMIO')")
print(f"  Entry point: offset 0x{initial_pc:X}")
print(f"  Test code size: {len(code) * 2} bytes")
print()
print("Test Description:")
print("  1. MMIO Debug Print - Write to MMIO region")
print("  2. MMIO Timer Read - Read from MMIO region")
print("  3. MMIO Echo Test - Write and read back")
print("  4. MMIO mid-block - MMIO in middle of instruction sequence")
print("  5. MMIO conditional - MMIO in conditional path")
print()
print("Expected Results:")
print("  - D7 = 5 if all tests pass")
print("  - D0 = 0xDEADBEEF for success")
print("  - D0 = 0xBADC0DE0 for failure")
print()
print("Advantages over EmulOps:")
print("  - Always trapped, even mid-TB")
print("  - No opcode ambiguity")
print("  - Clean memory-mapped interface")
print("  - Can pass parameters and return values")
print("  - Works identically in interpreter and JIT modes")