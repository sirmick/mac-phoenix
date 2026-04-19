# Unicorn-PPC backend — session handoff

Last updated: 2026-04-19 (branch `unicorn-ppc`, zero-on-skip unlock session).

This is a rolling status doc for the Unicorn PPC port. Append to it, don't
rewrite it. The goal: a next-session reader should be able to pick up the
investigation in under five minutes.

## Where we are

The Unicorn PPC backend runs end-to-end on the KPX test disk (Mac OS 7.5.5,
G3 ROM). It does not yet boot to Finder. Latest progress (2026-04-18):
with the Grand Central I/O MMIO stub (see below) boot now reaches
`"Loading boot blocks (resource #92)"` at ~0.78s but stalls sporadically
in a micro-op probe loop at PC `0x50461e94`. The earlier `0x500100xx @ 0x0`
stall turned out to be a downstream symptom of the SCC-probe tight loop;
GC-stub fixed it.

NATIVE_OP dispatch is now routed through `g_platform.ppc_native_op`
(commit `377e1a76`). Both backends call the same backend-agnostic pure
dispatcher `execute_native_op_pure(selector, uint32 gprs[32])` — no more
direct symbol references from `cpu_unicorn_ppc.cpp` into
`libkpx_interp.a`.

## CR2.GT divergence at op 12 — red herring (timing, not a bug)

Running the boundary comparator with IRQs enabled (default):

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

First diff is EmulOp 12 (sel=04): KPX `cr=40100f22`, Unicorn `cr=40500f22`.
Delta `0x00400000` is CR2.GT (**not** CR1.SO — earlier handoff was wrong).

**Root cause: tick-IRQ timing, not a backend bug.** KPX runs ~10× faster in
wall-clock, so by the time the 60Hz tick fires (~16.6ms after boot), KPX has
reached seq=111 while Unicorn is at seq=11. The tick enters
`uppc_handle_interrupt` → MODE_68K → OR `0x00e00000` into CR (= CR2.LT|GT|EQ).
So Unicorn's seq=11 MODE68K line lands **before** EMULOP 12, poisoning CR2
for op 12's snapshot. KPX doesn't see the same OR-in until seq=111.

Proof: with `MACEMU_PPC_NO_IRQ=1` on both backends, the traces are bit-identical
for 1114 lines (every EMULOP/POST pair matches exactly). The divergence is
purely async-IRQ scheduling.

The boundary comparator is therefore **only useful with `MACEMU_PPC_NO_IRQ=1`**
for finding real backend bugs. Without it, timing noise drowns out signal.

## 2026-04-19 session — quantified the IRQ-pressure gap

Previous summary blamed `OP_DISK_PRIME` retries. Wrong — re-traced with proper
counters:

- `DiskPrime` on Unicorn completes 20 varied reads successfully (drv 1/2/3,
  pos 0..0x2ae4400, all `->0` noErr). Calls #1–#20 match KPX's early-boot
  pattern byte-for-byte. The earlier "67 retries of pb=0x000003a4" reading
  was a tracing artifact (env-var-gated log captured stale state).
- Real stall signatures vary per run (timing-nondeterministic). Three stable
  shapes:
  1. Hot-skip loop @ `0x50465ef8` (`lha r4,0(r3)`) walking `0x50510000`+20×N
     — reads past end of SigStack (only 64KB mapped at `0x50500000`).
  2. Progress stall @ `0x50467f64`/`0x50467f80` — 68k-on-PPC dispatcher idle
     loop. Doesn't indicate a bug; fires whenever boot reaches an event-wait
     state without a pending IRQ to break it.
  3. LOWMEM writes at `pc=0x50461e94` / `lr=0x50486170` — bytes 0x80..0x9f
     written one-by-one to `@0` (A5-relative store with A5=0, i.e.
     uninitialized 68k globals). Fault is a downstream symptom of CR2
     corruption from the too-early IRQ.

