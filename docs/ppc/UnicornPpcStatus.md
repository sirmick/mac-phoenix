# Unicorn PPC backend — status & handoff

Last updated: 2026-04-20 (branch `unicorn-ppc`, post-prune + doc rewrite).

Live pointer: this is the single place the Unicorn PPC port's state lives.
Start here on any new session. Build/run snippets at the bottom.

## TL;DR

- **KPX (default PPC backend, SheepShaver-derived interpreter):** end-to-end
  clean on G3 ROM + Mac OS 7.5.5 and 7.6.1 — Desktop renders,
  BridgeAgent heartbeats, graceful shutdown via event bridge works.
- **Unicorn (QEMU TCG JIT):** builds, boots, reaches the internal
  "Finder detected" phase flag — but the guest never paints a visible
  Desktop. On 7.6.1 the cursor stays an hourglass then the guest crashes
  (SIGSEGV at guest PC `0x5009d288`, inside nanokernel territory). On
  7.5.5 the crash is earlier. Use 7.6.1 as the triage disk.
- **Headless `[Boot +N.Ns] Desktop ready` is not proof of a visible
  Desktop.** The phase flag flips on a `CurApName` peek + idle poll; it
  doesn't inspect the framebuffer. Verify visually or via
  `/api/screenshot`. See `feedback_ppc_desktop_phase_vs_visual.md`.
- **Patch set against upstream Unicorn** lives in
  `subprojects/unicorn-patches/` (6 patches, ~24 KB of diff).

## Architecture

### Backend selection

`--backend unicorn --arch ppc` selects the Unicorn PPC backend at
runtime. `--arch ppc` alone still defaults to KPX. The config parser
treats `--backend` as a strict space-separated form — the `=`-form is
ignored, so `--backend=unicorn` silently falls through to the UAE
default, which the PPC auto-promotion then lifts back to KPX. Use
space-separated form in scripts.

### Memory map (`uppc_cpu_init`, `src/cpu/cpu_unicorn_ppc.cpp:496`)

The backend mirrors KPX's memory layout byte-for-byte so SheepShaver's
nanokernel can address the same absolute PPC addresses KPX runs against.
All mappings use `uc_mem_map_ptr` against host-side allocations made
earlier in `init_ppc`:

| Region            | Mac addr              | Notes |
|-------------------|-----------------------|-------|
| RAM + NanoKernel pad | `0x00000000..`     | Host-address-0 mmap via REAL_ADDRESSING; requires patch 0002+0004+0005. |
| ROM               | `0x40800000`, `0x50000000..` | Aliased so both low-ROM and nanokernel-ROM addresses resolve. Mapped RW+X because the nanokernel self-patches. |
| KernelData        | `0x68ffe000..`, `0x5ffffe00..` | Dual-aliased per SheepShaver convention. |
| SheepMem          | `0x80000000..`        | Includes the `POWERPC_EXEC_RETURN` trampoline slot reserved at init. |
| Framebuffer       | `screen_base..`       | ~4 MB, rounded to page. QuickDraw issues ~1M stw/lwz ops per frame, so it must be real RAM, not MMIO. |
| Grand Central I/O | `0xf3000000..0xf3020000` | `uc_mmio_map` — reads return 0, writes discarded. Matches SheepShaver's dummy-IO map; unblocks SCC/MACE driver probe loops that would otherwise spin-wait. |
| DR probe regions  | `0xff000000..`        | Left unmapped at init; the unmapped-memory hook auto-maps zero pages on first touch. Remapped to RW RAM by `uppc_remap_dr_probes_once` after the first IRQ, matching KPX's `munmap`-then-mprotect pattern. |

### Execution path

1. `uppc_cpu_execute_fast` is the outer loop. Each iteration calls
   `uc_emu_start(pc, 0, 0, 0)` and classifies the result. EmulOps raised
   via major-opcode-6 trap back to `uppc_mac_emulop_cb`
   (`cpu_unicorn_ppc.cpp:464`) through the `mac_emulop` TCG helper —
   see patch 0003 + `qemu/target/ppc/mac_emulop_helper.c`.
