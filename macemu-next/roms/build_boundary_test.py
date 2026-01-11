#!/usr/bin/env python3
"""
Build boundary_test.rom - Test ROM for RAM/ROM boundary edge cases

This ROM simulates what PATCH_BOOT_GLOBS does:
- Sets A6 to RAM/ROM boundary (0x02000000)
- Tests memory accesses at and around the boundary
- Uses EmulOps for debugging output

ROM structure (Quadra-compatible):
  0x00: Initial SP = 0x01FFFE00 (near end of 32MB RAM)
  0x04: Initial PC = 0x00000100 (entry point)
  0x08: ROM Version = 0x067C (Quadra ROM version for CheckROM)
  0x10: Test ROM Magic = "BOUN" (0x424F554E)
  0x100: Test code
"""

import struct

# ROM size (1MB like Quadra)
ROM_SIZE = 1024 * 1024

# Test ROM magic signature
TEST_ROM_MAGIC = 0x424F554E  # "BOUN" in ASCII

# Create ROM buffer
rom = bytearray(ROM_SIZE)

# Header (Mac ROM compatible)
initial_sp = 0x01FFFE00  # Near end of 32MB RAM
initial_pc = 0x0000002A  # Standard Mac ROM entry point at offset 0x2A

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

# Test code starts at 0x2A (standard Mac ROM entry)
code = []

# Set up registers similar to PATCH_BOOT_GLOBS scenario
# move.l #0x01FFFFE4, %a4    ; A4 pointing near end of RAM (Boot Globals)
code.extend([0x287C, 0x01FF, 0xFFE4])  # MOVE.L #imm32, A4

# move.l #0x01FFFFE4, %a6    ; A6 initially same as A4
code.extend([0x2C7C, 0x01FF, 0xFFE4])  # MOVE.L #imm32, A6

# Simulate PATCH_BOOT_GLOBS writes
# move.l %a4, %a0
code.extend([0x204C])  # MOVE.L A4, A0

# sub.l #20, %a0              ; A0 = A4 - 20 (MemTop location)
code.extend([0x91FC, 0x0000, 0x0014])  # SUBA.L #20, A0

# move.l #0x02000000, (%a0)  ; Write MemTop = RAMBaseMac + RAMSize
code.extend([0x20BC, 0x0200, 0x0000])  # MOVE.L #imm32, (A0)

# THE CRITICAL OPERATION: Set A6 to RAM boundary
# move.l #0x02000000, %a6    ; A6 = boundary address
code.extend([0x2C7C, 0x0200, 0x0000])  # MOVE.L #imm32, A6

# Log that we set A6 (using EmulOp 0x7001 - DebugStr)
# move.l #0xA6A6A6A6, %d0    ; Marker value
code.extend([0x203C, 0xA6A6, 0xA6A6])  # MOVE.L #imm32, D0
code.extend([0x7001])  # EmulOp: DebugStr

# Test 1: Read using A6 directly (boundary address)
# move.l (%a6), %d0
code.extend([0x2016])  # MOVE.L (A6), D0
code.extend([0x7001])  # EmulOp: DebugStr

# Test 2: Read before A6 (last RAM location)
# move.l -4(%a6), %d1
code.extend([0x222E, 0xFFFC])  # MOVE.L -4(A6), D1
# move.l %d1, %d0
code.extend([0x2001])  # MOVE.L D1, D0
code.extend([0x7001])  # EmulOp: DebugStr

# Test 3: Read after A6 (into ROM)
# move.l 4(%a6), %d2
code.extend([0x242E, 0x0004])  # MOVE.L 4(A6), D2
# move.l %d2, %d0
code.extend([0x2002])  # MOVE.L D2, D0
code.extend([0x7001])  # EmulOp: DebugStr

# Test 4: Write using A6 offset (simulating Mac ROM behavior)
# move.l #0x12345678, %d3
code.extend([0x263C, 0x1234, 0x5678])  # MOVE.L #imm32, D3
# move.l %d3, 0x100(%a6)     ; Write at A6+0x100 (into ROM space)
code.extend([0x2D43, 0x0100])  # MOVE.L D3, 0x100(A6)

# Test 5: Use A6 as stack pointer (some Mac code might do this)
# move.l %a7, %a5            ; Save original stack
code.extend([0x2A4F])  # MOVE.L A7, A5
# move.l %a6, %a7            ; Use A6 as stack
code.extend([0x2E4E])  # MOVE.L A6, A7
# move.l #0xABCDEF00, -(%a7) ; Push to stack
code.extend([0x2F3C, 0xABCD, 0xEF00])  # MOVE.L #imm32, -(A7)
# move.l (%a7)+, %d4         ; Pop from stack
code.extend([0x281F])  # MOVE.L (A7)+, D4
# move.l %a5, %a7            ; Restore stack
code.extend([0x2E4D])  # MOVE.L A5, A7

# Signal successful completion with EmulOp
code.extend([0x7100])  # EmulOp: SHUTDOWN (success)

# Infinite loop (shouldn't reach)
# bra.s *
code.extend([0x60FE])  # BRA.S -2 (loop to self)

# Write code to ROM at 0x2A (Mac ROM entry point)
offset = 0x2A
for word in code:
    if isinstance(word, int) and word <= 0xFFFF:
        struct.pack_into('>H', rom, offset, word)
        offset += 2
    else:
        raise ValueError(f"Invalid instruction word: {word}")

# Write ROM file
with open('boundary_test.rom', 'wb') as f:
    f.write(rom)

print(f"Created boundary_test.rom ({ROM_SIZE} bytes)")
print(f"  Initial SP: 0x{initial_sp:08X}")
print(f"  Initial PC: 0x{initial_pc:08X}")
print(f"  ROM Version: 0x{rom_version:04X} (Quadra)")
print(f"  Test ROM Magic: 0x{TEST_ROM_MAGIC:08X} ('BOUN')")
print(f"  Entry point: offset 0x{initial_pc:X}")
print(f"  Test code size: {len(code) * 2} bytes")

# Verify
with open('boundary_test.rom', 'rb') as f:
    header = f.read(20)
    sp = struct.unpack('>I', header[0:4])[0]
    pc = struct.unpack('>I', header[4:8])[0]
    version = struct.unpack('>H', header[8:10])[0]
    magic = struct.unpack('>I', header[16:20])[0]

print(f"\nVerification:")
print(f"  Initial SP: 0x{sp:08X} ✓" if sp == initial_sp else f"  Initial SP: 0x{sp:08X} ✗")
print(f"  Initial PC: 0x{pc:08X} ✓" if pc == initial_pc else f"  Initial PC: 0x{pc:08X} ✗")
print(f"  ROM Version: 0x{version:04X} ✓" if version == rom_version else f"  ROM Version: 0x{version:04X} ✗")
print(f"  Test ROM Magic: {'BOUN'} ✓" if magic == TEST_ROM_MAGIC else f"  Test ROM Magic: ✗")