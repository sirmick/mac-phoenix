# Unicorn-PPC backend — session handoff

Last updated: 2026-04-18 (branch `unicorn-ppc`).

This is a rolling status doc for the Unicorn PPC port. Append to it, don't
rewrite it. The goal: a next-session reader should be able to pick up the
investigation in under five minutes.

## Where we are

The Unicorn PPC backend runs end-to-end on the KPX test disk (Mac OS 7.5.5,
G3 ROM). It does not yet boot to Finder — KPX does, Unicorn stalls writing
`0x500100xx` into `@0x00000000` in a tight loop a few seconds in.

NATIVE_OP dispatch is now routed through `g_platform.ppc_native_op`
(commit `377e1a76`). Both backends call the same backend-agnostic pure
dispatcher `execute_native_op_pure(selector, uint32 gprs[32])` — no more
direct symbol references from `cpu_unicorn_ppc.cpp` into
`libkpx_interp.a`.

## First open divergence

Running the boundary comparator:

```bash
# KPX — reference
MACEMU_PPC_TRACE=/tmp/kpx.log ./build/mac-phoenix \
  --backend kpx --arch ppc --rom ~/storage/roms/g3.rom \
  --disk ~/storage/images/macos-7.5.5.img --ram 128 \
  --no-webserver --timeout 30

# Unicorn — device under test
MACEMU_PPC_TRACE=/tmp/unicorn.log ./build/mac-phoenix \
  --backend unicorn --arch ppc --rom ~/storage/roms/g3.rom \
  --disk ~/storage/images/macos-7.5.5.img --ram 128 \
  --no-webserver --timeout 60

diff /tmp/kpx.log /tmp/unicorn.log | head
```

First divergence is at EmulOp 12 (sel=04, a lowmem-read EmulOp):

```
< 00000012 EMULOP ... cr=40100f22 ...   # KPX
> 00000012 EMULOP ... cr=40500f22 ...   # Unicorn
```

The difference is `0x00400000` — CR1.SO. CR1 is the FP-condition summary.
The same non-determinism recurs at ops 55, 62, 86, 91… and is **pre-existing**
(reproduces on `60a73b81`, the commit before today's refactor, so this is not
caused by the Platform API change).

Hypotheses for the next session:

1. FPSCR initial state differs between backends. QEMU initializes FPSCR
   differently than KPX's interpreter; CR1 copies FPSCR[0..3] on `mcrfs`
   / Rc-form FP ops. Compare `env->fpscr` post-reset on both sides.
2. Timer-driven IRQ fires at a different instruction boundary. The 60Hz
   timer in `timer_interrupt.cpp` may be sensitive to TB register rate;
   Unicorn's `TBL/TBU` increment may not match KPX's.
3. A supervisor-mode register (MSR.FP? MSR.FE0/FE1?) differs at first
   EmulOp entry.

Next step: narrow down with `MACEMU_PPC_CR2_TRACE=<lo>:<hi>` — set the window
around op 12 on both backends and diff the per-instruction CR trace.

Note: `MACEMU_PPC_CR2_TRACE` auto-disables the KPX JIT (see
`src/cpu/kpx/cpu_ppc_kpx.cpp` near the `cr_trace_forces_interp` check),
because the JIT bypasses the `ppc_trace_cr_step` hooks in
`src/cpu/kpx/src/cpu/ppc/ppc-cpu.cpp`. Unicorn always runs TCG so no
gating needed there.

## Deferred work

- **GET_RESOURCE family selectors on Unicorn.** The pure dispatcher returns
  false for NATIVE_GET_RESOURCE / NATIVE_GET_1_RESOURCE / … because those
  re-enter PPC code via `execute_ppc(old_get_resource)`. KPX handles this
  on its CPU instance (`sheepshaver_cpu::execute_native_op`); Unicorn has
  an `uppc_cpu_execute_ppc` entry but we haven't wired it into the
  platform shim yet. When one of these fires, `kpx_ppc_native_op` prints
  `[KPX] ppc_native_op: selector %u (GET_RESOURCE family) needs
  execute_ppc`. Not observed during boot so far.

- **Cursor update on Unicorn** is stubbed (`uppc_ppc_cursor_move` is a
  no-op). Needs an Execute68k path for CursorDeviceDispatch + SheepMem,
  same approach KPX uses.

## Key files

| File | Role |
|---|---|
| `src/cpu/cpu_unicorn_ppc.cpp` | Unicorn backend: memory map, emulop callback, install function |
| `src/cpu/kpx/cpu_ppc_kpx.cpp` | KPX backend + shared `execute_native_op_pure` + `kpx_ppc_native_op` platform shim |
| `src/cpu/kpx/src/cpu/ppc/ppc-cpu.cpp` | KPX interpreter loop; per-instruction CR trace hooks live here |
| `src/common/include/platform.h` | `Platform` function-pointer table (adds `ppc_native_op` at line 229) |
| `src/common/include/ppc_boundary_trace.h` | Shared comparator (EMULOP / POST / CR lines) |
| `subprojects/unicorn/qemu/target/ppc/*` | Unicorn PPC TCG target — only touch if we need new helpers |

## Useful env vars

| Var | Effect |
|---|---|
| `MACEMU_PPC_TRACE=<path>` | Write per-EmulOp boundary-state line pairs (EMULOP + POST) |
| `MACEMU_PPC_CR2_TRACE=<lo>:<hi>` | Per-instruction `[CR]` lines on stderr for EmulOp seq in `[lo, hi)`. Auto-disables KPX JIT. |
| `MACEMU_PPC_TRACE_TRAP=1` | Unicorn-only: log each EXEC_NATIVE dispatch |
| `MACEMU_PPC_NO_IRQ=1` | Mask 60Hz timer IRQ (isolate deterministic path) |

## Rebuild + smoke-test

```bash
cmake --build build -j$(nproc)

# KPX sanity — should reach "Desktop ready" in ~1 second after boot blocks
./build/mac-phoenix --backend kpx --arch ppc --rom ~/storage/roms/g3.rom \
  --disk ~/storage/images/macos-7.5.5.img --ram 128 \
  --no-webserver --timeout 15

# Unicorn — will stall at the 0x00 lowmem write loop for now
./build/mac-phoenix --backend unicorn --arch ppc --rom ~/storage/roms/g3.rom \
  --disk ~/storage/images/macos-7.5.5.img --ram 128 \
  --no-webserver --timeout 30
```