2. **Unmapped memory** (`UC_ERR_{READ,WRITE}_UNMAPPED` / `FETCH_UNMAPPED`)
   is handled by `uppc_skip_memop_at`: if the PC is a load, decode `rd`
   and zero it; if a store, advance PC. Mimics KPX's host-level SIGSEGV
   skip. The "zero-on-skip" policy replaced "preserve-dest" in
   late-4; without it, 68k-emulator probe tables at `0x504a7380` loop
   forever because the branch target is GPR-derived.
3. **EmulOp dispatch** (`uppc_dispatch_emul_op`): the major-opcode-6
   helper calls into `execute_native_op_pure(selector, gprs[32])` via
   `g_platform.ppc_native_op`, which both backends share. No direct
   symbol references from `cpu_unicorn_ppc.cpp` into `libkpx_interp.a`.
4. **68k reentrancy.** `uppc_cpu_execute_68k` sets up 68k register
   context in the `KernelData`-style layout and calls a nested
   `uc_emu_start` at the 68k trampoline. Requires patch 0006 — without
   it, the inner `uc_emu_stop` leaves `stop_request` asserted and the
   outer frame breaks out spuriously.
5. **IRQ injection** is cooperative. `uppc_tick_thread` sets
   `g_pending_irq` at 60 Hz (modulated by `TICK_PERIOD_SCALE`). A
   block-boundary hook pumps `g_pending_irq` into
   `uppc_handle_interrupt`, which re-enters MODE_68K via the nanokernel
   interrupt entry. Default `TICK_PERIOD_SCALE = 10` (6 Hz tick) for
   Unicorn — at 60 Hz the IRQ lands mid-EmulOp often enough to corrupt
   state (~30× the IRQ-per-EmulOp ratio KPX sees).

### Hook surface (`uppc_cpu_init`, installed once)

| Hook | Purpose | Gate |
|---|---|---|
| `UC_HOOK_BLOCK` `hook_last_pc` | 32-slot ring of last guest PCs; crash handler reads it | `MACEMU_PPC_NO_BLOCK_TRACE=1` to disable |
| `UC_HOOK_MEM_INVALID` `hook_unmapped` | Drives `uppc_skip_memop_at`; auto-maps zero pages at DR probe ranges | Always on |
| `UC_HOOK_CODE` at known EmulOp PCs | Forces `uc_emu_stop` so the outer loop observes the trap cleanly | Always on |
| `UC_HOOK_BLOCK` `hook_entry` | `MACEMU_PPC_TRACE_68K_ENTRY=<hex>[,...]` — dump register + r24 ring context when a listed 68k PC executes | Opt-in |
| `UC_HOOK_BLOCK` `hook_r24_ring` | 128-slot 68k-PC ring (r24) for `TRACE_68K_ENTRY` context | Auto-installed iff `TRACE_68K_ENTRY` is set |

Stall watchdogs inside `uppc_cpu_execute_fast`:

- **Hot-skip bail** — ≥100 000 consecutive skips at the same PC.
- **Concentrated-skip bail** — ≥200 skips across ≤6 distinct PCs.
- **Progress-stall bail** — no EmulOp advance for 5 s (with IRQ counter
  in the dump so we can distinguish "IRQs delivered but not consumed"
  from "no IRQs at all").
- **Block-window stall bail** — all 32 ring entries within a 4 KB window
  for 8 s.

### Platform glue

`src/common/include/platform.h` carries the `Platform` function-pointer
table. `cpu_context.cpp:init_ppc` dispatches to
`install_uppc_platform_hooks` or `install_kpx_platform_hooks` based on
`config.cpu_backend`. Core code never references backend-specific
symbols. Read commands (`/api/app`, `/api/windows`) and action commands
(`/api/launch`, `/api/shutdown`) work identically on both backends
via the bridge files.