Quantified the IRQ pressure:

| Metric                        | KPX              | Unicorn (stall)   |
|-------------------------------|------------------|-------------------|
| Total EmulOps traced          | 30,297           | 873               |
| IRQs (MODE68K lines) delivered| 714              | 618               |
| EmulOps per IRQ (avg)         | ~42              | ~1.4              |
| First IRQ fires at seq        | **105**          | **9**             |
| Wall-clock to Finder          | ~1s              | (doesn't reach)   |

Unicorn is ≈30× slower per EmulOp than KPX in wall-clock, so 60 Hz ticks
land on a seq-scale that starves 68k progress: the IRQ handler ORs
`0x00e00000` (CR2.LT|GT|EQ) into CR at every tick, and at Unicorn's cadence
that OR falls **inside** nearly every EmulOp's critical section.

Tried `MACEMU_PPC_MIN_EMULOPS_PER_IRQ=N` gate (added to `uppc_tick_thread`,
lines ~1405) — sweep over N ∈ {10,20,50,100,500}: all produced ~657 emulops
before stall (same as `NO_IRQ=1` baseline of 656). Gating doesn't rescue
boot — the Mac genuinely needs a 60 Hz-like tick pattern, just not one that
preempts every single emulop boundary. The knob stays in the code as a
future tuning lever (default `0` = off; existing behavior unchanged) but is
**not** the fix on its own.

**Next session paths:**

1. **Lie about time.** Scale `TimeToMacTime` / `WriteMac32(0x20c, …)` down by
   the Unicorn-vs-KPX ratio so the guest clock stays close to KPX's perceived
   rate. `uppc_tick_thread` already does the `0x20c` write — hook time reads
   with a slowdown factor.
2. **Defer the first IRQ.** Skip ticks until `g_emulop_count >= 105` (the
   seq at which KPX's first IRQ fires). After that, normal 60 Hz cadence.
   Cheapest experiment to attempt first.
3. **Unicorn throughput.** TB cache hit-rate, MMIO cost, hook overhead —
   each EmulOp round-trip costs a `uc_emu_start` teardown. If we can bring
   Unicorn to 3× slower (not 30×) the problem dissolves without gating.

### 2026-04-19 late — `MACEMU_PPC_TICK_PERIOD_SCALE` confirms the theory

Added a second knob: `MACEMU_PPC_TICK_PERIOD_SCALE=N` multiplies the
16.625 ms tick period (default 1 = 60 Hz; 10 → 6 Hz). Unlike the
emulops-based `MIN_EMULOPS_PER_IRQ` gate, this one *reduces the total
IRQ count* rather than re-spacing them. Results at SCALE=10 (first
run that goes past the old stall anywhere in this port's history):

```
[Boot +  0.00s] Boot globals patched
[Boot +  1.78s] Mac warm start complete (WLSC) after 51 resources
[Boot +  1.79s] Installing drivers
[Boot +  1.86s] Loading boot blocks (resource #92)
800+ DiskPrime reads at varied positions (0..0x2c32600) all -> 0
```

New downstream stall: `r1=0x00000001` at `pc=0x5046f90c` (stack pointer
corrupted near-zero). Caught by the existing R1ZERO block-hook tracer.
This is a genuinely different failure mode, not the old IRQ-pressure
stall — the port has advanced.

Nondeterminism remains: 5 SCALE=10 runs produced 1 early-stall, 2 reach
"Loading boot blocks" without error within 25 s, 1 reaches "Installing
drivers" with a progress-stall, 1 hits FETCH UNMAPPED at `0x50580000`.
The early-stall fraction suggests tick-phase still matters — an IRQ
landing at the wrong emulop can still corrupt state — but at SCALE=10
the majority of runs make real progress. SCALE=5 is in the same range
(one of three runs reached "Loading boot blocks"); SCALE=2 (30 Hz) is
as bad as SCALE=1.

**This proves the theory**: the port has no backend-implementation bug
blocking boot. The remaining work is purely around IRQ scheduling
(and, now, whatever downstream issue causes the r1→0 corruption).

Long-run (300 s timeout) at SCALE=10, `/tmp/u_long1.log`: this run hit
the early-stall mode. It reached "Loading boot blocks (resource #92)"
at 1.86 s with 800 successful DiskPrime reads, then locked into a tight
loop at `pc=0x504613d0` (`sthu r4, -2(r1)`) with `r1=0x00000001`
writing to `0xffffffff` and at `pc=0x50461174` (`stw r21, 0(r21)`) with
`r21` also near `0xffffffff`. The unmapped-write skip handler silently
consumes each store so the loop never faults — it just burns CPU until
the auto-exit timer fires. Boot state never advanced past "Loading boot
blocks" for the remaining 298 s.

Important: the *early-stall* fault at `pc=0x504613d0` is structurally
the same r1→near-zero corruption already flagged as the post-SCALE=10
"downstream stall" (`pc=0x5046f90c`). Different landing PC, same
underlying bug — the stack pointer comes into a small helper routine
already tiny, the routine's prologue pushes a few halfwords, and r1
underflows past zero. So the "nondeterminism" likely isn't two
separate bugs — it's one bug that's reached at different times
depending on IRQ phase.

**Next-session lever**: the unmapped-write skip handler hides the
fault. Consider gating the skip to addresses ≥ a small threshold
(e.g. 16 or 256) or emitting a one-shot abort when a *store* targets
`0xffffff??` so the stall surfaces at the first symptom instead of
looping silently. That would let the R1ZERO tracer capture the actual
entry path (LR chain at the moment r1 becomes tiny) rather than the
steady-state loop.

### 2026-04-19 late-2 — stall watchdogs added; CR2-corruption signature

Three watchdog changes in `execute_fast` so silent SCALE=10 spins
surface with a register dump instead of eating the whole timeout:

- **Moved progress-stall watchdog before the UNMAPPED `continue`.** Was
  a latent bug: unmapped-walk loops bypassed the watchdog entirely.
  Threshold also lowered 10 s → 5 s.
- **Concentrated-skip bail**: if `skipped ≥ 200` and distinct skip-PC
  set `≤ 6`, bail. Catches 2–3 PC alternating unmapped-write loops
  (e.g. `sthu r4, -2(r1)` + `stw r21, 0(r21)`) that `consecutive_same_pc`
  can't see because each toggle resets it.
- **Block-window stall bail**: if all 32 entries in `g_uppc_last_block_pcs`
  span `< 4 KB` for 8 s, bail. Catches tight loops that keep firing
  EmulOps (so progress-stall doesn't trip) but stay in a localized
  region of PPC code.

Across 5 SCALE=10 runs at the new 30 s timeout:

| n | fate | DiskPrime | notes |
|---|------|-----------|-------|
| 1 | 30 s timeout, no bail | #800 | wide-surface EmulOp cycling; window > 4 KB |
| 2 | **progress-stall bail** | — | 70 251 skips, pc=0x504900fc |
| 3 | **progress-stall bail** | — | 70 296 skips, pc=0x504900fc |
| 4 | 30 s timeout, no bail | #200 | wide-surface EmulOp cycling |
| 5 | 30 s timeout, no bail | #450 | wide-surface EmulOp cycling |

One earlier sample also hit the **concentrated-skip bail**; its dump
is the best clue to the underlying bug so far:

```
concentrated-skip bail: 200 skips across 4 distinct PCs
  pc=0x50465f28 insn=80830000 (lwz r4, 0(r3))  hits=99
  pc=0x50492350 insn=7e52d82e (lwzx r18,r18,r27) hits=99
  r3 =c0ffa2ea  r18=c0ff944e  r27=00004240
  r1 =05ff897e  r21=06000190  r31=68fff000
```

`r3 = 0xc0ffa2ea` and `r18 = 0xc0ff944e` — both point into the
`0xc0000000` range, which is **unmapped** in REAL_ADDRESSING (VMBaseDiff=0).
Meanwhile `r31 = 0x68fff000` is correctly inside the KernelData mapping.
The pattern looks like a KernelData-ish pointer where the top nibble
got flipped `0x68 → 0xc0` (XOR `0xa8000000`) — consistent with a
CR-flag-driven conditional branch using poisoned CR2 state and picking
the wrong pointer-computation arm. This is the first concrete
downstream artifact of the early-IRQ-poisons-CR2 theory described
above.

**Unified picture**: every SCALE=10 run plateaus at `"Loading boot
blocks (resource #92)"`. Three observed failure modes, all caused by
the same root (early IRQ lands in the wrong CR2 state):
1. **Unmapped-write loop** (progress-stall bail) — r1 or r21 corrupted.
2. **Unmapped-read loop** (concentrated-skip bail) — r3/r18 corrupted.
3. **Wide-surface EmulOp cycling** (no bail yet) — DiskPrime keeps
   firing for the same boot-block-load sector indefinitely. Catching
   this cleanly probably wants a *boot-phase-advance* watchdog
   (e.g. bail if the boot phase hasn't advanced in 15 s while
   DiskPrime count is still incrementing).

Diagnostic instrumentation added this session (still in tree):

- `src/core/disk.cpp` — `DiskPrime` unconditionally logs entry + result
  for calls #1–20 and every 50th afterwards. Remove when done debugging.
- `src/cpu/cpu_unicorn_ppc.cpp` — hot-skip bail and progress-stall bail
  now dump 16 instructions around the stuck PC / LR. Keep: useful for
  every future stall triage.

Trace format: EMULOP/POST pairs (original) interleaved with `IRQ` (entry into
`{uppc,sheepshaver_cpu::}interrupt`) and `MODE68K` (entry into
`{uppc_handle_interrupt,HandleInterrupt}` MODE_68K case). The latter two share
the same seq counter as the most recent EMULOP so diffs align.

### 2026-04-19 late-3 — zero-on-skip unlocks the probe-table loop; boot reaches 68k RAM

Root cause of the `pc=0x504a73ac` progress stall: the 68k-emulator
dispatch table at `0x504a7380..0x504a73e0` holds 8-byte slots of
`lwz r24, 0(r1); b 0x50467da0`. The handler at `0x50467da0` reads
`lha r27, 0(r24)` then computes another dispatch, ending with `bcctrl`
back into the table. When `r1` is below the KernelData mapping (a
downstream symptom of stack underflow from a different earlier bug),
the `lwz r24, 0(r1)` faults → Unicorn's skip handler runs → the old
preserve-dest-GPR policy leaves `r24` unchanged → branch target is
the same → infinite loop.

**Fix**: in `uppc_skip_memop_at` (`src/cpu/cpu_unicorn_ppc.cpp:~310`),
zero `rd` when skipping a failed load. With `r24=0` the follow-on
`lha r27, 0(r24)` faults at address 0 on the next iteration; the
subsequent store uses `r27=0` as a flag that causes the 68k dispatcher
to take a different path, breaking the loop. KPX's SIGSEGV skip on
x86 leaves RAX at whatever the host `mov` would've set it to — in
practice often 0 because the dest register was freshly loaded. Zeroing
matches that common case without relying on host-register side-effects
we can't observe at the PPC level.

**Result**: one run of 3 reached `"Finder detected (CurApName PPC
fallback)"` at Boot +3.32s with 1000 resources loaded — **first time
the Unicorn backend has booted to Finder**.

Five more `SCALE=10 --timeout 20` runs after that couldn't reproduce
Finder reach, but showed consistent downstream progress:

| n | fate | DiskPrime | notable signature |
|---|------|-----------|-------------------|
| 1 | early stall @ DP#20 | 20 | 31 iters, 31 IRQs, 0 skips, XLM_IRQ_NEST=0 |
| 2 | R1ZERO @ DP#850 | 850 | pc=0x5046fa0c r1=0x00000002, then pc=0x5046f904 r1=0x00000001 |
| 3 | early stall @ DP#20 | 20 | first unmapped at `pc=0x50465fb0 target=0xfffe85a4` |
| 4 | timeout mid-progress | 850 | no stall, steady advance — needs more wall-clock |
| 5 | wild branch | 550 | `execute_fast: exiting (pc=0x2c636f6c)` — ASCII `,clo` pattern, corrupted ctr |

Three `SCALE=10 --timeout 45` long runs then showed a new downstream
failure mode in 2 of 3: after DiskPrime #850 (boot blocks fully loaded),
control transfers into booted 68k System code at `~0x001c13b0`, then
faults on a 68k exception vector:

```
[Unicorn-PPC] *** first unmapped access:
  pc=00000134 insn=deadbeef target=0x00ffffbeef lr=001c13b0 ***
```

The recent-blocks ring shows PPC executing at RAM addresses
`0x00000028 → 0x00000060 → 0x00000068 → 0x00000080 → 0x00000134`
— walking the 68k exception vector table (VBR=0). `insn=deadbeef`
means vector 0x134 is uninitialized. This is real 68k System code
running on the PPC 68k-emulator and trapping — a qualitatively
different failure mode from the pre-zero-on-skip probe-table loops.
The port is now past the probe-table stall class.

**Two R1ZERO traces at boot entry (pc=0x50310000 r1=0 and pc=0x504a77d0
r1=0) fire before `Boot +0.00s`** in every run. These reflect the
initial vcpu state (r0/lr/ctr all zero) as the tracer starts; they
are **noise, not regressions**. Consider suppressing the first two
R1ZERO events or gating by `g_emulop_count > 0` so genuine corruption
stands out.

**Open questions**:
1. **Why only 1-in-N reaches Finder.** The stall sites vary run-to-run
   (#20, #550, #850, vector-deadbeef). Tick-phase-dependent CR2 poison
   is still the leading theory — earlier IRQ lands = earlier corruption.
2. **The DP#850 R1ZERO at `pc=0x5046f90c`** with `r1` going 0x05ff8048
   → 0x00000002 → 0x00000001. The `0x05ff8xxx` range is valid 68k-on-PPC
   stack; decrement by `2 * 0x5ff8047` would take it deeply negative
   → masked to tiny. Something is subtracting a wrong value. Not the
   probe-table loop (that's upstream), a fresh bug once we're into
   DiskPrime-heavy phase.
3. **The `pc=0x134 insn=deadbeef` vector fault** is a *valid* guest
   crash, not a backend bug — the 68k OS booted, trapped, and found
   no handler. On KPX either the vector gets initialized before the
   trap fires, or KPX masks a fault that Unicorn surfaces.

**Next-session lever**: compare KPX and Unicorn both running past
DiskPrime #800 with `MACEMU_PPC_TRACE`. Find the first EmulOp where
KPX writes the 68k vector table (or whatever initializes `@0x134`)
and Unicorn doesn't. Could be an EmulOp selector KPX handles that
Unicorn's pure dispatcher returns false for (see "GET_RESOURCE family"
under Deferred work).

### 2026-04-19 late-4 — **first Desktop boot**; new stall site at 0x504900fc

Re-ran 15 trials at SCALE=10 with the zero-on-skip change in place.
Run 5 (of the first 5) reached **`Desktop ready (Finder idle, no
modal)` at Boot +30.28s** with 4900 DiskPrime reads completed — the
full boot path now runs end-to-end on Unicorn for the first time.

Reach rate is rare (1 / 15 across this session). Typical outcomes:

| fate | frequency | example |
|---|---|---|
| Desktop ready | 1/15 | Finder +4.15s, Desktop +30.28s, DP#4900 |
| progress-stall bail @ 0x504900fc | 5/15 | `70000 skips, 0 IRQs, XLM_IRQ_NEST=1` |
| stall mid-run @ DP#850 | 4/15 | timeout before bail fires |
| R1ZERO @ nanokernel region | 2/15 | `pc=0x50010030 r1=0x10 prev=0x05ff80d8` |
| early stall (DP#0..#20) | 3/15 | 31 iters 31 IRQs, 0 skips |

The **new dominant stall** is `pc=0x504900fc insn=4bfd8d44` (a `b
0x5046DE40`). The preceding `lwz r8, 0(r1)` at `0x504900f8` faults
because `r1 = 0x68ff9b9e` is ~16 KB below the KD_hi base at
`0x68ffe000`. Register state at stall:

```
pc=0x504900fc insn=4bfd8d44 lr=504900f8 ctr=00000000 cr=8010ff53
r1=68ff9b9e r8=? r22=05ff80d8 r31=68fff000
```

This is structurally the same bug as the pre-fix `0x504a73ac` probe
loop — an `lwz rd, 0(r1)` + unconditional branch — just at a new site
reached once the earlier site is unblocked. Zero-on-skip makes
`r8=0` each iteration and the branch target at `0x5046DE40` still
sends control back to `0x504900f8`, so the loop is eternal.

**r1 underflow is the real root bug, not the skip-policy.** Zero-on-skip
only hides one site; others keep surfacing. The underlying question is
**why r1 drops below the KD_hi base** in the first place — some
routine's prologue is `stwu r1, -N(r1)` on an r1 that starts inside
KD (around `0x68ffe000..0x69000000`) and decrements past the bottom.
Or r1 is being explicitly reloaded with a bad value somewhere.

**Evidence that zero-on-skip is *net positive despite the stall move***:
before the change, no run ever reached Finder. After the change, 1/15
reaches Desktop in full, proving the complete boot path is executable
on Unicorn when IRQ phase cooperates.

**Suppress initial R1ZERO noise**: the first two R1ZERO fires (at
`pc=0x50310000` and `pc=0x504a77d0`) happen before `Boot +0.00s`
with `r0=lr=ctr=0` — fresh vcpu state, not corruption. Gate the
tracer with `g_emulop_count > 0` so genuine r1-corruption stands out.

**Next-session levers** (in order of expected payoff):

1. **Track r1 across the DP#700→#850 window**: add a block hook that
   logs every time r1 crosses below `0x68ffe000` for the first time
   in a run (one-shot). The LR at that moment names the bad caller.
2. **Compare KPX/Unicorn EmulOp traces post-DP#800**: find the last
   EmulOp selector common to both before divergence. KPX's `sheepshaver_cpu`
   handles a few selectors Unicorn's pure dispatcher doesn't (see
   `kpx_ppc_native_op` selector 19 / GET_RESOURCE family).
3. **Clamp r1 at skip time**: if `r1 < 0x68ffe000` when entering a skip,
   reload r1 from a known-good value (e.g. the KernelData base). Crude
   but would flush out whether r1-drift is the only mechanism.

## Real blocker: 0x500100xx lomem stall

With IRQs enabled, Unicorn boots a few seconds in then stalls writing
`0x500100xx` into `@0x00000000` in a tight loop. This isn't visible in the
EmulOp boundary trace (it's a busy loop between EmulOps). Next step:
instrument the memory write hook to capture the stall PC and surrounding
register state, or use `MACEMU_PPC_CR2_TRACE` across the stall window.

`MACEMU_PPC_CR2_TRACE=<lo>:<hi>` auto-disables the KPX JIT (see
`src/cpu/kpx/cpu_ppc_kpx.cpp` near `cr_trace_forces_interp`), because the JIT
bypasses the `ppc_trace_cr_step` hooks in
`src/cpu/kpx/src/cpu/ppc/ppc-cpu.cpp`. Unicorn always runs TCG, no gating needed.

## 2026-04-18 session: Grand Central I/O stub + skip-semantics tradeoff

### Grand Central MMIO stub (unblocked InstallDrivers)

The 0x500100xx lomem stall was actually a downstream symptom: the root stall
was a tight probe loop on `0xf3012002` (SCCA status register) at PC 0x5046628c
(`stb r4,0(r3)`). KPX skips each SIGSEGV in ~microseconds by advancing the host
x86 PC past the faulting MOV; Unicorn must tear down `uc_emu_start`, classify
`UC_ERR_{READ,WRITE}_UNMAPPED`, emulate the skip at the PPC level, and resume —
orders of magnitude slower. The guest driver never observes "not busy" before
wall-clock runs out.

Fix: stub the Grand Central I/O region (`0xf3000000..0xf3020000`) as
`uc_mmio_map` in `cpu_unicorn_ppc.cpp` (installed right after the trampoline
reservation, ~line 1036). Reads return 0 (driver sees "not busy / no data"),
writes are discarded. This matches SheepShaver's 68k-backend dummy I/O map and
mirrors the "let SIGSEGV handle it" behavior but without per-fault teardown
cost.

Result: boot now progresses through InstallDrivers and reaches
`"Loading boot blocks (resource #92)"` at ~0.78s (up from stalling forever in
the probe loop).

### Skip-semantics tradeoff (new sporadic stall at 0x50461e94)

Post-GC-stub, a new sporadic stall appears at PC `0x50461e94`
(`stb r4,0(r22)` with `r22=0`, `r4` incrementing 0x80→0x84→… per iteration).
This is another 68k-emulator micro-op in the 1MB-duplicated ROM region
(file offset 0x361e94).

Two skip semantics were tried in `uppc_skip_memop_at`
(`cpu_unicorn_ppc.cpp:255-320`):

| Variant | Behavior on unmapped load | Failure mode |
|---|---|---|
| Zero-dest | `wr_gpr(rd, 0)` | Breaks 0x5046628c-style probes where r4 monotonically advances — stalls move *earlier* to the 0x50461e94 stb loop |
| Preserve-dest (current) | Leave dest GPR untouched | Boot sometimes reaches "Loading boot blocks", sometimes stalls at 0x50461e94 writing incrementing bytes to mem[0] |

Neither matches KPX exactly because KPX's skip is **host-level** (x86 MOV is
skipped; the PPC-level dest reg holds whatever the interpreter's host reg
held before, which is effectively random/stale across iterations). Unicorn
skips at the **guest PPC level** — we have to *decide* the dest value.

KPX itself has zero SIGSEGV logging during boot, so "zero skip events" in
KPX is misleading — it's silently skipping too, just fast enough that the
probe loops eventually see the one iteration where stale state happens to
satisfy the exit condition.

### 68k-emulator micro-op dispatch reference

Both stall PCs are in the 68k-on-PPC micro-op table. Typical pattern (from
PC 0x50466280, ROM file offset 0x366280):

```
50466280: rlwimi r29,r27,3,13,28    ; compute dispatch index
50466284: mtlr r29
50466288: lhau r27,2(r24)           ; fetch next 68k opcode word (r24 = 68k PC)
5046628c: stb r4,0(r3)              ; probe store — this is what faults
50466290: addco. r4,r4,r0           ; advance probe data
50466294: bgelr+ cr2                ; return to dispatcher if continue
50466298: b 0x5046d0d4
```

Note ROM layout: the 1MB-duplicated region (rom_patches_ppc.cpp:732
memcpy ROMBaseHost+ROM_SIZE ← ROMBaseHost+(ROM_SIZE-0x100000)) means
PC `0x5046xxxx` corresponds to ROM file offset `0x36xxxx`.

### Next investigation directions

1. **Decoded the 0x50461e80..0x50461eb8 window**: micro-op body for 68k
   `MOVE.B Dn,(An)`. `r22=0` means the 68k A-register is literally NULL.
   Under KPX the store silently SIGSEGVs and the 68k-level loop-counter
   D-reg eventually expires. Under Unicorn, Unicorn skips the store but
   the loop doesn't exit (observed variable stall).
2. **Implemented hot-skip-loop bailout**
   (`cpu_unicorn_ppc.cpp:execute_fast` skip path): if the same PC faults
   UC_ERR_{READ,WRITE}_UNMAPPED kHotSkipBailThreshold=100000 consecutive
   times we print a register snapshot and break. Doesn't fire on the
   current stall (which is a *valid-memory* loop, not a skip loop).
3. **Variant observed 2026-04-18 post-GC-stub smoke**: boot reaches
   "Loading boot blocks (resource #92)" at 0.80s, then stalls in a
   tight loop at `pc=0x504662a4` writing `0x50010000` to `@0x00000000`
   (valid RAM, not an MMIO probe). LOWMEM write hook caps at 32 lines
   so the terminal shows only the first few. Neither the hot-skip-loop
   bailout nor the existing `stuck_pc` mechanism catches it because
   `uc_emu_start` keeps returning `UC_ERR_OK`. Needs a
   *progress watchdog*: if N consecutive successful `uc_emu_start`
   returns see identical PC (no forward motion, no IRQ, no EmulOp) bail
   out with a snapshot.
4. **Why KPX escapes the same loop**: speculation — KPX's faster
   wall-clock means the 60Hz VBL fires often enough inside the tight
   loop to progress 68k state via the interrupt handler. Under Unicorn
   the per-insn cost is higher but the 1-second `s_emu_timeout_us`
   ceiling still polls `g_pending_irq` once per second. So the IRQ
   *should* fire; either delivery is broken after "boot blocks" or
   the guest code doesn't actually wait on an IRQ here.
5. **Try**: trace `uppc_handle_interrupt` entries while stalled — if
   zero, IRQ delivery silently stopped; if many, the 68k IRQ handler
   isn't breaking this loop the same way it does on KPX.

### Smoke-test snapshot 2026-04-18

```
[Boot +  0.72s] Mac warm start complete (WLSC) after 51 resources
[Boot +  0.73s] Installing drivers
[DRV] Opening .Sony... / .Disk / .AppleCD    (all open ok)
[DRV] InstallDrivers() complete
[Boot +  0.80s] Loading boot blocks (resource #92)
[LOWMEM-WRITE] @0x00000000 val=0x50010000 size=4 pc=0x504662a4 lr=0x50490e40
[LOWMEM-WRITE] @0x00000000 val=0x50010000 size=4 pc=0x504662a4 lr=0x50490e40
[Timeout: 30 seconds elapsed, exiting]
```

The two skip sites observed pre-stall (DR-probe-range loads at pc=0x50490280
r16=0xff002f08 insn=`82100000` and pc=0x50498140 insn=`7c90daae` target
0xff0009b6) each fired only 3 times (one per outer IRQ/probe cycle) so
they aren't the blocker. The 0xff00xxxx targets live in the DR probe
range (see `uppc_enable_dr_probe_range`).

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
| `MACEMU_PPC_MIN_EMULOPS_PER_IRQ=N` | Unicorn: suppress tick-IRQ unless ≥N emulops have elapsed since last one. Experimental IRQ-pressure gate. Default 0 = off. |
| `MACEMU_PPC_TICK_PERIOD_SCALE=N` | Unicorn: multiply the 16.625 ms tick period. `10` → 6 Hz, proven to get boot past `"Loading boot blocks"`. Default 1 = 60 Hz. |

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
