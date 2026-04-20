# late-7: Vector-table byte-shift is PStrToCStr called with A0=vector table

## What it is

The 80 `stb r4, 0(r16)` writes at PPC handler `0x50461a74` that overwrite
@0x08..@0x57 are the **correct execution** of a 68k Pascal-string-to-C-string
converter at `0x500c6198`. The routine is correct; **it's being called with
A0 pointing at the vector table instead of a real string**.

## The 68k code

```
0x500c6198:  2040         MOVEA.L D0, A0        ; A0 = D0 (caller passes str ptr in D0)
0x500c619a:  7000         MOVEQ   #0, D0        ; D0 = 0
0x500c619c:  1010         MOVE.B  (A0), D0      ; D0 = length byte
0x500c619e:  6004         BRA.S   $500c61a4     ; jump to DBF
0x500c61a0:  10e8 0001    MOVE.B  1(A0), (A0)+  ; shift byte left, A0++ (LOOP BODY)
0x500c61a4:  51c8 fffa    DBF     D0, $500c61a0 ; dec D0, loop if != -1
0x500c61a8:  4210         CLR.B   (A0)          ; null-terminate
0x500c61aa:  4ed1         JMP     (A1)          ; return
```

## Why the length byte reads as 0x50

A0 points at @0x08 (vec 2 of exception table). First byte @0x08 = 0x50
(MSB of longword 0x50003040, vec 2 handler address). Routine reads this
as "string length" = 80 decimal → runs 80 shift iterations.

## Proof of PPC r4 load site

PC-DUMP of block `0x50488740` (5th in the 6-block cycle):
```
50488740: 7c90d8ae  lbzx r4, r16, r27
50488748: 7c91d8ae  lbzx r4, r17, r27
50488750: 7c92d8ae  lbzx r4, r18, r27
...
```

This is a dispatch table keyed by source An register (r16..r23 = A0..A7).
Entry for A0 loads byte from (A0 + r27). With r27=1 (= d16 of the 68k
MOVE.B 1(A0),(A0)+ instruction), r4 = byte @(A0+1). Store block
`0x50461a60` finishes with `stb r4, 0(r16); addi r16, r16, 1` → writes
that byte at A0, then A0++. Net: in-place byte-shift-left by 1.

## Why KPX boots fine

KPX presumably never calls this routine with A0=@0x08 — Unicorn/KPX
divergence happens EARLIER, and by the time either reaches ROM code
that does "call PStrToCStr with some argument", the argument differs.

## Next-session targets

1. **Find the caller of 0x500c6198** — look at blocks BEFORE the cycle
   in the ring (`0x504b0020 0x50467ec0 0x50467ed4`). These correspond to
   68k instructions that set D0 before calling 0x500c6198.
2. **KPX boundary trace** — run KPX with `MACEMU_PPC_TRACE=/tmp/kpx.log`
   and find the first EmulOp where either backend's D0/A0/A1 values
   diverge in a way that leads to this wrong D0 argument.
3. **Rule out Unicorn TCG miscompile** — the `lbzx r4, r16, r27` load is
   correct on its own; r27=1 is correct (= d16 of MOVE.B opcode 0x10e8
   with immediate word 0x0001). The bug is in the 68k call setup, not
   in PPC execution.
