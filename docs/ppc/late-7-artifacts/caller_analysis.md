# late-7b: PStrToCStr caller analysis and primary-repro pivot

## Routine entry is 0x500c6190, not 0x500c6198

The previous late-7 note described the routine as starting at `0x500c6198`.
In fact the real entry is 8 bytes earlier. The first 4 instructions pop
the return address and string pointer from the stack (which the caller
set up via `SUBQ.L #4, A7` + `PEA str; JSR trampoline`):

```
0x500c6190: 225f         MOVEA.L (A7)+, A1     ; pop return addr
0x500c6192: 201f         MOVE.L  (A7)+, D0     ; pop string ptr
0x500c6194: 2e80         MOVE.L  D0, (A7)      ; overwrite the reserved
                                                ; out-slot with D0 (so caller
                                                ; can read back the C-string ptr)
0x500c6196: 6712         BEQ.B   $500c61aa     ; if str_ptr == 0, skip work
0x500c6198: 2040         MOVEA.L D0, A0
0x500c619a: 7000         MOVEQ   #0, D0
0x500c619c: 1010         MOVE.B  (A0), D0      ; D0 = length byte
0x500c619e: 6004         BRA.S   $500c61a4
0x500c61a0: 10e8 0001    MOVE.B  1(A0), (A0)+  ; shift-left body
0x500c61a4: 51c8 fffa    DBF     D0, $500c61a0
0x500c61a8: 4210         CLR.B   (A0)          ; null-terminate
0x500c61aa: 4ed1         JMP     (A1)
```

## Calling convention

- Push nothing to reserve the return slot: `SUBQ.L #4, A7`  (caller)
- Push the Pascal string pointer: `PEA ...` or `MOVE.L ...,-(A7)`  (caller)
- `JSR <trampoline>`  (caller, implicit push of return addr)
- Routine pops both into A1/D0, and stashes the (in-place-converted)
  C-string pointer into the out-slot for the caller to read back.

## Two trampolines reach 0x500c6190

```
ROM@0x06e040  (loaded 0x5006e040):  60ff 0005814e  = BRA.L 0x500c6190
ROM@0x051080  (loaded 0x50051080):  60ff 0007510e  = BRA.L 0x500c6190
```

## Three ROM caller sites

Found by scanning for BRA/BSR/JSR{abs,PC-rel} that target either
trampoline. No direct branch in the 4 MB ROM reaches `0x500c6190`
(or `0x500c6198`); only via trampolines.

### Caller A — 0x50051040
```
0x051038: 3e1f       MOVE.W (A7)+, D7
0x05103a: 6626       BNE.B +38
0x05103c: 598f       SUBQ.L #4, A7
0x05103e: 2f0b       MOVE.L A3, -(A7)        ; pushes A3 as str_ptr
0x051040: 4eba 003e  JSR    0x50051080       ; → trampoline
```

### Caller B — 0x500517f0
```
0x0517e0: 4e56 fffc  LINK A6, #-4
0x0517e4: 48e7 0300  MOVEM.L D6/D7, -(A7)
0x0517ea: 598f       SUBQ.L #4, A7
0x0517ec: 486e 0008  PEA    8(A6)            ; pushes A6+8 as str_ptr
0x0517f0: 4eba f88e  JSR    0x50051080       ; → trampoline
```

### Caller C — 0x5006df14   (matches the A-trap A0=0x08 bug)
```
0x06df0a: 598f       SUBQ.L #4, A7
0x06df0c: 206e ffda  MOVEA.L -38(A6), A0     ; A0 = *(A6-38)
0x06df10: 4868 0008  PEA    8(A0)            ; pushes A0+8 as str_ptr
0x06df14: 4eba 012a  JSR    0x5006e040       ; → trampoline
```

If `*(A6-38) == 0`, caller C pushes `8` as the string pointer. The
routine then reads the byte @0x08 of the vector table as "length",
gets `0x50`, and runs 80 shift iterations — exactly the late-7 A-trap
bomb signature.

