# A-line / F-line trap dispatch (Unicorn-m68k)

How `0xAxxx` (Mac OS Toolbox) and `0xFxxx` (FPU) traps actually reach
their handlers under the Unicorn-m68k backend, given that QEMU
overwrites PC after every `UC_HOOK_INTR` callback.

## The problem in one line

Calling `uc_reg_write(UC_M68K_REG_PC, …)` from inside `UC_HOOK_INTR`
silently does nothing — Unicorn's QEMU backend restores
`exception_next_eip` after the hook returns, clobbering whatever you
wrote.

## The mechanism

Don't fight it — defer.

1. `0xAxxx` / `0xFxxx` raises `UC_ERR_EXCEPTION`.
2. `hook_interrupt()` (`unicorn_wrapper.c`) identifies the opcode and
   runs the EmulOp / trap handler in C++.
3. The handler queues every register change it wants — D0–D7, A0–A7, PC,
   SR — into per-register `deferred_*` slots, with corresponding
   `_valid` bits. Returns from the hook **without** calling
   `uc_emu_stop()`.
4. QEMU restores its own PC (clobbering nothing of ours, because we
   wrote nothing).
5. At the next basic-block boundary, `hook_block()` runs
   `apply_deferred_updates_and_flush()`:

```c
for (int i = 0; i < 8; i++) {
    if (deferred_dregs_valid & (1 << i))
        uc_reg_write(uc, UC_M68K_REG_D0 + i, &deferred_dregs[i]);
    if (deferred_aregs_valid & (1 << i))
        uc_reg_write(uc, UC_M68K_REG_A0 + i, &deferred_aregs[i]);
}
if (deferred_pc_valid) uc_reg_write(uc, UC_M68K_REG_PC, &deferred_pc);
if (deferred_sr_valid) {
    uint32_t sr32 = deferred_sr;     /* must be uint32_t — see UnicornQuirks */
    uc_reg_write(uc, UC_M68K_REG_SR, &sr32);
}
/* clear all valid bits */
```

By the time we get here, QEMU has finished its post-hook PC restoration,
and our deferred PC write is the one that takes effect. Execution
resumes at the trap handler.

## Trap classes

| Range | Source | Dispatch |
|-------|--------|----------|
| `0x71xx` | UAE EmulOp encoding | `UC_HOOK_INSN_INVALID` (Unicorn raises `UC_ERR_INSN_INVALID`) |
| `0xAE00..0xAE3F` | BasiliskII A-line EmulOps | `UC_HOOK_INTR` (A-line exception) |
| `0xA000..0xAFFF` | Mac OS Toolbox traps | `UC_HOOK_INTR`; trap_handler builds the m68k exception frame in deferred state |
| `0xF000..0xFFFF` | FPU | `UC_HOOK_INTR`; same path as A-line |

UAE patches the m68k ROM with `0x71xx`; Unicorn-m68k patches with
`0xAExx` to reuse the A-line exception path. ROM patching in
`src/core/rom_patches.cpp` checks the active backend.

## Why we don't use QEMU's native trap delivery

`do_interrupt_m68k_aline` etc. would build the exception frame
internally, but reaching it requires accessing `CPUM68KState` through
opaque `uc_struct` pointers at offsets that vary by build. The
deferred-update approach uses only the public Unicorn API
(`uc_reg_*`, `uc_mem_*`) and is portable across compiler / arch /
Unicorn version. See the rationale in
[`../PlatformAPIInterrupts.md`](../PlatformAPIInterrupts.md).

## Boot parity

Both backends populate the same 87 entries in the OS trap table from
the same 16 K+ EmulOp dispatches and reach identical state at every
boot-progress checkpoint, including `$0b78 = 0xfd89ffff`.

## Files

- `src/cpu/unicorn_wrapper.c` — hook surface, deferred-update arrays,
  `apply_deferred_updates_and_flush`.
- `src/cpu/cpu_unicorn.cpp` — backend installer, MMIO, memory map.
- `src/cpu/unicorn_exec_loop.c` — `unicorn_execute_with_interrupts`.
- `src/cpu/unicorn_exception.c` — A-line dispatch into `op_illg`.
- `src/cpu/unicorn_validation.cpp` — DualCPU lockstep glue.
