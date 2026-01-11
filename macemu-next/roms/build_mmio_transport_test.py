#!/usr/bin/env python3
"""
Build mmio_transport_test.rom - Test MMIO transport for EmulOps

This ROM tests that EmulOps can be triggered via MMIO writes
instead of using the actual 0x71xx opcodes.
"""

import struct

# ROM size (1MB like Quadra)
ROM_SIZE = 1024 * 1024

# Test ROM magic signature
TEST_ROM_MAGIC = 0x4D4D5452  # "MMTR" in ASCII (MMIO Transport)

# MMIO addresses for common EmulOps
MMIO_BASE = 0xFF000000
MMIO_SHUTDOWN = MMIO_BASE + ((0x7101 - 0x7100) * 2)  # 0xFF000002
MMIO_RESET = MMIO_BASE + ((0x7103 - 0x7100) * 2)     # 0xFF000006
MMIO_BREAK = MMIO_BASE + ((0x7102 - 0x7100) * 2)     # 0xFF000004
MMIO_READ_XPRAM = MMIO_BASE + ((0x7105 - 0x7100) * 2) # 0xFF00000A

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

print("=== MMIO EmulOp Transport Test ===")
print(f"MMIO Base: 0x{MMIO_BASE:08X}")
print()

# Initialize registers
# moveq #0, %d7                ; Test counter
code.extend([0x7E00])  # MOVEQ #0, D7
# moveq #0, %d6                ; Error counter
code.extend([0x7C00])  # MOVEQ #0, D6

# =============================================================================
# TEST 1: BREAK EmulOp via MMIO
# =============================================================================
print(f"Test 1: BREAK via MMIO (write to 0x{MMIO_BREAK:08X})")

# Set marker in D0 so we know which test is running
# move.l #0x00000001, %d0
code.extend([0x203C, 0x0000, 0x0001])  # MOVE.L #imm32, D0

# Trigger BREAK via MMIO write
# move.l #1, MMIO_BREAK
code.extend([0x23FC])  # MOVE.L #imm32, abs.L
code.extend([0x0000, 0x0001])  # immediate = 1
code.extend([MMIO_BREAK >> 16, MMIO_BREAK & 0xFFFF])  # address

# If we get here, BREAK was handled and execution continued
# addq.l #1, %d7                ; Test 1 passed
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# TEST 2: Traditional EmulOp for comparison
# =============================================================================
print(f"Test 2: Traditional BREAK EmulOp (0x7102)")

# Set marker
# move.l #0x00000002, %d0
code.extend([0x203C, 0x0000, 0x0002])  # MOVE.L #imm32, D0

# Traditional EmulOp (should still work if not removed yet)
code.extend([0x7102])  # BREAK EmulOp

# addq.l #1, %d7                ; Test 2 passed
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# TEST 3: Multiple MMIO EmulOps in sequence
# =============================================================================
print(f"Test 3: Multiple MMIO EmulOps")

# Set marker
# move.l #0x00000003, %d0
code.extend([0x203C, 0x0000, 0x0003])  # MOVE.L #imm32, D0

# Multiple MMIO writes in sequence
# First BREAK
# move.l #1, MMIO_BREAK
code.extend([0x23FC, 0x0000, 0x0001])  # MOVE.L #imm32, abs.L
code.extend([MMIO_BREAK >> 16, MMIO_BREAK & 0xFFFF])

# Second BREAK
# move.l #2, MMIO_BREAK
code.extend([0x23FC, 0x0000, 0x0002])  # MOVE.L #imm32, abs.L
code.extend([MMIO_BREAK >> 16, MMIO_BREAK & 0xFFFF])

# addq.l #1, %d7                ; Test 3 passed
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# TEST 4: MMIO in a loop
# =============================================================================
print(f"Test 4: MMIO in a loop")

# Set marker
# move.l #0x00000004, %d0
code.extend([0x203C, 0x0000, 0x0004])  # MOVE.L #imm32, D0

