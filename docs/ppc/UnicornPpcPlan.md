# Unicorn PPC Integration Plan

Integration plan for adding a Unicorn PPC backend alongside the existing KPX
(Kheperix) backend. Unicorn becomes a peer of KPX inside the existing PPC
subprocess, selectable at runtime via `--backend unicorn --arch ppc`. KPX stays
default.

Companion reference: [UnicornPpc.md](UnicornPpc.md) covers the Unicorn PPC API
surface itself. This document is the project-specific integration plan.

## 0. Scope

**Target**: `--backend unicorn --arch ppc` boots the same ROM + disk image that
`--backend kpx --arch ppc` boots, to the Finder.

**Non-goals**:
- Changing the parent/child subprocess split.
- Replacing KPX's `execute_68k` path (the 68k emulator under PPC stays as-is).
- PPC DualCPU lockstep — sketched in §11 as a follow-on.

## 1. The blocker: broken MSR[IR]/MSR[DR] TB invalidation

`subprojects/unicorn/qemu/target/ppc/unicorn.c:36-43`, inside `uc_ppc_store_msr`:

```c
if (((value >> MSR_IR) & 1) != msr_ir ||
    ((value >> MSR_DR) & 1) != msr_dr) {
    // cpu_interrupt_exittb(cs);   // <-- COMMENTED OUT
}
```

QEMU keys translation blocks by `(phys_addr, mmu_mode)`. Flipping IR/DR without
exiting the current TB means the JIT reuses blocks compiled under the old
translation mode. The Mac nanokernel toggles IR/DR constantly during boot and
IRQ servicing — with this stub, execution silently diverges the moment BATs
turn on.

**This must be fixed before the backend can boot.** The fix is the one-liner
that's already there, just uncommented. Audit `misc_helper.c` /
`helper_regs.c` for the same stub on the `mtmsr` instruction helper. Audit
`tlbie`/`tlbsync`/`tlbia` helpers — Unicorn's classic 32-bit PPC
(`POWERPC_MMU_SOFT_6xx`) path is rarely exercised by userspace-emulation
users, so gaps are plausible.

Budget: 0.5 day for the patch, with validation via milestone 2 (§13).

## 2. KPX's interrupt model is not architectural — major simplification

Reading `src/cpu/kpx/cpu_ppc_kpx.cpp:817-861` (`sheepshaver_cpu::interrupt`):

```cpp
void sheepshaver_cpu::interrupt(uint32 entry) {
    uint32 saved_pc = pc(), saved_lr = lr(), saved_ctr = ctr(), saved_sp = gpr(1);
    gpr(1) = SignalStackBase() - 64;
    SheepVar32 trampoline = POWERPC_EXEC_RETURN;   // 0x18000001
    WriteMacInt32(KERNEL_DATA_BASE + 0x004, gpr(1));
    // ... stash gpr(6..13) into KernelData ...
    gpr(10) = trampoline.addr();
    gpr(12) = trampoline.addr();
    gpr(13) = get_cr();
    // ... set up gpr(7), gpr(11), CR ...
    execute(entry);                                 // ROMBase + 0x312b1c
    // EXEC_RETURN trampoline has unwound us; restore saved state
    pc() = saved_pc; lr() = saved_lr; ctr() = saved_ctr; gpr(1) = saved_sp;
}
```

KPX does **not** raise an architectural 0x500 external-interrupt exception. It
calls the nanokernel's IRQ entry as a normal function, using a host-side fake
stack and the sentinel opcode `POWERPC_EXEC_RETURN = 0x18000001` as the
"return address". When the nanokernel finishes and branches to `gpr(10)` /
`gpr(12)`, it hits the sentinel; the EmulOp dispatch catches it, sets
`SPCFLAG_CPU_EXEC_RETURN`, and the interpret loop unwinds.

**Implication**: Unicorn PPC does not need PPC architectural interrupt
delivery. It needs:

- The exact same register + memory setup dance.
- `uc_emu_stop` on the sentinel.
- `uc_emu_start` re-entry at the saved PC after restoring state.