## Upstream patch set

`subprojects/unicorn/` is vendored. Changes we need in upstream Unicorn
are also captured as numbered patches in `subprojects/unicorn-patches/`
(with its own README):

| # | One-line |
|---|---|
| 0000 | m68k: implement RTR instruction (predates the PPC series; reconstructed from the vendored tree) |
| 0001 | scaffold backend alongside KPX (weak perf counters, TB-flush on MSR IR/DR flips) |
| 0002 | drop `NULL`-ptr guard in `uc_mem_map_ptr` so RAM can mmap at host 0 |
| 0003 | `mac_emulop` helper + CMake/helper.h plumbing (the one mac-phoenix-specific feature) |
| 0004 | carry `RAM_PREALLOC` through `ram_block_add` + register `mac_emulop` unconditionally |
| 0005 | `qemu_ram_block_from_host` ignores `block->host == NULL` sentinel when `RAM_PREALLOC` set |
| 0006 | clear `stop_request` on nested `uc_emu_start` return so outer frame resumes cleanly |

## Known gaps

### Current wall: Finder-detect → hourglass → crash

The 7.6.1 boot path on Unicorn reaches `CurApName` == `Finder`, the
`boot_phase` tracker flips to `desktop` at `[Boot +14.35s]`, but the
framebuffer shows the hourglass cursor and the guest crashes before
the Desktop paints. Flight recorder at `/tmp/mp_sigsegv_trace.log`
captures host PC `0x7854efa5` (TCG-compiled code) at guest PC
`0x5009d288` — nanokernel territory.

Headless run doesn't flag this cleanly because:
1. `Desktop ready` prints on phase-flag flip, independent of paint.
2. The crash handler's KPX-style skip masks the fault — the process
   keeps running to timeout and exits 0.

See `feedback_ppc_desktop_phase_vs_visual.md`.

### Post-Desktop unmapped-read loop (7.6.1 headless)

16 hits at `pc=0x50491348 target=0x0021cf0ca4 size=4` fire right after
the phase flag flips, then stop. `0x0021cf0ca4` is inside the 128 MB
RAM window (`0..0x08000000`) yet Unicorn reports it unmapped —
probably a `uc_mem_map` gap, possibly a guest-generated physical
address that crosses a region boundary we haven't stubbed. Separate
from the Finder-crash but worth pairing in the triage session.

### Intrinsic IRQ-pressure sensitivity

At 60 Hz ticks (`TICK_PERIOD_SCALE=1`) Unicorn boots unreliably — 9/10
progress-stall bails in the late-8d baseline matrix. TCG path
dispatches EmulOps ~30× slower than KPX's direct interpreter, so a
fixed-wall-clock tick lands one IRQ per ~9 EmulOps vs KPX's
one-per-105. `SCALE=10` is the current default and gives ~30 %
desktop-reach in 60 s; `SCALE=30` gets 6/10 but each boot costs more
wall-clock. This is an architectural cost of TCG+softmmu+hooks, not
a backend bug — see late-8e perf flame graph below.

### Cursor update stubbed

`uppc_ppc_cursor_move` is a no-op. Needs an Execute68k path for
`CursorDeviceDispatch` + SheepMem, mirroring KPX's approach.

### GET_RESOURCE-family selectors

`execute_native_op_pure` returns `false` for `NATIVE_GET_RESOURCE /
NATIVE_GET_1_RESOURCE / …` because those re-enter PPC code via
`execute_ppc(old_get_resource)`. KPX handles this on its CPU
instance (`sheepshaver_cpu::execute_native_op`); Unicorn has an
`uppc_cpu_execute_ppc` entry but it isn't wired into the platform
shim. When one fires, `kpx_ppc_native_op` prints
`[KPX] ppc_native_op: selector %u (GET_RESOURCE family) needs
execute_ppc`. Not observed during boot — deferred until it is.