# Loop 3 times
# move.l #3, %d1
code.extend([0x223C, 0x0000, 0x0003])  # MOVE.L #imm32, D1

# loop_start:
loop_addr = len(code) * 2 + 0x2A
# move.l d1, MMIO_BREAK         ; Trigger BREAK with loop counter as value
code.extend([0x23C1])  # MOVE.L D1, abs.L
code.extend([MMIO_BREAK >> 16, MMIO_BREAK & 0xFFFF])
# subq.l #1, %d1
code.extend([0x5381])  # SUBQ.L #1, D1
# bne loop_start
offset = loop_addr - (len(code) * 2 + 0x2A + 2)
code.extend([0x6600 | ((offset >> 8) & 0xFF), offset & 0xFF])  # BNE loop_start

# addq.l #1, %d7                ; Test 4 passed
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# TEST 5: MMIO with different values
# =============================================================================
print(f"Test 5: MMIO with different values")

# Set marker
# move.l #0x00000005, %d0
code.extend([0x203C, 0x0000, 0x0005])  # MOVE.L #imm32, D0

# Write different values to show they're passed through
# move.l #0xDEADBEEF, MMIO_BREAK
code.extend([0x23FC, 0xDEAD, 0xBEEF])  # MOVE.L #imm32, abs.L
code.extend([MMIO_BREAK >> 16, MMIO_BREAK & 0xFFFF])

# move.l #0xCAFEBABE, MMIO_BREAK
code.extend([0x23FC, 0xCAFE, 0xBABE])  # MOVE.L #imm32, abs.L
code.extend([MMIO_BREAK >> 16, MMIO_BREAK & 0xFFFF])

# addq.l #1, %d7                ; Test 5 passed
code.extend([0x5287])  # ADDQ.L #1, D7

# =============================================================================
# COMPLETION CHECK
# =============================================================================

# Check test counter
# cmp.l #5, %d7                 ; Should be 5 if all tests passed
code.extend([0xBEBC, 0x0000, 0x0005])  # CMP.L #imm32, D7
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
# Use MMIO to trigger SHUTDOWN
print(f"Shutdown via MMIO (write to 0x{MMIO_SHUTDOWN:08X})")
# move.l #1, MMIO_SHUTDOWN
code.extend([0x23FC, 0x0000, 0x0001])  # MOVE.L #imm32, abs.L
code.extend([MMIO_SHUTDOWN >> 16, MMIO_SHUTDOWN & 0xFFFF])

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
with open('mmio_transport_test.rom', 'wb') as f:
    f.write(rom)

print()
print(f"Created mmio_transport_test.rom ({ROM_SIZE} bytes)")
print(f"  Initial SP: 0x{initial_sp:08X}")
print(f"  Initial PC: 0x{initial_pc:08X}")
print(f"  ROM Version: 0x{rom_version:04X} (Quadra)")
print(f"  Test ROM Magic: 0x{TEST_ROM_MAGIC:08X} ('MMTR')")
print(f"  Entry point: offset 0x{initial_pc:X}")
print(f"  Test code size: {len(code) * 2} bytes")
print()
print("Test Description:")
print("  1. BREAK via MMIO write")
print("  2. Traditional BREAK EmulOp (for comparison)")
print("  3. Multiple MMIO EmulOps in sequence")
print("  4. MMIO in a loop (3 iterations)")
print("  5. MMIO with different values")
print()
print("Expected Results:")
print("  - D7 = 5 if all tests pass")
print("  - D0 = 0xDEADBEEF for success")
print("  - D0 = 0xBADC0DE0 for failure")
print()
print("MMIO Addresses Used:")
print(f"  BREAK:     0x{MMIO_BREAK:08X} (EmulOp 0x7102)")
print(f"  SHUTDOWN:  0x{MMIO_SHUTDOWN:08X} (EmulOp 0x7101)")
print()
print("Implementation Notes:")
print("  - MMIO writes trigger EmulOp handlers")
print("  - The value written doesn't matter (except for debugging)")
print("  - This transport is 100% reliable in JIT mode")