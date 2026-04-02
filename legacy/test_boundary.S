| Test ROM for RAM/ROM boundary edge cases
| This tests accessing memory at the exact boundary where RAM ends and ROM begins
| Assembles with: m68k-elf-as -o test_boundary.o test_boundary.S
| Links with: m68k-elf-ld -Ttext=0x02000000 -o test_boundary.elf test_boundary.o
| Convert to binary: m68k-elf-objcopy -O binary test_boundary.elf test_boundary.rom

    .text
    .org 0x0000

| ROM header (minimal)
rom_start:
    dc.l    0x00000000      | Initial SSP (will be set by emulator)
    dc.l    rom_entry       | Initial PC

| Vectors (minimal set)
    .org 0x0008
    dc.l    unhandled       | Bus error
    dc.l    unhandled       | Address error
    dc.l    illegal_inst    | Illegal instruction
    dc.l    unhandled       | Divide by zero

    .org 0x0028
    dc.l    line_a_trap     | Line-A trap (0xA000-0xAFFF)
    dc.l    line_f_trap     | Line-F trap (0xF000-0xFFFF)

| Test entry point
    .org 0x0100
rom_entry:
    | Test 1: Store A6 value (from PATCH_BOOT_GLOBS scenario)
    move.l  #0x02000000, %a6    | Set A6 to RAM/ROM boundary
    move.l  #0x11111111, %d0    | Test marker 1
    dc.w    0x7001              | EmulOp: DebugStr - log test 1

    | Test 2: Try reading from just before boundary (last RAM address)
    move.l  #0x01FFFFFC, %a0    | Last longword of RAM
    move.l  (%a0), %d0          | Read it
    dc.w    0x7001              | EmulOp: DebugStr - show what we read from last RAM

    | Test 3: Try reading from exact boundary (first ROM address)
    move.l  #0x02000000, %a0    | First longword of ROM
    move.l  (%a0), %d1          | Read it (should read ROM header)
    move.l  %d1, %d0            | Copy to D0 for debug
    dc.w    0x7001              | EmulOp: DebugStr - show what we read from ROM start

    | Test 4: Try reading from just after boundary
    move.l  #0x02000004, %a0    | Second longword of ROM
    move.l  (%a0), %d2          | Read it (should read initial PC)

    | Test 5: Write to last RAM location
    move.l  #0x01FFFFFC, %a0    | Last longword of RAM
    move.l  #0xDEADBEEF, (%a0)  | Write test pattern
    move.l  (%a0), %d3          | Read it back

    | Test 6: Try write to ROM (should fail or be ignored)
    move.l  #0x02000100, %a0    | Somewhere in ROM
    move.l  #0xCAFEBABE, (%a0)  | Try to write
    move.l  (%a0), %d4          | Read it back (should be unchanged)

    | Test 7: Access using A6 as base
    move.l  (%a6), %d5          | Read using A6 (at boundary)
    move.l  -4(%a6), %d6        | Read A6-4 (last RAM)
    move.l  4(%a6), %d7         | Read A6+4 (into ROM)

    | Signal success with EmulOp
    dc.w    0x7100              | EmulOp: Shutdown (success)

    | Infinite loop (shouldn't reach here)
halt:
    bra.s   halt

| Trap handlers
line_a_trap:
    | Log that we hit A-line trap
    move.l  #0xAAAAAAAA, %d0
    rte

line_f_trap:
    | Log that we hit F-line trap
    move.l  #0xFFFFFFFF, %d0
    rte

illegal_inst:
    | Log illegal instruction
    move.l  #0x11111111, %d0
    rte

unhandled:
    | Unhandled exception
    move.l  #0xDEADDEAD, %d0
    rte

| Pad ROM to minimum size (64KB)
    .org 0xFFFC
    dc.l    0xFFFFFFFF      | ROM checksum placeholder