# Unicorn PPC backend — status & handoff

Last updated: 2026-04-21 (dirty-bitmap restored; see §`2026-04-21 — dirty-bitmap restored`).

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
  `subprojects/unicorn-patches/` (10 patches, ~31 KB of diff across 16
  files). Verified to `git am` cleanly against pristine `2.1.4`. See
  that directory's `README.md` for the per-patch breakdown.

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
| RAM + NanoKernel pad | `0x00000000..`     | Host-address-0 mmap via REAL_ADDRESSING; requires patch 0003+0005+0006. |
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
   see patch 0004 + `qemu/target/ppc/mac_emulop_helper.c`.
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
   `uc_emu_start` at the 68k trampoline. Requires patch 0007 — without
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
| `UC_HOOK_BLOCK` `hook_last_pc` | 32-slot ring of last guest PCs; crash handler reads it | `MACEMU_PPC_BLOCK_TRACE=1` to enable (opt-in since late-9c — was 6.58% wall on PPC) |
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
| 0000 | m68k: implement RTR instruction (standalone — applies to pristine 2.1.4 alone) |
| 0001 | m68k: MacPhoenix host-integration patches (A-line pre-read, RTE fast-return, interrupt auto-ack, perf counters, looser use_goto_tb) |
| 0002 | scaffold PPC backend alongside KPX (promote 0001's perf counters to weak; TB-flush on MSR IR/DR flips) |
| 0003 | drop `NULL`-ptr guard in `uc_mem_map_ptr` so RAM can mmap at host 0 |
| 0004 | `mac_emulop` helper + CMake/helper.h plumbing (the one mac-phoenix-specific feature) |
| 0005 | carry `RAM_PREALLOC` through `ram_block_add` + register `mac_emulop` unconditionally |
| 0006 | `qemu_ram_block_from_host` ignores `block->host == NULL` sentinel when `RAM_PREALLOC` set |
| 0007 | clear `stop_request` on nested `uc_emu_start` return so outer frame resumes cleanly |
| 0008 | 4-way page-keyed LRU in `find_memory_mapping` (late-9, 68k +4s savings, also helps PPC) |
| 0009 | Cache MR pointer in `CPUTLBEntry` — skip per-access `memory_mapping` on TLB hit (late-9b) |

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
| `MACEMU_PPC_BLOCK_TRACE=1` | Unicorn: enable the `hook_last_pc` UC_HOOK_BLOCK that feeds the crash handler's last-PC ring. Opt-in since late-9c (measured 6.58% wall cost). |

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
   Captured as patch 0009 in `subprojects/unicorn-patches/` (2026-04-20).
5. **Larger N baseline matrix at SCALE=10 post-late-9b.** 5/10 is
   promising but not confirmation; rerun with N≥30 to see if the
   stability gain survives statistical scrutiny, and re-check
   SCALE=1.

## 2026-04-20 late session — SMC theory + SCALE=1/10 asymmetry

Active task: #24 "Root-cause boot-blocks stall after DiskPrime #1200".
Working tree: `src/cpu/cpu_unicorn_ppc.cpp` and
`docs/UnicornPerformanceAnalysis.md` are modified but uncommitted.
`index.html` is untracked (unrelated to this thread — user asked about an
FFT visualisation at session end; not in this repo as far as we could
find).

### What ran

1. **mprotect audit** — ruled out Theory 4 (SIGSEGV-skip-in-JIT-block
   corrupting JIT state). `c6b65d79` already gates the ROM mprotect on
   `!Unicorn`; remaining mprotect sites are test harness or inactive.
   No `[SIGSEGV] #` markers in 10 stall logs → global sigsegv_handler
   at `src/main.cpp:266` is not firing during these stalls.

2. **SMC-staleness experiment** — `MACEMU_PPC_TB_FLUSH_EVERY=1` (flush
   TB cache every outer-loop iter) gated into `cpu_unicorn_ppc.cpp`
   around the outer `while (!g_stop_requested)` loop. 10× at SCALE=10:
   desktop rate unchanged (0/10), but earliest `[Boot +0.00s]
   Boot globals patched` stalls eliminated (0/10 vs 2/10 baseline).
   Later stalls shifted to new failure modes (wild FETCH_UNMAPPED at
   `0x50580000`, skip storms on `0x21000000`-range pointers). SMC
   staleness is a contributor to the earliest stall only — not the
   dominant cause. Keep the env gate in tree for future debugging; don't
   revert.

3. **IRQ-return / CR2 audit** — ruled out CR2.SO supervisor-flag
   corruption. Save mask `0xff9fffff` at `cpu_unicorn_ppc.cpp:384` is
   identical to KPX line 545. `or_mask=0x00e00000` correctly sets
   CR2.LT|GT|EQ on IRQ entry. `cr_before` always has CR2.SO=1.

4. **IRQ trace extended** with Ticks (`0x016a`) + D0 logging around
   line 1180-1188 of `cpu_unicorn_ppc.cpp`. Revealed that during the
   "boot blocks stall" at SCALE=10, **Ticks is advancing** (0→34 over
   20s) and D0 rotates through different values. The "stall" is
   sequential Ticks waits running at 10× slow speed, not a deadlock.

5. **SCALE=1 test** (`/tmp/ppc_scale1.log`, 50s timeout). Result
   contradicted the earlier "SCALE=10 just masks register corruption"
   working theory:
   - Only 2 early benign unmapped skips at startup (r17=0x101ac000,
     just past framebuffer tail at 0x10080000+0x12c000).
   - **No** concentrated-skip bail.
   - **No** r3/r5/r6/r7=0x21000000 register corruption pattern.
   - Steady DiskPrime progress (#1 → #250 in 50s, all returning 0).
   - Never advanced past `[Boot +0.30s] Installing drivers` phase
     marker in 50s, despite successful disk I/O throughout.

   So SCALE=1 and SCALE=10 are **qualitatively different** failure
   modes — not the same bug at different speeds. The
   r3/r5/r6/r7=0x21000000 corruption pattern is SCALE=10-specific.

### Summary state of each theory

| # | Theory | Status |
|---|--------|--------|
| 1 | EmulOp CR2.SO clobber | Ruled out (mask correct) |
| 2 | Stale TB / SMC from dirty-flag stub | Partial contributor (earliest stall only), NOT dominant |
| 3 | IRQ nest XLM_IRQ_NEST stuck | Untested post-late-9b |
| 4 | SIGSEGV-skip-in-JIT corrupts state | Ruled out (no SIGSEGV in recent logs, already gated) |
| 5 | IRQ-mid-dispatch corrupts A-regs | Still the leading theory for SCALE=1 |
| — | SCALE=10 register corruption to 0x21000000 | New; unexplained; SCALE=10-specific |

### Next session — entry points

1. **Why does SCALE=1 DiskPrime loop not advance phase past "Installing
   drivers" despite healthy I/O?** Read `src/core/boot_progress.cpp`
   for what gates the "Loading boot blocks" phase flip. Check whether
   the phase marker needs a write to some memory location that happens
   at 60Hz but not 6Hz cadence, or whether Finder CurApName peek just
   never sees the expected value.

2. **What is 0x21000000 in SCALE=10 register corruption?** Not RAM
   (0..0x04000000), not SheepMem (0x10000000..0x10080000), not
   framebuffer (0x10080000..0x101ac000), not ROM (0x50000000..). Could
   be Mac OS nanokernel page-table base or similar. Grep SheepShaver
   source for the constant.

3. **Restore `cpu_physical_memory_set_dirty_flag`** (task #19) — the
   proper fix needs a per-page bitmap in
   `subprojects/unicorn/qemu/include/exec/ram_addr.h:75` with
   `tb_invalidate_phys_page_fast` wired to it.

4. **Uncommitted work to review before reset:** `MACEMU_PPC_TB_FLUSH_EVERY`
   gate + extended IRQ trace in `cpu_unicorn_ppc.cpp`. Either commit as
   instrumentation ("keep in tree, env-gated") or stash. **(Resolved
   2026-04-20 — committed as 72344c54.)**

## 2026-04-20 super-late session — dedup + targeted SMC flush shipped

### Commits

- **`398e1868`** — "Unicorn PPC: dedup tick-kick on already-pending IRQ".
  `uppc_tick_kick` now `exchange(true)` on `g_pending_irq` and only
  issues `uc_emu_stop` on the rising edge. Back-to-back kicks at
  SCALE=1 (60Hz) were tearing the TB mid-execution and correlated with
  wild-pointer corruption post-Finder.
- **`72344c54`** — "Unicorn PPC: env-gated IRQ trace + TB_FLUSH_EVERY
  diagnostics". Keeps the prior-session instrumentation (extended IRQ
  trace with Ticks + D0, `MACEMU_PPC_TB_FLUSH_EVERY` knob) in tree but
  gated off by default.
- **`9977f5bd`** — "Unicorn PPC: flush TB cache after I/O EmulOps (SMC
  via host writes)". After `g_platform.ppc_emulop_handler` returns for
  `SONY_PRIME`, `DISK_PRIME`, `CDROM_PRIME`, `SOUNDIN_PRIME`,
  `EXTFS_COMM`, `EXTFS_HFS`, `GET_SCRAP`, call `uc_ctl_flush_tb`.
  These are the paths that `memcpy` from host into guest RAM through
  `Sys_read` in `src/core/disk.cpp:310` and bypass the TLB entirely,
  so `notdirty_write` never fires and the stubbed
  `cpu_physical_memory_set_dirty_flag` can't help.

### Impact

| Config | Before dedup+flush | After dedup+flush |
|---|---|---|
| SCALE=10 | Finder 9.64s, then post-Finder wild-pointer crash | **Finder 12.30s, 2550 DiskPrimes/60s, no fatal** |
| SCALE=1 | Stuck at "Boot globals patched" forever (IRQ pressure) | Boot blocks 0.41s, **only 50 DiskPrimes/60s, no Finder** |

SCALE=10 is the new default for reaching Finder. SCALE=1 now *starts*
cleanly (WLSC 0.30s) but **wall-clock throughput collapses** after
boot blocks: only ~50 DiskPrimes/60s at SCALE=1 vs ~2550 at SCALE=10
= **~50× slower**, despite SCALE=1 supposedly being the "real" 60Hz
tick rate. Zero errors, zero unmapped skips, zero warnings — pure
throughput starvation, consistent with the engine spending almost
all its time on IRQ entry/exit rather than forward progress.
`uc_emu_stop`+re-enter at 60Hz is ~60 TB-tears/sec, and dedup only
prevents *redundant* tears, not the mandatory one per rising edge.
The SCALE=10 post-Finder `0x21000000` r3/r5/r6/r7 corruption is now
strictly a post-Finder artifact — doesn't block reaching desktop.

### Theory status, updated

| # | Theory | Status |
|---|--------|--------|
| 2 | Stale TB / SMC from dirty-flag stub | **Partial fix shipped** — targeted flush after I/O EmulOps. Full bitmap still pending for writes that bypass EmulOp dispatch entirely. |
| — | IRQ-pressure / redundant tick-kick | **Fixed** in `398e1868`. No longer a contributor at any SCALE. |
| — | SCALE=1 post-boot-blocks stall | **New** — not the old IRQ-pressure stall. Leading candidate: one of the drivers/extensions that runs between "boot blocks" and "extensions" phases needs host-visible writes that our targeted flush misses. |

### Next session — entry points

1. **SCALE=1 wall-clock starvation** (reframed). DiskPrime rate is
   ~50/60s at SCALE=1 vs ~2550/60s at SCALE=10 — a 50× throughput
   collapse, not a stall. The engine is spending most of its CPU on
   the IRQ entry/exit round-trip: one mandatory `uc_emu_stop`+TB-reenter
   per rising-edge tick = ~60/s. Options to reduce IRQ overhead:
   - (a) Make IRQ delivery *in-place* — check pending flag at TB
     boundaries without `uc_emu_stop` (needs Unicorn hook support or
     a host-side check inside a tight outer loop with `UC_HOOK_BLOCK`).
   - (b) Batch ticks — at SCALE=1 coalesce multiple ticks if the guest
     is slow, so we raise INTFLAG_VIA once and let the guest's own
     `Ticks` counter catch up. Trades IRQ precision for throughput.
   - (c) Accept SCALE≥5 as the shipping default and document it as
     the CPU-performance knob until (a) lands.

2. **Full dirty-bitmap restoration** (task #3 / old task #19). The
   targeted flush is a load-bearing workaround, not a fix — any new
   host-to-guest write path we add in the future will silently break
   SMC again. Proper fix: per-RAMBlock bitmap in
   `subprojects/unicorn/qemu/include/exec/ram_addr.h:75`, update
   `cpu_physical_memory_set_dirty_flag` / `_range` / `_test_and_clear`,
   wire `tb_invalidate_phys_page_fast` on dirty→clean transitions.
   This also removes the cache-wide flush cost (currently ~unmeasured
   but likely the reason SCALE=10 Finder regressed 9.64→12.30s).

3. **SCALE=10 register corruption to 0x21000000** — still unexplained,
   but now masked behind the successful Finder path. Revisit if
   post-Finder interactive use shows crashes.

## 2026-04-21 — block-hook IRQ delivery unlocks SCALE=1

### Commit

- **`1e9dfbd0`** — "Unicorn PPC: in-place IRQ delivery via UC_HOOK_BLOCK".
  Replaces the cross-thread async `uc_emu_stop` from `uppc_tick_thread`
  with an always-on `UC_HOOK_BLOCK` callback. The tick thread now only
  sets `g_pending_irq`; the block hook checks the flag at each TB entry
  and exits cleanly via same-thread `uc_emu_stop` at a natural TB
  boundary. No more mid-TB register unwind from TCG IR on every IRQ.

### Impact

| Config | Before block hook | After block hook |
|---|---|---|
| SCALE=1  | **Never reached Finder in 60s** | **Finder ~10.15s** (three runs: 10.12 / 10.17 / 10.19) |
| SCALE=10 | Finder 12.30s                   | Finder 21.11s (hook overhead on every TB entry) |

SCALE=1 now boots *faster* than the old SCALE=10 baseline. The block
hook trades a small per-TB cost (one atomic load + compare + int
compare) for eliminating the mid-TB state-unwind cost per IRQ. At
60Hz tick rate the trade is overwhelmingly positive; at 6Hz the hook
overhead slightly dominates. Since SCALE=1 is now the preferred
configuration, the SCALE=10 regression is moot.

Rollback knob: `MACEMU_PPC_NO_IRQ_HOOK=1` reinstates the legacy async
`uc_emu_stop` from the tick thread (no block hook installed).

### Open items

1. **Make SCALE=1 the default.** Currently the default `SCALE` is 10
   (set elsewhere — check `MACEMU_PPC_TICK_PERIOD_SCALE` default).
   After confirming a few more workloads, flip the default.
2. **Block-hook overhead characterization.** ~70% Finder-time regression
   at SCALE=10 suggests per-TB hook cost is meaningful. If the hook
   function-pointer dispatch is the bottleneck (not the atomic load),
   inlining the check into an existing block hook (e.g. merging with
   `last_pc_cb`) might recover some of that. Profile first.
3. **Post-Finder `0x21000000` register corruption** remains a separate
   bug, independent of the IRQ delivery path.

## 2026-04-21 — dirty-bitmap restored (task #3 / old #19)

### Commit

- **(this commit)** — "Unicorn: restore per-RAMBlock CODE dirty bitmap".
  Replaces the stubbed `cpu_physical_memory_{is_clean,set_dirty_flag,
  set_dirty_range,test_and_clear_dirty}` implementations (which always
  answered "clean=true / nothing dirty") with a real per-RAMBlock
  bitmap. One bit per `TARGET_PAGE_SIZE` page, allocated in
  `ram_block_add`, freed in `reclaim_ramblock`. Bit=1 means the page is
  dirty (no compiled code is watching); bit=0 means a TB covers this
  page and writes must go through `notdirty_write`. `tb_set_page` marks
  pages clean via `tlb_protect_code`; `notdirty_write` re-dirties them
  via `tlb_unprotect_code` once all TBs on that page are gone. All
  helpers now take `struct uc_struct *uc` (needed to resolve the
  address → block lookup per-arch).

### Implementation notes

- `RAMBlock.dirty_code_bmap` lives in `subprojects/unicorn/include/qemu.h`
  (same struct-definition-here-due-to-circular-include hack the block
  already has).
- `qemu_get_ram_block` promoted from `static` → extern in `exec.c`;
  declared in `ram_addr.h`; added to `symbols.sh` COMMON_SYMBOLS so
  `symbols.sh` regenerates the per-arch `#define ... _ppc`/`_m68k`/...
  mangling (otherwise link-time multiple-definition).
- Only `DIRTY_MEMORY_CODE` is tracked. VGA and MIGRATION clients are
  no-ops (return `false` / skip), matching how Unicorn has never needed
  them.

### Impact

| Config | Before (stub) | After (bitmap) |
|---|---|---|
| PPC Finder, G3 ROM + 7.6.1, SCALE=1 | 10.15 / 10.17 / 10.19 s | **9.64 / 9.69 / 9.69 / 9.77 s** |
| 68k Finder, quadra.rom | stalls in extensions phase (~2950 disk primes at 170s) | **identical** — pre-existing, not caused by this change |

The ~0.5s PPC improvement comes from `TLB_NOTDIRTY` now correctly
clearing on pages without compiled code, so writes skip the
`notdirty_write` slow path and `tb_invalidate_phys_page_fast` work that
was happening speculatively for every host-visible write before. The
targeted post-I/O-EmulOp flush from commit `9977f5bd` remains —
host-pointer writes from the Platform API still bypass the TLB and so
bypass the bitmap. Removing the workaround is a separate task and
needs per-write `cpu_physical_memory_set_dirty_range` calls (or a
wrapper on the host-pointer write paths).

### Post-Finder behavior

Post-Finder `0x21000000`/`0xa4000000` register corruption still
reproduces. Unaffected by this change (as expected — the corruption is
on the execute path, not the dirty-tracking path).

### Open items

1. **Host-pointer write tracking.** The Platform API writes guest RAM
   directly via `RAMBaseHost[...]`; those bypass both the TLB and the
   bitmap. Either (a) keep the targeted flush workaround indefinitely,
   (b) call `cpu_physical_memory_set_dirty_range` at every Platform
   write site, or (c) narrow the flush to ranges that actually hold
   compiled code (check `tb_tree`). Current workaround is load-bearing;
   don't remove without a replacement.
2. **68k Unicorn extensions-phase stall** is pre-existing (reproduced
   on the baseline without this change). Separate investigation.