## Performance picture (late-8e perf flame graph)

`perf record -F 99 --call-graph dwarf` on a 30-s `--backend unicorn
--arch ppc` run shows Unicorn's **softmmu translation chain is ~40 %
of wall time**. This is the architectural cost of a TCG JIT with hooks
vs. KPX's direct PPC-on-PPC interpretation — not a bug.

| %      | Function                          | What it is |
|-------:|-----------------------------------|------------|
| 25.6 % | `load_helper`                     | Softmmu memory-load helper (inclusive). |
|  9.1 % | `[unknown]`                       | JIT-compiled TCG guest code (unsymbolized). |
|  8.3 % | `find_memory_mapping_ppc`         | Unicorn-side MMU translation. |
|  7.5 % | `helper_lookup_tb_ptr_ppc`        | TB-pointer lookup (per basic block). |
|  6.8 % | `flatview_translate_ppc`          | QEMU flatview translate. |
|  5.4 % | `tb_lookup__cpu_state`            | TB cache lookup. |
|  5.0 % | `helper_be_lduw_mmu_ppc`          | 16-bit BE load helper. |
|  ~5 %  | our lambdas (UC hooks)            | Debug + block-ring hooks. |
|  1.6 % | `helper_check_exit_request_ppc`   | Per-TB IRQ-exit check. |

`_int_malloc` frames in the original profile were DWARF-unwinder
phantoms — confirmed no runtime malloc via LD_PRELOAD counter (33
mallocs in a 15 s boot).

**Real perf lever** (addressed in late-9b): `cputlb.c:1556` used to
call `uc->memory_mapping(uc, paddr)` on every memory access — even
on TLB hit. Now cached in `CPUTLBEntry::mr` and reused until the
entry is flushed. Five softmmu call sites rewired. Companion LRU in
`find_memory_mapping` catches the remaining non-TLB callers (uc.c
API paths). Rerun `perf record` to refresh this table — current
numbers are from the pre-late-9b profile.

Artifacts: `docs/ppc/late-8-artifacts/perf/` (flame SVGs + folded
stacks; raw `.data` files gitignored).

## Useful env vars

### Behavioral knobs (change runtime timing/IRQ shape)

| Var | Effect |
|---|---|
| `MACEMU_PPC_TICK_PERIOD_SCALE=N` | Unicorn: multiply the 16.625 ms tick period. `10` → 6 Hz. **Default 10** for Unicorn PPC; explicit env var overrides. |
| `MACEMU_PPC_NO_IRQ=1` | Both backends: mask 60 Hz timer IRQ. Used by the boundary comparator for deterministic traces. |
| `MACEMU_PPC_MIN_EMULOPS_PER_IRQ=N` | Unicorn: suppress tick IRQ unless ≥N emulops elapsed. Default 0 = off. Negative-result knob (late-8c) — kept as evidence. |
| `MACEMU_PPC_DEFER_FIRST_IRQ=N` | Unicorn: suppress tick IRQs until `g_emulop_count ≥ N`. Default 0 = off. Also negative (late-8c). |
| `MACEMU_PPC_NO_BLOCK_TRACE=1` | Unicorn: disable the always-on `hook_last_pc` UC_HOOK_BLOCK. Set only for clean perf runs — the crash handler loses its last-PC ring. |

### Tracers (all default off; ×1 in stderr unless otherwise noted)

