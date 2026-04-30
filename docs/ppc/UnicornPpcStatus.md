# Unicorn-PPC status

The Unicorn-PPC backend (`--backend unicorn-ppc`) builds, boots, and
reaches the internal "Finder detected" phase flag on Mac OS 7.6.1 — but
the framebuffer paint is unreliable and there are workloads where the
guest crashes before the desktop becomes visible. **KPX is the default
for PPC; Unicorn-PPC is for development.**

This doc is the entry point for anyone working on the backend. The
shared PPC architecture and KPX side live in
[`README.md`](README.md).

## TL;DR

- KPX (default) cleanly boots 7.5.5 / 7.6.1 to Finder, BridgeAgent
  heartbeats, graceful shutdown via the bridge works.
- Unicorn-PPC reaches `CurApName == "Finder"` (the boot-phase tracker
  flips to `desktop`), but on 7.6.1 the cursor stays an hourglass and
  the guest typically crashes inside nanokernel territory
  (most-recent SIGSEGV captured at host PC `0x7854efa5` / guest PC
  `0x5009d288`). On 7.5.5 the crash comes earlier. **Use 7.6.1 as the
  triage disk.**
- Headless `[Boot +N.Ns] Desktop ready` is **not proof of a visible
  Desktop**. The phase flag flips on a `CurApName` peek + idle poll;
  it doesn't inspect the framebuffer. Verify visually or via
  `/api/screenshot`.
- Patch set against pristine Unicorn 2.1.4 lives in
  `subprojects/unicorn-patches/` (10 numbered patches, ~31 KB across
  16 files; `git am` clean).

## Backend selection

`--backend unicorn-ppc` is the strict, space-separated form. The parser
ignores `--backend=unicorn-ppc` (the `=`-form silently falls through to
the UAE default, which the PPC auto-promotion then lifts back to KPX).
Use space-separated form in scripts.

## Memory map (`uppc_cpu_init`, `src/cpu/cpu_unicorn_ppc.cpp`)

Mirrors KPX byte-for-byte so the same nanokernel addresses resolve.
All host-backed mappings use `uc_mem_map_ptr`.

| Region | Mac addr | Notes |
|--------|----------|-------|
| RAM + nanokernel pad | `0x00000000..` | host-address-0 mmap via REAL_ADDRESSING; needs patches 0003 + 0005 + 0006 |
| ROM | `0x40800000`, `0x50000000..` | aliased; mapped RW+X because the nanokernel self-patches |
| KernelData | `0x68ffe000..`, `0x5ffffe00..` | dual-aliased per SheepShaver convention |
| SheepMem | `0x80000000..` | includes the `POWERPC_EXEC_RETURN` trampoline slot |
| Framebuffer | from video driver | ~4 MB; QuickDraw issues ~1 M stw/lwz per frame, must be real RAM not MMIO |
| Grand Central I/O | `0xf3000000..0xf3020000` | `uc_mmio_map`, reads return 0; matches SheepShaver dummy-IO map |
| DR probe regions | `0xff000000..` | unmapped at init; auto-mapped to zero pages on first touch, remapped to RW RAM by `uppc_remap_dr_probes_once` after the first IRQ |

## Execution path

1. **Outer loop** (`uppc_cpu_execute_fast`): each iteration runs
   `uc_emu_start(pc, 0, 0, 0)` and classifies the result. EmulOps raised
   via major-opcode-6 trap into `uppc_mac_emulop_cb` through the
   `mac_emulop` TCG helper added by patch 0004.
2. **Unmapped memory** (`UC_ERR_{READ,WRITE,FETCH}_UNMAPPED`):
   `uppc_skip_memop_at` decodes the faulting instruction. For a load
   it zeroes the destination register; for a store it advances PC.
   Mirrors KPX's host-level SIGSEGV skip. The "zero-on-skip" policy is
   load-bearing: 68k-emulator probe tables at `0x504a7380` loop forever
   without it, because the branch target is GPR-derived.