The Unicorn backend's `interrupt(entry)` is a straight port of KPX's, with
`gpr(n) = …` replaced by `uc_reg_write(UC_PPC_REG_n, …)` and `execute(entry)`
replaced by `uc_reg_write(UC_PPC_REG_PC, entry); uc_emu_start(uc, entry, 0, 0, 0)`.

This invalidates the "Option 2: direct exception simulation" suggestion in
[UnicornPpc.md](UnicornPpc.md) — we need neither SRR0/SRR1 save nor vector
dispatch.

## 3. Memory map — match KPX byte-for-byte

REAL_ADDRESSING (Mac addr == host addr, `VMBaseDiff = 0`) is not optional.
Moving RAM anywhere else breaks the nanokernel's pointer arithmetic. The map
Unicorn sees must mirror KPX's.

From `src/core/cpu_context.cpp:423-476` and `src/cpu/kpx/cpu_ppc_kpx.cpp:1197-1206`:

| Region | Mac addr | Host addr | Perms | Unicorn mapping |
|---|---|---|---|---|
| RAM | `0..ram_mb` | same (MAP_FIXED) | RWX | `uc_mem_map_ptr` |
| NK probe pad | just past RAM, 8 MB | same | RW zero | `uc_mem_map_ptr` |
| ROM | `0x50000000..+0x400000` | same | RX | `uc_mem_map_ptr`, `UC_PROT_READ\|EXEC` |
| ROM area tail | up to `+0x500000` | same | RW | `uc_mem_map_ptr` |
| KernelData alias #1 | `0x5fffe000..+0x2000` | SHM-backed | RW | `uc_mem_map_ptr` |
| KernelData alias #2 | `0x68ffe000..+0x2000` | **same SHM**, aliased | RW | `uc_mem_map_ptr`, same host pointer |
| SheepMem | `vm_acquire` (≈`0x10000000`) | same | RW | `uc_mem_map_ptr` |
| VIA / Mac I/O | scattered, inside RAM region | — | — | already covered |

### Two subtleties

**(a) KernelData aliasing.** KPX maps one SHM segment at two Mac addresses
(`0x5fffe000` and `0x68ffe000`). Reproduce with two `uc_mem_map_ptr` calls
over the **same host pointer**. `uc_mem_map` (which allocates) is wrong.

**(b) Deliberately unmapped regions.** `cpu_ppc_kpx.cpp:1197-1206` explicitly
`munmap`s `0x68070000` (DR Emulator) and `0x69000000` (DR Cache) before boot,
because the nanokernel probes those addresses and the *fault* drives it down
the right init path. KPX relies on `SIGSEGV_RETURN_SKIP_INSTRUCTION` leaving
the destination register unchanged.

Under Unicorn we can't cleanly skip one instruction from
`UC_HOOK_MEM_UNMAPPED`. The faithful workaround: register these regions with
`uc_mmio_map` returning 0 on read, ignoring writes. This matches the KPX
behavior as long as the probing code doesn't observe the result in a
register-sensitive way — which holds for the documented probe addresses.
Revisit case-by-case if boot diverges.