| Var | Effect |
|---|---|
| `MACEMU_PPC_TRACE=<path>` | Both: per-EmulOp boundary-state line pairs (EMULOP + POST) to `<path>`. Drives the KPX-vs-Unicorn diff workflow. |
| `MACEMU_PPC_CR2_TRACE=<lo>[:<hi>]` | Both: per-instruction `[CR]` lines on stderr for EmulOp seq in `[lo, hi)`. Auto-disables KPX JIT. |
| `MACEMU_PPC_TRACE_TRAP=1` | Unicorn: log each EXEC_NATIVE dispatch. |
| `MACEMU_PPC_TRACE_IRQ=1` | Unicorn: log every `g_pending_irq` 0→1 / 1→0 edge in `uppc_handle_interrupt`. |
| `MACEMU_PPC_TRACE_68K_ENTRY=<hex>[,<hex>...]` | Both: dump register context and recent-68k-PC ring when a listed 68k PC (`r24` on Unicorn) is executed. Auto-disables KPX JIT. |
| `MACEMU_PPC_TRACE_68K_MAX=N` | Both: max hits per target for `TRACE_68K_ENTRY` before suppression (default 5). |

Removed and **not** coming back unless re-scaffolded for a new symptom:
`MACEMU_PPC_TRACE_DISP`, `TRACE_EMULOP`, `TRACE_ROMZ`, `TRACE_LOWMEM`,
`VECTOR_TRACE`, `TRACE_NEST`, `TRACE_TICK`, `TRACE_LOOP`, `TRACE_MACOS`,
`BCTRL_WATCH`, `TRACE_R1ZERO`, `TRACE_TWI`, `DUMP_PC`. See git log if you
genuinely need one.

## Key files

| File | Role |
|---|---|
| `src/cpu/cpu_unicorn_ppc.cpp` | Unicorn backend: memory map, emulop callback, install function, execute loop |
| `src/cpu/kpx/cpu_ppc_kpx.cpp` | KPX backend + shared `execute_native_op_pure` + `kpx_ppc_native_op` platform shim |
| `src/cpu/kpx/src/cpu/ppc/ppc-cpu.cpp` | KPX interpreter loop; per-instruction CR trace hooks live here |
| `src/common/include/platform.h` | `Platform` function-pointer table (adds `ppc_native_op`) |
| `src/common/include/ppc_boundary_trace.h` | Shared comparator (EMULOP / POST / CR lines) |
| `subprojects/unicorn/qemu/target/ppc/*` | Unicorn PPC TCG target — only touch via a new patch in `subprojects/unicorn-patches/` |
| `subprojects/unicorn-patches/` | Upstream-applicable patches (see its README) |

## Rebuild + smoke-test

```bash
cmake --build build -j$(nproc)

# KPX sanity — Desktop in ~1.6 s, BridgeAgent heartbeat follows
./build/mac-phoenix --backend kpx --arch ppc --rom ~/storage/roms/g3.rom \
  --disk ~/storage/images/macos-7.6.1.img --ram 128 \
  --no-webserver --timeout 15

# Unicorn — reaches Finder-detect, hourglass, crashes before visible Desktop.
# Visually confirm via UI at http://localhost:8000 instead of trusting the log.
./build/mac-phoenix --backend unicorn --arch ppc --rom ~/storage/roms/g3.rom \
  --disk ~/storage/images/macos-7.6.1.img --ram 128 \
  --timeout 60
```

## Compressed history

Kept as one-line milestones. The rolling per-session diary was trimmed
2026-04-20 — if you need a specific session's detail, reach for git log.

- **late-4 (2026-04-19)** — zero-on-skip policy unlocks the
  `0x504a7380` probe-table loop. 1/15 runs reach Desktop in full for
  the first time.
- **late-5** — UI visual confirmation: Unicorn does render the Mac
  splash "Starting Up…" progress bar and (separately) a proper Type 10
  bomb dialog. The PPC backend executes enough of Mac OS 7.5.5 to push
  real UI through the framebuffer.
- **late-6 / late-7** — chased an "A-trap vector corruption" byte-shift
  to its source: a legitimate 68k `PStrToCStr` routine being called
  with `A0 = 0x08` (treating the exception vector table as a Pascal
  string). Verdict: no PPC TCG miscompile; the bug is in a
  caller-of-caller's A0 setup, which diverges from KPX one call-frame
  earlier. Don't chase TCG rotate-by-8 suspicions.