3. **EmulOp dispatch** (`uppc_dispatch_emul_op`): the major-opcode-6
   helper calls `execute_native_op_pure(selector, gprs[32])` via the
   shared `g_platform.ppc_native_op`. No backend-specific symbol leaks
   from `cpu_unicorn_ppc.cpp` into KPX's `libkpx_interp.a`.
4. **68k re-entrancy** (`uppc_cpu_execute_68k`): nested `uc_emu_start`
   into the 68k trampoline. Requires patch 0007 — without it, the inner
   `uc_emu_stop` leaves `stop_request` asserted and the outer frame
   breaks out spuriously.
5. **IRQ injection** (`uppc_tick_thread`): cooperative. The tick thread
   sets `g_pending_irq` at 60 Hz (modulated by `TICK_PERIOD_SCALE`). A
   `UC_HOOK_BLOCK` callback drains the flag at TB boundaries and exits
   cleanly via same-thread `uc_emu_stop`, no mid-TB unwind. Default
   `TICK_PERIOD_SCALE = 1` (true 60 Hz) — the in-place block-hook
   delivery makes that workable; the older async cross-thread
   `uc_emu_stop` path is still available behind `MACEMU_PPC_NO_IRQ_HOOK=1`
   as a rollback knob.

## Hooks

| Hook | Purpose | Default |
|------|---------|---------|
| `UC_HOOK_BLOCK` `hook_irq_drain` | Drains `g_pending_irq` at TB boundaries; the in-place IRQ delivery path | always on |
| `UC_HOOK_MEM_INVALID` `hook_unmapped` | Drives `uppc_skip_memop_at`; auto-maps zero pages at DR probe ranges | always on |
| `UC_HOOK_CODE` at known EmulOp PCs | Forces `uc_emu_stop` so the outer loop observes the trap cleanly | always on |
| `UC_HOOK_BLOCK` `hook_last_pc` | 32-slot ring of last guest PCs for the crash handler | opt-in via `MACEMU_PPC_BLOCK_TRACE=1` (measured 6.6 % wall cost) |
| `UC_HOOK_BLOCK` `hook_entry` | Dump register + r24-ring context when listed 68k PCs execute | opt-in via `MACEMU_PPC_TRACE_68K_ENTRY=<hex>[,...]` |

Stall watchdogs inside `uppc_cpu_execute_fast`:

- ≥100 000 consecutive skips at the same PC → bail.
- ≥200 skips across ≤6 distinct PCs → bail.
- No EmulOp advance for 5 s → bail (with IRQ counter so we can tell
  "IRQs delivered but not consumed" from "no IRQs at all").
- All 32 ring entries within a 4 KB window for 8 s → bail.

## Platform glue

`Platform` (in `src/common/include/platform.h`) carries the function
pointers; `cpu_context.cpp:init_ppc` dispatches to
`install_uppc_platform_hooks` or `install_kpx_platform_hooks` based on
`config.cpu_backend`. Read commands (`/api/app`, `/api/windows`) and
action commands (`/api/launch`, `/api/shutdown`) work identically on
both backends — the bridge is file-based.

## Upstream patch set

`subprojects/unicorn/` is vendored. Upstreamable changes live as
numbered patches in `subprojects/unicorn-patches/`:

| # | One-line |
|---|---|
| 0000 | m68k: implement RTR instruction (standalone) |
| 0001 | m68k: MacPhoenix host-integration patches (A-line pre-read, RTE fast-return, interrupt auto-ack, perf counters, looser `use_goto_tb`) |
| 0002 | scaffold PPC backend alongside KPX (promote 0001's perf counters to weak; TB-flush on MSR IR/DR flips) |
| 0003 | drop `NULL`-ptr guard in `uc_mem_map_ptr` so RAM can mmap at host 0 |
| 0004 | `mac_emulop` TCG helper + CMake/helper.h plumbing |
| 0005 | carry `RAM_PREALLOC` through `ram_block_add` + register `mac_emulop` unconditionally |
| 0006 | `qemu_ram_block_from_host` ignores `block->host == NULL` sentinel when `RAM_PREALLOC` set |
| 0007 | clear `stop_request` on nested `uc_emu_start` return so outer frame resumes cleanly |
| 0008 | 4-way page-keyed LRU in `find_memory_mapping` (helps both 68k and PPC softmmu) |
| 0009 | cache MR pointer in `CPUTLBEntry` — skip per-access `memory_mapping` on TLB hit |

## Known gaps

### Pre-Desktop crash

7.6.1 reaches `CurApName == "Finder"`, the `boot_phase` tracker flips
to `desktop`, but the framebuffer shows the hourglass cursor and the
guest crashes before the Desktop paints. Flight recorder at
`/tmp/mp_sigsegv_trace.log` captures the host PC + guest PC at fault
time. Headless runs don't flag this cleanly because the phase print
is paint-independent and the crash handler's KPX-style skip masks the
fault — verify with `/api/screenshot` (or arm
`MACEMU_PPC_TRACE_68K_ENTRY` on the PCs immediately before the fault).

### Post-Desktop unmapped read

After the phase flag flips, ~16 hits at
`pc=0x50491348 target=0x0021cf0ca4 size=4`, then quiet. `0x0021cf0ca4`
is inside the 128 MB RAM window yet reports unmapped — likely a
`uc_mem_map` gap or a guest-generated physical address crossing a
region boundary we haven't stubbed. Independent from the pre-Desktop
crash; worth pairing in any triage session.

### IRQ-pressure sensitivity

The TCG path dispatches EmulOps roughly 30× slower than KPX's direct
interpreter, so a fixed 60 Hz wall-clock tick lands one IRQ per ~9
EmulOps under Unicorn vs one per ~105 under KPX. Earlier baselines at
`SCALE=1` failed to reach Finder; the in-place block-hook delivery
(commit `1e9dfbd0`) made `SCALE=1` viable by removing per-IRQ TB
unwind. Tuning knob is still present (`MACEMU_PPC_TICK_PERIOD_SCALE`)
in case a future workload re-introduces sensitivity.

### Cursor update is stubbed

`uppc_ppc_cursor_move` is a no-op. Needs an Execute68k path for
`CursorDeviceDispatch` + SheepMem, mirroring KPX's approach.

### GET_RESOURCE-family selectors

`execute_native_op_pure` returns `false` for `NATIVE_GET_RESOURCE` /
`NATIVE_GET_1_RESOURCE` / etc. because they re-enter PPC code via
`execute_ppc(old_get_resource)`. KPX handles this on its
`sheepshaver_cpu` instance; Unicorn-PPC has an
`uppc_cpu_execute_ppc` entry but it isn't wired into the platform
shim. Not observed during boot — deferred until it fires.

## Performance picture

Recent `perf record -F 99 --call-graph dwarf` over a 30 s
`--backend unicorn-ppc` run: softmmu translation chain is ~40 % of
wall time. This is the architectural cost of TCG + softmmu + hooks
vs. KPX's direct PPC-on-PPC interpretation, not a bug.

| % | Function | What |
|--:|----------|------|
| 25.6 | `load_helper` | softmmu memory-load helper (inclusive) |
| 9.1 | `[unknown]` | JIT-compiled TCG guest code |
| 8.3 | `find_memory_mapping_ppc` | Unicorn-side MMU translation |
| 7.5 | `helper_lookup_tb_ptr_ppc` | TB-pointer lookup per BB |
| 6.8 | `flatview_translate_ppc` | QEMU flatview translate |
| 5.4 | `tb_lookup__cpu_state` | TB cache lookup |
| 5.0 | `helper_be_lduw_mmu_ppc` | 16-bit BE load helper |
| ~5 | UC hook lambdas | debug + block-ring hooks |
| 1.6 | `helper_check_exit_request_ppc` | per-TB IRQ-exit check |

The biggest open lever is the Unicorn dirty-bitmap stub (see
[`../deepdive/JitSmcDetectionAnalysis.md`](../deepdive/JitSmcDetectionAnalysis.md)).
PPC pays the notdirty slow path on every RAM write because the
bitmap can't transition pages out of `TLB_NOTDIRTY` — a far bigger
hit on PPC than on m68k due to PPC's higher write density (framebuffer,
memcpy-heavy code paths).

The MR-pointer TLB cache (patch 0009) closed `find_memory_mapping`
out of the hot softmmu path; numbers above are post-cache.

