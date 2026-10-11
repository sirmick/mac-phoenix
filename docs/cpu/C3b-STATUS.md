# C3b: the Power Mac's 68k code on Musashi, where it stands

Status 2026-10-11: **in progress, does not boot.** Experimental behind
`--core mame-ppc --m68k-host 3`, off by default. The milestone is in
[PLAN.md](PLAN.md) (C3b); the code is in `src/cpu/kpx/cpu_ppc_mame.cpp`
(`c3b_*`).

## The goal

On a Power Mac most of Mac OS is PowerPC code, but a large part is still
68k code, run by an emulator that lives in the ROM and is part of the
nanokernel. C3b replaces that emulator with Musashi (`mame-68k`): faster
on hosts where Musashi is compiled natively, debuggable, and the same 68k
core the 68k machines use. The emulator stays in charge of what is tied to
the PowerPC side: Mixed Mode switches, its native A-line trap dispatch,
and interrupt delivery.

## What the ROM's 68k emulator turned out to be

Measured on the G3 ROM with probes (since removed):

* **Opcode table** at the address in KernelData `+0x1074` (`0x50480000`):
  8 bytes per 68k opcode, the PowerPC operation then a branch back to the
  dispatch loop.
* **Dispatch loop** at the emulator code base (`KD+0x1078`) + `0x6084`:
  `rlwimi r29,r27,3,0x7FFF8 ; mtlr r29 ; lhau r27,2(r24) ; bgelr cr2 ;
  b <interrupt path>`. It expects r27 = the opcode, sign-extended, and r24
  = its address; a slot runs with r24 past the opcode and r27 the next
  word. An odd PC selects a second table, for address errors.
* **68k registers**: D0-D7 in r8-r15, A0-A6 in r16-r22, A7 in r1, the SR's
  high byte in r25, **VBR in r28** (the exception builder loads the vector
  from r28 + offset; it stays 0).
* **The CCR**: X in r26 bit 29 (`0x20000000`), N and Z in CR0 LT and EQ, V
  and C in XER OV and CA. CR2 SO is the supervisor bit; CR2 LT/GT/EQ are
  the pending-interrupt bits the host sets. Confirmed both by `move #ccr`
  probes and by reading how the emulator packs the SR into exception
  frames.
* **Words the emulator handles itself**: A-line traps (its native trap
  dispatch), `$AAFE` (Mixed Mode), `$FE00`-`$FE42` (its own operations,
  for example `$FE07` in exception stubs, plus SheepShaver's EMUL_RETURN,
  EXEC_RETURN and EXEC_NATIVE), and every undefined opcode, including
  `$71xx` (MOVEQ with bit 8 set), which go through its exception builder
  with vector `$10`.

## How C3b works

1. At the ROM's reset EmulOp, the opcode table is patched: 51,207 of the
   65,536 slots start with a host call (native op 63, `0x18000FC2`). A-line,
   `$FExx` and the undefined-opcode slots are left alone.
2. When the emulator dispatches a patched slot, the host call imports the
   68k state from the emulator's registers into Musashi and runs Musashi
   from that opcode.
3. Musashi runs until it reaches a word the emulator must handle (A-line,
   non-EmulOp `$FExx`, an undefined opcode), or until the PowerPC side has
   an interrupt (TriggerInterrupt stops Musashi). SheepShaver's EmulOps
   (`$FE43` and up) run inline, as in C3a.
4. It then exports the state back into the emulator's registers and resumes
   the emulator at its dispatch loop, pointed at the next word. The
   emulator checks for interrupts and dispatches that word itself; the next
   ordinary opcode comes straight back to Musashi.

Supporting changes made for it: `cpu::Host::illegal()` and
`OpResult::Illegal` (a host can decline an illegal or host-op word and let
the CPU take its exception, or hand it elsewhere); `$71xx` words are
illegal instructions on the Power Mac, not host ops.

## Where it fails

* **From reset:** the boot runs about 2,300 hand-backs, then Musashi runs
  the ROM's 68k trap dispatcher (vector `$28`, `0x50012af0`) and `rts`es
  into a stub on the stack (`0x003ffbb0`, a `$71xx` word). The emulator
  raises the illegal-instruction exception for it, and the Mac lands in the
  ROM's error path (the debugger nub's VIA polling loop). What the stack
  holds differs from what the emulator path would have left there, so the
  divergence is earlier. It is the same divergence C3a's mode 2 met when
  Musashi ran that dispatcher, still unexplained.
* **Switched on after the desktop is up:** the Mac keeps running (about
  50,000 entries a second, nearly all ending at a hand-back), but Musashi
  runs stay nested 69 deep and never unwind. Not understood yet.

## Fixed along the way

Each of these broke the boot until found:

* **VBR**: the ROM's `movec ...,vbr` relocated Musashi's vectors while the
  emulator kept them at 0. Musashi now takes VBR from r28 at every entry.
* **`$FE07` and its kin**: Musashi took a line-F exception for the
  emulator's own operations; they are handed back.
* **Undefined opcodes**: the adapter treated `$71xx` as a host call (a
  no-op) where the emulator raises an exception; they are handed back.
* **Patch timing**: patching mid-interrupt (at the first IRQ EmulOp) mixed
  the two engines inside one exception; the patch moved to reset.

## Next step

A differential trace of the trap dispatcher: from one saved state, run the
same routine on the emulator (through `execute_68k` with the table
unpatched) and on Musashi, and compare register state instruction by
instruction until they differ. That names the instruction or state they
disagree on. Effort unknown until that point is found.

## After C3b

C3c (boot time and a 68k-heavy benchmark, Musashi against the emulator)
waits on C3b booting. `--jit68k` (straggler S15, already a no-op) would be
retired by C3b.