## This session's primary repro is NOT the A-trap path

Across 6 parallel `SCALE=1` boots with
`MACEMU_PPC_TRACE_68K_ENTRY=0x500c6198,0x5006df14,0x50051040,0x500517f0`:

- 4/6 runs corrupt; none matched the late-6/7 pattern (PPC PC
  `0x50461a74`, 68k PC `0x500c61a6`, writes starting @0x08).
- All 4 corrupt runs show the **same different pattern**:
  - PPC PC: `0x50461e94` (`stb r4, 0(r22)`)
  - 68k PC: ~`0x500ce210`
  - 68k A-regs at first write: **A6=0, A7=0**, A0=1, A3=1
  - 68k D-regs: D0=0x80, D6=0x01, D1=0x00ff0200
  - Write lands at lomem @0x00 (single byte val=0x80), then @0x400

## The 68k routine with A6 as buffer base

```
0x500ce1e0..0x500ce1fe:  (preamble — builds D0 from D1/A0/D5 via ADD/AND/SWAP)
0x500ce208: 0640 0080    ADDI.W #0x80, D0
0x500ce20c: 1c80         MOVE.B D0, (A6)           ; write D0.B @ A6
0x500ce20e: 0c2e 00e9 0800  CMPI.B #0xe9, 0x800(A6)
0x500ce214: 6704         BEQ.B  +4
0x500ce216: 1d40 0400    MOVE.B D0, 0x400(A6)      ; write D0.B @ A6+0x400
0x500ce21a: 102e 0800    MOVE.B 0x800(A6), D0
0x500ce21e: 0c00 00bc    CMPI.B #0xbc, D0
0x500ce222: 6706         BEQ.B  +6
0x500ce224: 0c00 00bb    CMPI.B #0xbb, D0
0x500ce228: 6612         BNE.B  +18
0x500ce22a: 102e 0804    MOVE.B 0x804(A6), D0
```

A6 is treated as a base pointer into a buffer with meaningful offsets
at `0`, `0x400`, `0x800`, `0x804`. Likely a CLUT/palette builder or
similar 2–4 KB table initializer. A6=0 is not legal.

## Current thinking

The late-7 A-trap bomb and this `@0x00` CLUT corruption are likely
two downstream symptoms of the same upstream A-register divergence.
Whichever routine runs first with bad A-regs produces the visible
corruption. Hunt the divergence, not the symptoms.

Two LEGIT PStrToCStr calls captured by the tracer (A0 = 0x45540,
D0 = 0x45548 = A0+8, A1 = 0x5006df18 = return addr into caller-C)
confirm the expected calling convention. On the bug path one of the
callers must push 0x08 or 0x00 instead of a real string pointer; most
likely caller-C with `*(A6-38) == 0`.

## Next-session targets

1. Arm `MACEMU_PPC_TRACE_68K_ENTRY=0x500ce1e0,0x500ce20c,0x5006df14`
   and see A6/A7 at the CLUT-builder entry and caller-C's A0 at time
   of `PEA 8(A0)`.
2. Walk the PPC-block ring back from a corrupting run to find the
   last EmulOp or native op that cleared A6/A7 to zero.
3. Cross-check with KPX: at the same 68k PC boundary, what are A6/A7?

## Commands used

```
MACEMU_PPC_TRACE_68K_ENTRY=0x5006df14,0x50051040,0x500517f0,0x500c6198 \
MACEMU_PPC_TICK_PERIOD_SCALE=1 \
timeout 45 ./build/mac-phoenix --backend unicorn --arch ppc \
  --no-webserver --port 8992 --signaling-port 9092 --timeout 40
```

Tracer implementation: `src/cpu/cpu_unicorn_ppc.cpp`, block hook over
the ROM range keyed on `r24` (68k PC) equality to any of up to 8
targets. Every hit is logged (unlike `MACEMU_PPC_DUMP_PC` which is
first-hit only).