- **late-7b** — the primary repro of the session shifted to a different
  downstream corruption (CLUT-builder with A6 = A7 = 0). Same upstream
  A-reg divergence, different visible symptom. Subsequent sessions
  couldn't reproduce.
- **late-8d** — bombshell: every previous baseline-matrix row
  attributed to Unicorn had silently been KPX, because
  `matrix.sh` used `--backend=unicorn` (the `=`-form the parser
  ignores). Corrected baseline: Unicorn SCALE=1 = 0/10 desktop,
  SCALE=10 = 3/10, SCALE=30 = 6/10, KPX SCALE=1 = 9/10. Intrinsic
  IRQ-pressure sensitivity is real.
- **late-8e** — perf flame graph. Softmmu is the tax, not TCG dispatch;
  `uc_emu_stop` on IRQ is cheap (~2 %); no runtime malloc.
- **late-8f** — debug hook gating. Hygiene, not a perf win.
- **late-9 (2026-04-20)** — disk restore (7.6.1 image was corrupt from
  earlier crashed runs). Pruned ~670 lines of scaffolding that had
  served its purpose (`TRACE_DISP`, `TRACE_EMULOP`, `TRACE_ROMZ`,
  `VECTOR_TRACE`, `TRACE_NEST`, `BCTRL_WATCH`, `R1ZERO`, `TWI`,
  `DUMP_PC`, `TRACE_TICK`, `TRACE_LOOP`, `TRACE_MACOS`). Extracted
  the 6 upstream-targetable patches into `subprojects/unicorn-patches/`.
- **late-9b (2026-04-20)** — perf tightening pass. Softmmu MR cache
  landed in two layers: (a) 4-way page-keyed LRU inside
  `find_memory_mapping` (`qemu/unicorn_common.h`) for the uc.c / other
  callers, and (b) per-TLB-entry `MemoryRegion *mr` pointer in
  `CPUTLBEntry` (`qemu/accel/tcg/cputlb.c`) so the five hot softmmu
  sites skip the indirect `uc->memory_mapping` call on warm pages.
  Invalidation piggybacks on `tlb_flush()`. Succ rate at SCALE=10
  moved from doc baseline 3/10 to 5/10 (N=10); time-to-Desktop on
  successful runs dropped from ~14s (stale doc measurement) to ~2.8s.
  Hypothesis: less softmmu overhead per EmulOp → less IRQ-per-EmulOp
  pressure → fewer corrupting mid-dispatch IRQs. N=10 is too small to
  nail down but trend is consistent. 68k tightening (companion commit)
  separately dropped 68k Unicorn boot from 15.78s → 11.75s.

## Next-session targets

1. **Capture what's actually executing at the pre-Desktop
   Finder-crash.** Arm `MACEMU_PPC_TRACE_68K_ENTRY` on the guest PCs
   seen right before the SIGSEGV at `0x5009d288`, look for a
   reproducible trigger 68k routine. `/tmp/mp_sigsegv_trace.log`
   captures recent block PCs at fault time.
2. **Explain the post-Desktop unmapped read at `0x0021cf0ca4`.** Inside
   nominal RAM range but reports unmapped; check `uc_mem_map` against
   `RAMBase / RAMSize` and cross-reference what
   `find_memory_mapping_ppc` returns.
3. **CI gate that doesn't trust headless `Desktop ready`.** Either
   `/api/screenshot` → minimum-non-black pixel check, or bridge-read a
   Finder sentinel.
4. ~~Cache MR pointer in TLB entry~~ — **done in late-9b**, see above.
   Regenerate `subprojects/unicorn-patches/` as 0007 when ready to
   ship the patch series upstream.
5. **Larger N baseline matrix at SCALE=10 post-late-9b.** 5/10 is
   promising but not confirmation; rerun with N≥30 to see if the
   stability gain survives statistical scrutiny, and re-check
   SCALE=1.