## Environment variables

### Behavioural knobs

| Var | Effect |
|---|---|
| `MACEMU_PPC_TICK_PERIOD_SCALE=N` | Multiply the 16.625 ms tick period. `1` = 60 Hz (default), `10` = 6 Hz. |
| `MACEMU_PPC_NO_IRQ=1` | Mask 60 Hz timer IRQ entirely (both backends). Used for deterministic boundary traces. |
| `MACEMU_PPC_NO_IRQ_HOOK=1` | Fall back from in-place block-hook IRQ delivery to the older async `uc_emu_stop` from the tick thread. |
| `MACEMU_PPC_MIN_EMULOPS_PER_IRQ=N` | Suppress tick IRQ until ≥N EmulOps elapsed since the last one. Off by default. |
| `MACEMU_PPC_DEFER_FIRST_IRQ=N` | Suppress tick IRQs until `g_emulop_count ≥ N`. Off by default. |
| `MACEMU_PPC_BLOCK_TRACE=1` | Enable the `hook_last_pc` ring (~6.6 % wall cost). Crash handler's last-PC ring depends on this. |
| `MACEMU_PPC_TB_FLUSH_EVERY=1` | Flush the TB cache every outer-loop iteration. Diagnostic. |

### Tracers (×1 to stderr unless otherwise noted)

| Var | Effect |
|---|---|
| `MACEMU_PPC_TRACE=<path>` | Per-EmulOp boundary state (EMULOP + POST). Drives the KPX-vs-Unicorn diff workflow. |
| `MACEMU_PPC_CR2_TRACE=<lo>[:<hi>]` | Per-instruction `[CR]` lines for EmulOp seq in `[lo, hi)`. Auto-disables KPX JIT. |
| `MACEMU_PPC_TRACE_TRAP=1` | Log each EXEC_NATIVE dispatch. |
| `MACEMU_PPC_TRACE_IRQ=1` | Log every `g_pending_irq` 0→1 / 1→0 edge in `uppc_handle_interrupt`. |
| `MACEMU_PPC_TRACE_68K_ENTRY=<hex>[,<hex>...]` | Dump register context and recent-68k-PC ring when a listed 68k PC (`r24` on Unicorn) executes. Auto-disables KPX JIT. |
| `MACEMU_PPC_TRACE_68K_MAX=N` | Max hits per target for `TRACE_68K_ENTRY` before suppression (default 5). |

## Key files

| File | Role |
|---|---|
| `src/cpu/cpu_unicorn_ppc.cpp` | Unicorn-PPC backend — memory map, EmulOp callback, install function, execute loop, IRQ delivery |
| `src/cpu/kpx/cpu_ppc_kpx.cpp` | KPX backend + shared `execute_native_op_pure` + `kpx_ppc_native_op` platform shim |
| `src/cpu/kpx/src/cpu/ppc/ppc-cpu.cpp` | KPX interpreter loop; per-instruction CR trace hooks |
| `src/common/include/platform.h` | `Platform` function-pointer table (adds `ppc_native_op`) |
| `src/common/include/ppc_boundary_trace.h` | Shared comparator (EMULOP / POST / CR lines) |
| `subprojects/unicorn/qemu/target/ppc/*` | Unicorn PPC TCG target — only touch via a new patch in `subprojects/unicorn-patches/` |
| `subprojects/unicorn-patches/` | Upstream-applicable patches (see its README) |

## Smoke test

```bash
cmake --build build -j$(nproc)

# KPX sanity — Desktop in ~1.6 s, BridgeAgent heartbeat follows.
./build/mac-phoenix --backend kpx --rom ~/storage/roms/g3.rom \
  --disk ~/storage/images/macos-7.6.1.img --ram 128 \
  --no-webserver --timeout 15

# Unicorn — reaches Finder-detect, hourglass, crashes before visible Desktop.
# VISUALLY confirm via http://localhost:11000 — don't trust the headless log.
./build/mac-phoenix --backend unicorn-ppc --rom ~/storage/roms/g3.rom \
  --disk ~/storage/images/macos-7.6.1.img --ram 128 --timeout 60
```
