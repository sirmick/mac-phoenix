# UAE vs Unicorn-m68k

Side-by-side of the two m68k backends. UAE is the end-user default;
Unicorn is for validation, research, and tooling.

## Architecture

**UAE** is the WinUAE m68k interpreter. Direct access to the register
struct (`regs.regs[]`), inline opcode dispatch, optional JIT
(`--jit`/`--no-jit`), prefetch buffer disabled. Exceptions are built
internally by `Exception()`.

**Unicorn-m68k** is QEMU TCG via Unicorn Engine. Translation blocks
JIT-compiled into x86, register access through `uc_reg_*` API, hooks
fire at block boundaries (`UC_HOOK_BLOCK`) and on
exceptions / illegal instructions (`UC_HOOK_INTR`,
`UC_HOOK_INSN_INVALID`). Trap entry is built manually in host code via
deferred register updates; see [`cpu/ALineAndFLineStatus.md`](cpu/ALineAndFLineStatus.md).

## Feature matrix

| Feature | UAE | Unicorn-m68k |
|---------|-----|--------------|
| Boot Mac OS 7.5.5 / 7.6.1 | ~5 s | ~12 s |
| `0x71xx` EmulOps | `op_illg` | `UC_HOOK_INSN_INVALID` |
| `0xAExx` A-line EmulOps | `op_illg` | `UC_HOOK_INTR` |
| `0xA000+` Toolbox traps | `Exception()` | deferred-update path |
| `0xF000+` FPU traps | `Exception()` | deferred-update path |
| Interrupt delivery | `SPCFLAG_INT` per instruction | `UC_HOOK_BLOCK` poll |
| RTE | native interpreter | patched in QEMU's `cpu-exec.c` |
| VBR | native | custom `UC_M68K_REG_CR_VBR` |
| Cycle counting | accurate (disabled) | approximate |
| Prefetch queue | optional (off) | not modelled |
| ROM patching | direct memory write | direct memory write |

Both backends populate the same 87 entries in the OS trap table from
the same EmulOp dispatch sequence and reach identical state at every
boot-progress checkpoint.

## Why the perf gap

QEMU TCG compilation cost dominates — first-time compilation of ~2.8 M
unique guest code addresses, ~17 µs each, accounts for most of the
boot-time gap. Detailed breakdown in
[`../UnicornPerformanceAnalysis.md`](../UnicornPerformanceAnalysis.md).

Other contributors:

- m68k condition codes are lazy in QEMU; every branch materialises 5–10
  host instructions to compute the test.
- Average TB is 1.95 instructions ([`cpu/JitBlockSizeAnalysis.md`](cpu/JitBlockSizeAnalysis.md)),
  so per-TB entry/exit overhead is a large fraction of useful work.
- m68k is a less-optimised QEMU target than ARM/x86.

The structural pieces aren't easily fixable. The remaining tunable
items live in the perf doc.

## When to use which

- **UAE** — running the emulator. Default.
- **Unicorn-m68k** — when you need an independent m68k implementation
  to validate against UAE, or when you're working on the QEMU TCG
  layer.
- **DualCPU** (`--backend dualcpu`) — when you suspect a UAE CPU bug.
  Runs both in lockstep, fails fast on register divergence.

## Files

- `src/cpu/cpu_uae.c`, `src/cpu/uae_cpu/`, `src/cpu/uae_wrapper.{cpp,h}`
- `src/cpu/cpu_unicorn.cpp`, `src/cpu/unicorn_wrapper.{c,h}`,
  `src/cpu/unicorn_exec_loop.c`, `src/cpu/unicorn_exception.c`
- `src/cpu/cpu_dualcpu.c`, `src/cpu/unicorn_validation.cpp`

See also: [`cpu/UaeQuirks.md`](cpu/UaeQuirks.md),
[`cpu/UnicornQuirks.md`](cpu/UnicornQuirks.md),
[`InterruptTimingAnalysis.md`](InterruptTimingAnalysis.md).
