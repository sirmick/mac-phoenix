| Test ROM simulating PATCH_BOOT_GLOBS behavior
| This simulates what happens when PATCH_BOOT_GLOBS sets A6 = RAMBaseMac + RAMSize

    .text
    .org 0x0000

| ROM header
rom_start:
    dc.l    0x01FFFE00      | Initial SSP (near end of RAM)
    dc.l    rom_entry       | Initial PC

| Skip vectors for simplicity
    .org 0x0100

rom_entry:
    | Set up registers similar to PATCH_BOOT_GLOBS scenario
    move.l  #0x01FFFFE4, %a4    | A4 pointing near end of RAM (Boot Globals location)
    move.l  #0x01FFFFE4, %a6    | A6 initially same as A4

    | Simulate PATCH_BOOT_GLOBS EmulOp (0x7107)
    | This does:
    | - WriteMacInt32(r->a[4] - 20, RAMBaseMac + RAMSize)  // MemTop
    | - WriteMacInt8(r->a[4] - 26, 0)                      // No MMU
    | - WriteMacInt8(r->a[4] - 25, ReadMacInt8(r->a[4] - 25) | 1) // No MMU flag
    | - r->a[6] = RAMBaseMac + RAMSize

    | Write MemTop at A4-20
    move.l  %a4, %a0
    sub.l   #20, %a0            | A0 = A4 - 20
    move.l  #0x02000000, (%a0)  | Write RAMBaseMac + RAMSize (MemTop)

    | Write MMU flags at A4-26 and A4-25
    move.l  %a4, %a0
    sub.l   #26, %a0            | A0 = A4 - 26
    move.b  #0, (%a0)           | Write 0 (No MMU)

    move.l  %a4, %a0
    sub.l   #25, %a0            | A0 = A4 - 25
    move.b  (%a0), %d0          | Read current value
    or.b    #1, %d0             | Set bit 0
    move.b  %d0, (%a0)          | Write back

    | THE CRITICAL OPERATION: Set A6 to RAM boundary
    move.l  #0x02000000, %a6    | A6 = RAMBaseMac + RAMSize (boundary address)

    | Debug: Log that we set A6
    move.l  #0xA6A6A6A6, %d0    | Marker value for debugging
    dc.w    0x7001              | EmulOp: DebugStr (will print D0)

    | Now test what happens when we use A6
    | This simulates what the Mac ROM might do after PATCH_BOOT_GLOBS

    | Test 1: Read using A6 directly
    move.l  (%a6), %d0          | Read from boundary address
    dc.w    0x7001              | EmulOp: DebugStr (will show what we read)

    | Test 2: Read before A6
    move.l  -4(%a6), %d1        | Read last longword of RAM
    move.l  %d1, %d0            | Copy to D0 for debug output
    dc.w    0x7001              | EmulOp: DebugStr

    | Test 3: Read after A6
    move.l  4(%a6), %d2         | Read into ROM
    move.l  %d2, %d0            | Copy to D0 for debug output
    dc.w    0x7001              | EmulOp: DebugStr

    | Test 4: Write using A6 (Mac ROM might do this)
    move.l  #0x12345678, %d3
    move.l  %d3, 0x100(%a6)     | Write at A6+0x100

    | Test 5: Use A6 as stack (some Mac ROM code might do this)
    move.l  %a7, %a5            | Save original stack
    move.l  %a6, %a7            | Use A6 as stack
    move.l  #0xABCDEF00, -(%a7) | Push to stack (will write to RAM)
    move.l  (%a7)+, %d4         | Pop from stack
    move.l  %a5, %a7            | Restore stack

    | Signal successful completion
    dc.w    0x7100              | EmulOp: Shutdown

    | Should not reach here
halt:
    bra.s   halt

| Pad to minimum ROM size
    .org 0xFFFC
    dc.l    0xFFFFFFFF