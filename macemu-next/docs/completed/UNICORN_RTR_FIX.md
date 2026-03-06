# Unicorn Backend Debug Investigation

## Status: RESOLVED - Unicorn boots to Finder!

### Root Cause: Missing RTR instruction in Unicorn/QEMU m68k translator

**RTR (Return and Restore Condition Codes, opcode 0x4e77)** was completely missing from
Unicorn's QEMU m68k instruction decoder (`translate.c`). The instruction was treated as
an illegal opcode (EXCP_ILLEGAL), causing:

1. Mac OS illegal instruction handler to fire instead of proper RTR execution
2. CCR (condition code register) never restored from stack
3. BNE instruction after JSR sub2@47840 always saw stale Z=1 flag
4. Finder's Desktop DB initialization loop ran forever

### Fix
Added `DISAS_INSN(rtr)` to `subprojects/unicorn/qemu/target/m68k/translate.c`:
- Pop CCR (word) from stack → `gen_helper_set_ccr()`
- Pop PC (long) from stack → `gen_jmp()`
- Registered opcode: `BASE(rtr, 4e77, ffff)`

### Investigation Timeline
1. Identified Desktop DB loop via stall detector (PC=0x0004783e)
2. Confirmed all system state identical at CHECKLOAD #1824 between UAE and Unicorn
3. Traced RTR at 0x4783e: CCR_on_stack=0x2000 (Z=0) but SR_before=0x2004 (Z=1)
4. Discovered 0x479ca/0x47840 NOT block boundaries → large compiled TB
5. Added EXC-TRACE: confirmed intno=4 (EXCP_ILLEGAL) firing thousands of times at 0x4783e
6. Found RTR completely absent from translate.c opcode table
7. Implemented RTR → Unicorn boots to Finder desktop (2513+ CHECKLOADs)

## Previous Issues (also resolved)
- **CHECKLOAD #299 stall**: Framebuffer placement overlapping WDCB (fixed)
- **FlushCodeCache**: JIT TB invalidation via uc_ctl_flush_tb() (fixed)