**Post-nanokernel remap.** `cpu_ppc_kpx.cpp:1202`: these regions get remapped
as RW RAM "after the nanokernel init completes (first HandleInterrupt)". The
Unicorn backend hooks the first-IRQ boundary (we're already there for §8),
calls `uc_mmio_unmap`, and re-maps as RAM.

## 4. CPU config and initial state

**Model**: `UC_CPU_PPC_603E` or `UC_CPU_PPC_604E`, matching KPX's choice.
`uc_open(UC_ARCH_PPC, UC_MODE_PPC32 | UC_MODE_BIG_ENDIAN, &uc)` then
`uc_ctl_set_cpu_model(uc, …)`.

**Initial state at `ROMBase + 0x310000`**:
- `PC = ROMBase + 0x310000`.
- `MSR` — **must be confirmed empirically**. Add one `fprintf` at KPX's
  `execute(entry)` call site to log MSR/SPRG0-3/HID0 on first entry, run once,
  encode the observed values. Guessing risks obscure faults.
- All GPRs, LR, CTR = 0. `unicorn.c:reg_reset` already does this.
- No BATs, no SDR1 initially — translation is off (MSR[IR]=MSR[DR]=0). The
  nanokernel sets up BATs via `mtspr` from guest code.

**SPRs not exposed by Unicorn PPC from host.** `unicorn.c`'s `reg_read/reg_write`
only covers GPR / FPR / CR / PC / LR / CTR / MSR / XER / FPSCR. SPRG0-3,
HID0/1, DBAT\*, IBAT\*, SDR1, DAR, DSISR are **not writable from host code**.
Boot still works because the nanokernel uses `mtspr` from guest code. Matters
for save/restore (deferred).

## 5. EmulOps — the 0x18xxxxxx dispatch

Confirmed from `execute_sheep` at `src/cpu/kpx/cpu_ppc_kpx.cpp:319-345`:

```
0x18000000 | 0              = EMUL_RETURN   (QuitEmulator)
0x18000000 | 1              = EXEC_RETURN   (set SPCFLAG_CPU_EXEC_RETURN; stop)
0x18000000 | 2              = EXEC_NATIVE   (dispatch NATIVE_OP_field[20:25])
0x18000000 | (selector + 3) = EMUL_OP       (dispatch selector; pc += 4)
NativeOp extended: (POWERPC_EMUL_OP | (FN<<12) | (OP<<6) | 2)
```

Major opcode 6 (`0x18000000`) is reserved in the base PPC ISA. QEMU's decoder
raises a program-check. Two integration choices:

### (a) Decoder patch (preferred)

Add a handler for major opcode 6 in
`subprojects/unicorn/qemu/target/ppc/translate.c` that emits
`gen_helper_mac_emulop(pc, opcode)` — a helper calling back into host C via a
`uc_struct`-stored function pointer. Mirrors the m68k 0xAExx pattern in
`src/cpu/cpu_unicorn.cpp`. Clean and TB-friendly.

### (b) `UC_HOOK_INSN_INVALID` (fallback)

Let QEMU raise program-check, hook the illegal-instruction event, check the
faulting opcode, dispatch if `(opcode >> 26) == 6`. Simpler, but every EmulOp
exits the TB and rebuilds state, and the hook fires *after* QEMU has set up
0x700 vector state that we'd need to undo.

**Go with (a).** ~40 lines of `translate.c` plus a helper.

### Dispatch body

```cpp
void unicorn_ppc_emulop_helper(uint32 opcode, uint32 pc) {
    switch (opcode & 0x3f) {
      case 0: QuitEmulator(); return;
      case 1: uc_emu_stop(uc); return;                          // EXEC_RETURN
      case 2: {                                                  // EXEC_NATIVE
        uint32 sel = (opcode >> 6) & 0x3f;
        execute_native_op(sel);
        if ((opcode >> 12) & 1) set_pc(lr()); else set_pc(pc + 4);
        return;
      }
      default: {                                                 // EMUL_OP
        M68kRegisters r; marshal_ppc_to_m68k_regs(&r);
        g_platform.ppc_emulop_handler(&r, pc, (opcode & 0x3f) - 3);
        marshal_m68k_regs_to_ppc(&r);
        set_pc(pc + 4);
        return;
      }
    }
}
```

`marshal_*_regs` convert between `M68kRegisters{d[8], a[8]}` and GPRs the way
legacy SheepShaver does. The exact GPR↔d/a mapping varies by op — confirm
against `src/cpu/kpx/emul_op_ppc.cpp` before trusting any table.

## 6. Illegal memory access

Three paths, matching §3:

1. **`uc_mmio_map` regions** (VIA, SCC, NuBus, the DR probe addresses during
   boot): callbacks forward to existing host-side handlers.
2. **`UC_HOOK_MEM_UNMAPPED`** for genuinely unexpected accesses: port
   `kpx_sigsegv_handler` logic at `cpu_ppc_kpx.cpp:1119-1179` — ignore ROM
   writes, check known-benign-fault signatures (e.g.,
   `ROMBase + 0x488160 + gpr(20) == 0xf8000000` during MacOS 8 install). On
   unknown fault in Mac code: return `true` to continue, matching KPX's
   "skip all faults in Mac code" default.
3. **Host SIGSEGV inside TCG-generated code**: the existing `sigsegv_handler`
   (`src/main.cpp:588-591`) still catches it. Install a Unicorn-PPC-specific
   variant paralleling KPX's, querying PC via `uc_reg_read(UC_PPC_REG_PC)`.

The global blind-skip handler from `main.cpp` must **not** be left installed
while Unicorn PPC runs. KPX already replaces it via
`kpx_install_sigsegv_handler()` before `execute_fast`; the Unicorn backend
does the same.

## 7. Illegal instruction

Dispatch major-opcode-6 per §5. For actually-illegal opcodes (unknown
encoding, not major-op 6), let QEMU's default program-check delivery run —
the guest may legitimately rely on catching them via the 0x700 handler. Do
not swallow these.

## 8. IRQ injection, TLB boundaries, and the order of operations

### Flow

```
tick thread:
  SetInterruptFlag(INTFLAG_60HZ)
  g_platform.cpu_trigger_interrupt(level)
    -> unicorn_ppc_trigger_interrupt(level):
         set pending_irq = true
         uc_emu_stop(uc)             // thread-safe per Unicorn docs

CPU thread (top-level execute loop):
  uc_emu_start returns
  if (pending_irq && !irq_disabled())
      unicorn_ppc_handle_interrupt()
  else
      uc_emu_start again at current PC
```

`irq_disabled()` = `ReadMacInt32(XLM_IRQ_NEST) > 0` (matches `HandleInterrupt`
gate at `cpu_ppc_kpx.cpp:893`). Check `XLM_RUN_MODE` for the MODE_NATIVE /
MODE_68K / MODE_EMUL_OP split — **that logic is pure host C and reusable
verbatim**; only the "execute nanokernel" line changes from
`ppc_cpu->interrupt(…)` to `unicorn_ppc_interrupt(ROMBase + 0x312b1c)`.

### Save set on injection

PC, LR, CTR, GPR1, plus several KernelData stores
(`KERNEL_DATA_BASE + 0x004`, `0x018`, fields within
`KERNEL_DATA_BASE + 0x65c`'s dereference). All are guest memory writes that
land on the mmap'd host pointer — execute from the CPU thread only to avoid
ordering issues with TCG internals.

### Restore on EXEC_RETURN

Sentinel fires → `uc_emu_stop` → outer loop restores PC/LR/CTR/GPR1 from
saved values. No TB flush needed (no mapping changed).

### TLB / BAT / MSR interaction with IRQ injection — the subtle case

When an interrupt lands with translation on (MSR[IR,DR]=1, BATs covering
ROM and RAM), the KPX model just jumps to `ROMBase + 0x312b1c` with
translation still on. The nanokernel's IBAT covers that address. **We do
not change MSR on injection.**

When an interrupt lands while the 68k emulator is running (MODE_68K),
`HandleInterrupt` doesn't call the nanokernel at all — it writes a memory
flag and lets the 68k emulator pick it up (line 902-903). Still no MSR
change from our side.

**But** the nanokernel's handler itself does `mtmsr` several times. Which
means we're *guaranteed* to hit the §1 bug during the very first IRQ, and
cannot debug IRQs until §1 is fixed.

### Explicit TB flush points the backend must guarantee

Regardless of whether the subproject patch is done, the backend should force
a flush at:

- `mtmsr` / `mtmsrd` when IR, DR, or PR changed.
- `mtspr` to `DBAT[0..3]U/L`, `IBAT[0..3]U/L`, `SDR1`, `HID0`.
- `tlbie` / `tlbia` / `tlbsync`.
- `isync` after any of the above (defensive; cheap; matches how the guest
  uses isync as a serialize barrier).

All translate to "exit TB + invalidate affected pages" in TCG:
`cpu_interrupt_exittb(cs)` plus `tlb_flush(cs)`.

**Perf note**: the nanokernel does tight mtmsr/mtmsr/mtmsr sequences. Flushing
on every mtmsr will hurt; flush only when the value actually changed IR/DR/PR.
Secondary — get correctness first.

## 9. 60 Hz timer

The existing `src/drivers/platform/timer_interrupt.cpp` tick infrastructure
is fine, but note `kpx_cpu_execute_fast` at line 1224-1225 spawns its **own**
tick thread inside `kpx_cpu_execute_fast` rather than using the shared one —
because the KPX child needs to check `XLM_IRQ_NEST` before calling
`trigger_interrupt`.

The Unicorn PPC backend does the same: spawn its own tick thread inside
`unicorn_ppc_execute_fast`, modeled on `cpu_ppc_kpx.cpp:1063-1109`. Reuse the
`tick_inhibit`, `INTFLAG_60HZ`, `INTFLAG_VIA` globals from the shared compat
layer.

**Parity hazard** (per
`memory/feedback_intflag_enum_parity.md`): `INTFLAG_*` values in
`src/common/include/main.h` and `src/cpu/kpx/compat/main.h` must stay
aligned. The Unicorn PPC backend uses the compat header's definitions since
it sits in the same compilation unit family as KPX.

## 10. Backend selection plumbing

### CLI

`src/config/emulator_config.cpp:597-598` currently forces `CPUBackend::KPX`
when `--arch ppc`. Change to: only force if `--backend` wasn't explicitly
passed.

### Install dispatch

`src/core/cpu_context.cpp:605` (PPC init branch) — replace the unconditional
`cpu_ppc_kpx_install(&platform_)` with:

```cpp
switch (config.cpu_backend) {
  case CPUBackend::Unicorn:
    cpu_unicorn_ppc_install(&platform_);
    break;
  case CPUBackend::DualCPU:             // §11, future
    cpu_dualcpu_ppc_install(&platform_);
    break;
  case CPUBackend::KPX:
  default:
    cpu_ppc_kpx_install(&platform_);
    break;
}
```

### File layout

- `src/cpu/cpu_unicorn_ppc.cpp` — new, implements `cpu_unicorn_ppc_install`.
- `src/cpu/unicorn_wrapper.c` — extend engine-agnostic helpers. Fix the
  `unicorn_create_with_model()` hardcoded `UC_ARCH_M68K` noted in
  [UnicornPpc.md §Current Wrapper Issues](UnicornPpc.md).
- `src/cpu/unicorn_wrapper_ppc.c` — new, PPC-specific register accessors,
  mirroring how the m68k wrapper is organized.

The KPX `compat/` headers (`xlowmem.h`, `macos_util.h`, `emul_op.h`,
`thunks.h`, `ppc_memory.h`, `timer.h`) are 90% shared infrastructure and
include unchanged. **Structurally the Unicorn PPC backend is "KPX minus the
`powerpc_cpu` class"**, with Unicorn providing execute + state functions.

## 11. DualCPU PPC — follow-on sketch

KPX as primary, Unicorn as shadow. Compare at EmulOp boundaries — the
`is_primary` flag in `src/common/include/platform.h:206-214` already exists.
Because KPX drives, the shadow doesn't spawn its own tick thread; it receives
the same `trigger_interrupt` calls and mirrors the register + memory setup.
Compare register deltas after each EmulOp.

Do not attempt per-instruction compare — KPX's interpreter and Unicorn's TCG
don't agree on inter-instruction CR/XER update timing, only on architectural
end-of-insn state.

## 12. Subproject patches needed

Concrete edits to `subprojects/unicorn/qemu/target/ppc/`:

1. `unicorn.c:36-43`: uncomment `cpu_interrupt_exittb(cs)` in
   `uc_ppc_store_msr`. **Blocker.**
2. Audit `misc_helper.c` / `helper_regs.c` for the `mtmsr` instruction helper
   — ensure it also exits TB and flushes TLB when IR/DR flip.
3. `translate.c`: add decoder entry for major opcode 6 emitting
   `gen_helper_mac_emulop(pc, opcode)` (§5).
4. `helper.h` + new `mac_emulop_helper.c`: declare and implement the helper.
5. `unicorn.c`: extend `reg_read`/`reg_write` to handle
   DBAT\*/IBAT\*/SDR1/HID0 — not required for boot, useful for flight
   recorder state dump.

Budget: ~2–3 days, mostly validation.

## 13. Validation milestones

Each milestone gates the next.

1. **First 100 insns match.** Run KPX and Unicorn side-by-side (separate
   runs, compared offline) — PC stream from `ROMBase + 0x310000` must match
   exactly. Divergence = wrong initial MSR / SPRG / BAT (§4).
2. **MSR flip survives.** Reach the first `mtmsr` enabling IR/DR. The next
   instruction must decode from the new translation regime. Stale decode = §1
   patch failed.
3. **First BAT setup.** Dump BATs when first written; compare to KPX at the
   same PC. Mismatch here means we diverged earlier and didn't notice.
4. **First IRQ delivered and returned.** 60 Hz tick → trampoline → EXEC_RETURN
   → outer loop restores. Verify via `/api/status` that `checkload_count`
   starts ticking.
5. **First EmulOp.** Catch an `OP_XPRAM1` or `OP_MICROSECONDS`, dispatch,
   resume.
6. **Nanokernel probe remap.** First HandleInterrupt → remap `0x68070000` and
   `0x69000000` as RW. Boot continues past nanokernel init.
7. **Finder.** `test_boot_to_finder.sh` with `--backend unicorn --arch ppc`.
   Add a row to the matrix in `tests/test_guest_suite.sh`.

## 14. Failure modes, ranked

1. **QEMU PPC MMU stubs for classic 32-bit (603/604) not fully implemented.**
   The Mac nanokernel is the only real-world user of `POWERPC_MMU_SOFT_6xx`
   that I know of. Expect at least one bug in `mmu_helper.c`.
2. **BAT enforcement off-by-one.** The nanokernel uses BATs aggressively for
   KernelData aliasing. QEMU's BAT matcher bugs on supervisor-only ranges
   would produce weird faults.
3. **Inconsistent `isync` / TB flush timing.** Even with §12 fixes, QEMU may
   execute a TB that was valid at mtmsr but stale by the following isync.
   Mitigate with the defensive isync-as-flush-point.
4. **`uc_emu_stop` race with the sentinel EmulOp.** If the tick thread stops
   the engine the same cycle EXEC_RETURN also asks to stop, the outer loop
   must handle both. Order: service EXEC_RETURN first (it restores state
   anyway), then service IRQ on the restored PC.
5. **Perf degradation from over-aggressive TB flushing** — secondary; only
   matters once correct.

## 15. Work order

- **Day 1–2**: Subproject patches (§12 items 1–2). Confirm no regressions in
  the Unicorn m68k backend.
- **Day 3**: `cpu_unicorn_ppc.cpp` skeleton — Platform API install, memory
  map (§3), CPU init (§4), execute loop that just runs and stops on any
  event. Hit milestone 1.
- **Day 4**: EmulOp dispatch (§5) + mem fault path (§6). Hit milestones 2–3.
- **Day 5**: IRQ injection (§8). Hit milestones 4–6.
- **Day 6+**: Boot to Finder (milestone 7), triage, add test matrix row.

Realistic estimate to boot-to-Finder: **1.5–2 weeks** given the subproject
unknowns. Boot-in-isolation first run could be 3 days if lucky with the MMU
helper.

## 16. Performance expectations

Unicorn PPC vs KPX depends heavily on host architecture:

| Host | KPX | Unicorn PPC | Likely winner |
|---|---|---|---|
| x86-64 | dynarec | TCG JIT | KPX (hand-tuned, Mac-specific) |
| Apple Silicon | interpreter (no ARM64 backend) | TCG JIT → ARM64 | **Unicorn**, probably 3–10× |
| ARM64 Linux | interpreter | TCG JIT → ARM64 | **Unicorn** |
| RISC-V | interpreter | TCG JIT → RISC-V | **Unicorn** |

KPX's dynarec targets x86/x86-64 and PPC hosts only — predates ARM64. On
Apple Silicon, ARM servers, and RISC-V, KPX runs in interpreter mode, and
Unicorn's TCG JIT should win comfortably. **This is the primary motivation:**
Unicorn PPC is the fast path on ARM / RISC-V hosts and the validation path
on x86.

The TB-flush and MMIO-callback penalties from §1 and §8 are constant taxes,
not order-of-magnitude hits — they matter for tuning but don't flip the
ranking.
