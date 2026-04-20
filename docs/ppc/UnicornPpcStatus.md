# Unicorn-PPC backend — session handoff

Last updated: 2026-04-20 (branch `unicorn-ppc`, post-prune; Finder starts, Desktop doesn't render).

This is a rolling status doc for the Unicorn PPC port. Append to it, don't
rewrite it. The goal: a next-session reader should be able to pick up the
investigation in under five minutes.

## Where we are (2026-04-20, late-9)

**Both PPC backends build and run on G3 ROM + Mac OS 7.5.5 / 7.6.1 images.**
KPX is end-to-end clean: reaches a visible Desktop with BridgeAgent
heartbeating in ~1.6 s on 7.6.1 (restored from `.bak` — see late-9 note).
Unicorn **does not** reach a visible Desktop yet.

**Manual UI test result (user, 2026-04-20):**

| Disk     | Backend | Outcome |
|----------|---------|---------|
| 7.6.1    | KPX     | Desktop renders, BridgeAgent responds, shutdown via event bridge OK |
| 7.6.1    | Unicorn | Finder detected by log, but cursor stays hourglass, **crashes just before Desktop paints** |
| 7.5.5    | KPX     | Desktop renders, shutdown OK |
| 7.5.5    | Unicorn | **Hard crash** earlier than 7.6.1 |

Headless log line `[Boot +N.Ns] Desktop ready (Finder idle, no modal)` is a
phase-flag flip from CurApName + idle-poll, **not** evidence Finder painted
the Desktop — Unicorn can flip the flag while the guest is still showing the
hourglass and about to crash. Treat headless `Desktop ready` as "Finder
started, didn't finish." See `feedback_ppc_desktop_phase_vs_visual.md`.

**Current blocker:** between `Finder detected` and visible Desktop, the
guest either hangs on hourglass or takes a wild-PC exception. 7.6.1 gets
measurably further than 7.5.5, so default the Unicorn test disk to 7.6.1
when triaging this from now on.

### Previous wall (late-8, for context, still partially true)

With the Grand Central I/O MMIO stub (see 2026-04-18 section) boot now
reaches `"Loading boot blocks (resource #92)"` at ~0.78s. The earlier
`0x500100xx @ 0x0` stall turned out to be a downstream symptom of the
SCC-probe tight loop; GC-stub fixed it.

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

### 2026-04-19 late-5 — **proof of life: guest OS actually renders**

Launched `mac-phoenix` with UI (port 8000) and `MACEMU_PPC_TICK_PERIOD_SCALE=30`,
polled `/api/screenshot` mid-boot. The guest emitted:

- Classic Mac OS splash with "Starting Up..." progress bar
- Type 10 ("Unexpected exception") bomb dialog **rendered on top of the
  guest splash**, with proper font, icon, and Restart button

This proves the Unicorn PPC backend isn't just executing instructions —
it's running enough of Mac OS 7.5.5 to render real UI through the
framebuffer. The Type 10 is the downstream crash we've been chasing
(walking 68k vector table at `0x28 → 0x60 → ... → 0x134` ending at
`insn=deadbeef`).

**A-trap (Line A, vector 0x28) is the crash source.** The 68k
emulator on PPC dispatches every Mac toolbox call through vector 0x28.
Something initializes `@0x28` incorrectly (or not at all) so when the
first toolbox call after boot-block-loaded fires, it vectors to
garbage, bombs to vector 0x134 which is `deadbeef`, and raises Type 10.

At SCALE=30 the emulator does NOT bail — no progress-stall fires, CPU
stays running indefinitely — but `boot_phase` never advances past
`"boot blocks"` and DiskPrime throughput drops to ~2 ops/sec (vs ~100
ops/sec at SCALE=10). The bomb dialog appears at `boot_elapsed≈90s`
and the CPU keeps churning afterward (probably the bomb's input-wait
loop).

Reach rates observed this session:
- **SCALE=10, 13 runs**: 0/13 reached Finder via UI (all stall at boot blocks)
- **SCALE=10, batch earlier**: 1/15 reached Desktop in full (+30s)
- **SCALE=30, 1 run**: no stall, splash + bomb dialog rendered at ~90s

Stability picture: zero-on-skip is a real unlock (guest renders) but
nondeterminism is severe. The user's intuition — "slower more stable"
— is correct in the "no bail" sense, but slower doesn't cross the
A-trap boundary any better than faster.

**Next session focus (prompt below)**:
1. Pin down the A-trap vector: what writes `@0x28`, `@0x134` etc. on
   KPX and when. Likely an EmulOp dispatch that Unicorn's pure
   dispatcher returns false for, causing the 68k vector table to stay
   uninitialized.
2. Drive stability up before more feature work: either the config-field
   plumbing for `ppc_tick_scale` or a default-when-unicorn so UI launch
   doesn't require env-var dance.

### 2026-04-19 late-6 — **A-trap is byte-shift corruption, not missing init**

Built `MACEMU_PPC_VECTOR_TRACE=1` tracer that logs every write to
`@0x00..@0x400` with 68k register context. Ran ~50 Unicorn SCALE=10 boots
in parallel; ~1-in-10 reach the vector-install phase before stalling. Two
of those runs (saved as `docs/ppc/late-6-artifacts/`) captured the exact
corruption mechanism.

**What happens:**

1. SheepShaver nanokernel zeroes `@0x00..@0xFF` (pc `0x503103f0`). ✓
2. An early longword-install loop (pc `0x50461414`, lr `0x504aa248` /
   `0x504a8e40`) writes `@0x08..@0x57` with correct handler addrs
   (`0x50003040`, `0x50003042`, ..., `0x50003052`, fallback
   `0x50003054`). ✓
3. A dedicated A-trap-install writes `@0x28 = 0x50012af0` (pc
   `0x504662a8`, lr `0x50490e40`). ✓ — matches KPX-known value.
4. **Then a later 68k routine at 68k PC `0x500c61a6` (opcode `0x51c8` =
   DBF seen at PC-2) runs a byte-by-byte copy loop via PPC handler
   `0x50461a74` (`stb r4, 0(r16)`) that REPLACES `@0x08..@0x57` with
   bytes rotated left by 8 bits.** Vector 10 ends up as `0x012af050`
   instead of `0x50012af0` → invalid PPC address → walked via exception
   → hits `deadbeef` → Type 10 bomb.

The full rewritten table is captured in the artifact:
`docs/ppc/late-6-artifacts/vector_corrupt_first_dump.txt`.

**Proof the rotation is systematic (not random corruption):**

Correct longwords (bytes in BE order): `50 00 30 40 50 00 30 42 50 00 30 44 ... 50 01 2a f0 50 00 30 52 50 00 30 54 ...`
Observed after corruption: `00 30 40 50 00 30 42 50 00 30 44 50 ... 01 2a f0 50 00 30 52 50 00 30 54 50 ...`

Each 4-byte group is `rotl(correct, 8)` — the MSB (`0x50`) ends up in
position 3 instead of position 0. Vector 10's bytes `50 01 2a f0` appear
shifted to `01 2a f0 50`, so reading the longword at `@0x28` now gives
`0x012af050`.

**PPC handler disassembly at `0x50461a74`:**

```
0x50461a64: rlwimi r4,r4,24,0,0    ; bit-0 fiddle on r4 (odd)
0x50461a68: rlwimi r29,r27,3,13,28 ; r27<<3 → next handler PC
0x50461a6c: mtlr r29               ; LR = next dispatcher
0x50461a70: lha r27,2(r24)         ; r27 = next 68k instr word
0x50461a74: stb r4,0(r16)          ; ★ THE STORE
0x50461a78: addi r16,r16,1         ; A0++
0x50461a7c: addze. r4,r4           ; r4 += CA, update CR
0x50461a80: bclr BO=5,BI=8         ; conditional return-to-LR
```

This is the tail of a 68k-micro-op handler that does `stb; A0++; conditional-branch`.
r4 is the byte being written; it comes from an earlier PPC block in a
6-block cycle: `0x50461a60 → 0x504a8e40 → 0x50463004 → 0x5046301c →
0x50463028 → 0x50488740 → (repeat)`. r4 gets **loaded** somewhere in
that cycle — not yet pinned down.

**68k source register is NOT A1** — dumped memory at A1=`0x5006df18`
and it contains 68k instruction bytes (`48 68 00 08 4e ba 01 2a…` =
PEA/BSR) not the vector-handler table. So some other register (likely
A2, A4, or a computed address off A6 frame pointer) holds the source
longword, which is then rotated to extract each byte.

**Identified artifacts:**
- `docs/ppc/late-6-artifacts/vector_corrupt_first_dump.txt` — full
  first-corruption context: registers, recent-block ring, PPC code dump,
  all 8 A-regs dereferenced, D-regs, and the 0x60-byte dst state.
- `docs/ppc/late-6-artifacts/byte_writes.txt` — all 80 individual byte
  writes with pc/lr/val/size/CR.

**Stability side-quest (done this session):**

`MACEMU_PPC_TICK_PERIOD_SCALE` default is now **10** for Unicorn PPC
(was 1). Reason: empirically the only rate where boot reaches the
vector-install phase. Explicit env-var still overrides. Edit in
`src/cpu/cpu_unicorn_ppc.cpp` around the `s_tick_scale` initializer.
Fixes the original task-2: UI launch no longer needs env-var dance.

**Next session targets (concrete):**

1. **Pin down where r4 is loaded.** Extend the PPC code-dump hook to
   also capture `0x50488740`, `0x504a8e40`, `0x50463004` contents. r4
   is the source-byte register; the block that does `lbz r4, ?(?)`
   or similar is the key. Once we know the source register holding the
   longword, we can confirm whether it's (a) wrong pointer, (b) wrong
   value from ROM, or (c) PPC TCG codegen bug in Unicorn.
2. **Set up a KPX-vs-Unicorn comparison at this exact moment.** With
   `MACEMU_PPC_TRACE=<file>` on both backends, find the EmulOp pair
   immediately preceding where Unicorn enters this 68k instruction —
   compare register state (esp. CR2, all data regs). Divergence there
   will pin the PPC codegen bug to a specific opcode.
3. **The 68k instruction emitting this is probably MOVE.B via the DBF
   tail-decode fast path.** 68k PC `0x500c61a6` opcode `0x51c8` is DBF;
   the PPC handler does "tail-chain" decode of both DBF and the
   preceding MOVE.B. Disassembling the ROM bytes at `0x500c619e..0x500c61a8`
   will identify the 68k loop structure.
4. **Alternative angle: the rotate-by-8 pattern is suspicious.** PPC's
   `stwbrx` instruction (store word byte-reverse) does byte-swap, not
   rotate-by-8. If Unicorn's TCG for PPC mis-emits a rotate somewhere in
   the MOVE.B handler (e.g., `rlwinm` with wrong mask), that would
   produce exactly this output. Check the QEMU m68k→ppc target translate
   for the specific PPC ops used in this handler — the `rlwimi r4,r4,24,0,0`
   at `0x50461a64` has MB=ME=0 which only touches bit 0; Unicorn may be
   interpreting the mask as wider than spec.

### 2026-04-19 late-7 — **byte-shift is PStrToCStr with A0 = vector table**

Using `MACEMU_PPC_DUMP_PC=0x504a8e40,...,0x50461a60` to dump the 6-block
cycle, the r4-load site is **`lbzx r4, r16, r27`** at `0x50488740` (a
dispatch table keyed by source An reg). With r27=1 (= d16 of MOVE.B
opcode), this loads byte @(A0+1); the store block writes it to (A0),
then A0++. So the cycle faithfully implements 68k `MOVE.B 1(A0),(A0)+`.

Reading the 68k code from the ROM (offset `0xc6198` in `g3.rom`):

```
0x500c6198:  2040         MOVEA.L D0, A0          ; A0 = caller-supplied ptr
0x500c619a:  7000         MOVEQ   #0, D0
0x500c619c:  1010         MOVE.B  (A0), D0        ; D0 = length byte
0x500c619e:  6004         BRA.S   $500c61a4       ; jump to DBF
0x500c61a0:  10e8 0001    MOVE.B  1(A0), (A0)+    ; shift-left body
0x500c61a4:  51c8 fffa    DBF     D0, $500c61a0   ; loop
0x500c61a8:  4210         CLR.B   (A0)            ; null-terminate
0x500c61aa:  4ed1         JMP     (A1)            ; return
```

**This is a Pascal-string → C-string converter.** It reads the length
from (A0), shifts the N data bytes left by one (dropping the length byte),
and null-terminates. It is correct, well-behaved 68k code.

**The bug is in the caller**: A0 = `0x00000008` (vector 2 of the exception
table) when this routine is invoked, and the byte @0x08 = `0x50` (the
MSB of the vec 2 handler `0x50003040`). The routine reads that 0x50 as
"string length = 80" and runs 80 shift iterations, corrupting
@0x08..@0x57. Vector 10 @0x28 = `0x50012af0` becomes `0x012af050` →
invalid addr → Type 10 bomb.

**All Unicorn's PPC execution is correct here.** `lbzx`, `stb`, `rlwimi`
at the cycle blocks are semantically right. There is **no TCG miscompile
of rotate-by-8** — late-6's byte-shift signature is the genuine output of
a faithful 68k shift-left loop, not an emulation artifact.

Why KPX boots fine: KPX never calls `0x500c6198` with A0=@0x08. The
Unicorn-vs-KPX divergence happens in an **earlier 68k instruction** that
sets up A0/D0 for the call. The search now moves one call-frame up.

#### Next-session targets (late-7+)

1. **Find the 68k caller of `0x500c6198`.** It's a BSR/JSR somewhere; its
   instruction-before-the-call computes A0 (the Pascal string pointer) and
   goes wrong on Unicorn. The ring before the cycle shows blocks
   `0x504b0020 0x50467ec0 0x50467ed4` immediately preceding `0x504a8e40`
   — these are the PPC handlers for the last few 68k instructions before
   the MOVE.B loop executes. Decode which 68k opcodes those correspond
   to and walk backward from the BSR site.
2. **KPX trace at matching 68k PC.** With both backends at the same 68k
   PC range (roughly `0x500c6198` ± a few dozen instructions back), run
   `MACEMU_PPC_TRACE=…` and diff. The first divergence in D0/A0/A1 or
   supervisor flags identifies the bad instruction.
3. **No further PPC-level instrumentation needed** — the issue is at the
   68k-code level now, not the PPC-handler level. Stop chasing TCG
   miscompiles; start chasing wrong 68k register values.

Artifacts: `docs/ppc/late-7-artifacts/PStrToCStr_discovery.md` (analysis),
`docs/ppc/late-7-artifacts/cycle_pc_dumps.txt` (full register dumps at
each of the 6 cycle blocks).

### 2026-04-19 late-7b — PStrToCStr callers decoded; primary repro is different

**Correction:** the routine entry is `0x500c6190`, not `0x500c6198`.
The late-7 note led with `0x500c6198` (the first instruction *after*
the stack pops), but the real entry 8 bytes earlier does pop-then-run:

```
0x500c6190:  225f         MOVEA.L (A7)+, A1     ; pop return addr
0x500c6192:  201f         MOVE.L  (A7)+, D0     ; pop string ptr
0x500c6194:  2e80         MOVE.L  D0, (A7)      ; store out-ptr to slot
0x500c6196:  6712         BEQ.B   $500c61aa     ; skip on null
0x500c6198:  2040         MOVEA.L D0, A0        ; A0 = str_ptr
...                                               (rest as before)
0x500c61aa:  4ed1         JMP     (A1)          ; return via A1
```

**Calling convention**: caller does `SUBQ.L #4,A7` (reserve out-slot),
pushes str-ptr, then `JSR` (pushes return addr). Routine pops both.

No direct branch in ROM targets `0x500c6190`. Instead there are two
trampolines at `0x50051080` and `0x5006e040`, each `BRA.L 0x500c6190`.
Three caller sites reach the routine via those trampolines:

- `0x5006df14`: `MOVEA.L -38(A6),A0; PEA 8(A0); JSR 0x5006e040` — pushes `*(A6-38) + 8`
- `0x50051040`: `MOVE.L A3,-(A7); JSR 0x50051080` — pushes `A3`
- `0x500517f0`: `PEA 8(A6); JSR 0x50051080` — pushes `A6 + 8`

Tracer (`MACEMU_PPC_TRACE_68K_ENTRY=0x500c6198`) added; captures every
entry to the routine. In legit calls, `D0 = A0_caller + 8 = 0x45548`
with `A0 = 0x45540`, matching caller-C's `PEA 8(A0)` pattern with
`A0 = 0x45540`. The A-trap A0=0x08 repro from late-7 would correspond
to caller-C with `*(A6-38) == 0` (then pushed value = 8).

**But this session's primary repro is a different corruption.** Across
6 parallel `SCALE=1` boots, 4 hit the corruption path; none matched
late-6/7's PPC PC `0x50461a74` / 68k PC `0x500c61a6` pattern. All 4
matched a *different* shape:

- Corrupting PPC handler PC: `0x50461e94` (`stb r4, 0(r22)`)
- 68k PC at corruption: `0x500ce210` range
- 68k opcode responsible: `0x1c80` at `0x500ce20c` — **`MOVE.B D0, (A6)`**
- Register state at first write: **A6 = 0**, **A7 = 0**, A0 = 1, A3 = 1
- Write lands at lomem `@0x00` (not `@0x08`), then `@0x400` via
  `MOVE.B D0, 0x400(A6)` at `0x500ce216`.

The 68k code at `0x500ce1e0..0x500ce248` treats A6 as a base pointer
to a buffer with meaningful offsets at `0`, `0x400`, `0x800`, `0x804`
— reads `2048(A6)` and compares against `0xe9`/`0xbc`/`0xbb`. Looks
like a color-table / CLUT / palette builder or similar 2KB-layout
data-structure initializer.

**Root-cause hypothesis (late-7b):** the original A-trap bomb and this
`@0x00` corruption are two symptoms of the same upstream A-register
divergence. Whichever routine runs first with bad A-regs produces the
visible corruption; in u_b10 it was PStrToCStr with A0=8, in this
session's repros it's the CLUT-builder with A6=A7=0.

Runs 2 and 3 (no corruption) reached "Loading boot blocks (resource
#96/#92)" — so the corruption is stochastic on timing, consistent
with upstream state being sensitive to IRQ pressure.

#### Next-session targets (late-7b+)

1. **Trace r24 = 0x500ce20c** (and ideally `0x500ce1e0`, routine entry
   guess) with the existing `MACEMU_PPC_TRACE_68K_ENTRY` tracer. In
   runs that corrupt, see A6/A7 values at entry and walk back the
   PPC ring to identify the preceding 68k caller. In successful
   runs, confirm A6 and A7 are non-zero at that PC.
2. **KPX boundary trace comparison.** Same KPX-vs-Unicorn diff
   approach as late-7, but anchored at 68k PC `0x500ce20c` instead
   of `0x500c6198`. The first EmulOp where A6 or A7 differ between
   backends is the divergence point.
3. **Stop chasing PStrToCStr specifically.** It was a symptom; the
   upstream A-reg divergence is the real bug and shows up
   equivalently at the CLUT-builder. Hunt the divergence.

Artifacts: `docs/ppc/late-7-artifacts/caller_analysis.md`,
`/tmp/u_hunt_{1..6}.log`, `/tmp/u_entry_s1.log` (all SCALE=1 repros
this session). Tracer code is in `src/cpu/cpu_unicorn_ppc.cpp` under
`MACEMU_PPC_TRACE_68K_ENTRY`.

### 2026-04-19 late-8 — CLUT-builder repro vanished; Unicorn boots cleanly

Tried to resume the late-7b divergence hunt. Current state of the
working tree (tracer patches from late-7b still uncommitted) **no
longer reproduces the corruption at all**.

20 parallel `SCALE=1` boots with `MACEMU_PPC_TRACE_68K_ENTRY=0x500ce1e0`
armed: **0/20 hits on `0x500ce1e0`**, **0/20 bail-watchdog fires**,
14/20 reach `Desktop ready` in ≤5 s and the remaining 6 are still
mid-DiskPrime at the timeout (all past Finder). A single-target run
with `0x500ce1e0,0x500c7a4a,0x5000ecfe` armed confirms the wild
execution path is unreached: `0x500c7a4a` fires 0 times while
`0x5000ecfe` (the benign `JMP (A6)` via the BCLR dispatch path) fires
3 times with `A6 = 0x500000f6` (boot dispatcher). The late-7b
ring signature `... 5000ecbc → 500c7a4a → ... → 500ce1e0` is a
pure divergent-run artifact, not part of the normal boot trail.

**Hypothesis for the stabilisation:** no code change since late-7b
(only a docs commit `d08cfc65` + uncommitted tracer patches). Most
likely tick-phase/IRQ-cadence is happening to land in a survivable
window under current machine load. Reproduction was already described
as stochastic (4/6 → 0/20 is within the confidence interval when the
true rate drifts with wall-clock jitter).

Kept the tracer infrastructure in tree: `MACEMU_PPC_TRACE_68K_ENTRY=...`
(Unicorn side + KPX side), 128-slot `g_uppc_last_r24s` ring, JIT
auto-disable when TRACE_68K_ENTRY is set. All gated — zero overhead
when unset. Re-arm if the bug resurfaces; a single corrupting run
with the tracer active will capture A6/A7 at the divergence point.

**Next-session priorities** (in order):
1. **Verify stability lasts.** Re-run the 20× SCALE=1 batch in a
   fresh shell / different load; if corruption stays at 0/N, declare
   the CLUT repro dormant and shift focus.
2. **Pick up deferred work** from the "Deferred work" section below
   (GET_RESOURCE family selectors is the leading candidate — the pure
   dispatcher stubs them out and there's no visible impact, but they
   may matter once more complex 68k apps run).
3. **Config plumbing for `ppc_tick_scale`** (originally late-5's #2).
   UI launch still needs the `MACEMU_PPC_TICK_PERIOD_SCALE` env var
   to opt into SCALE=1; surfacing it as a config field would let
   future stochastic repros be toggled without restarting.

Artifacts: `/tmp/big_{1..20}.log` (parallel batch), `/tmp/u_dflt.log`
(single-target sanity).

### 2026-04-19 late-8b — baseline reproduction matrix

Re-establish what the current tree actually does before touching
timing code. 5 rows × N=20 parallel boots each, classified by log
greps (`Desktop ready` > `Finder detected` > bail-watchdog > bootblk >
driver/warm-start > early > preboot > crash). Script:
`/tmp/baseline/matrix.sh`.

| row           | scale | tmout | desktop | finder | bootblk | drivers | early | preboot | bail | crash | time-to-Desktop (mean/min/max) |
|---------------|-------|-------|---------|--------|---------|---------|-------|---------|------|-------|-------------------------------|
| `kpx_s1`      | 1     | 10 s  | **20**  | 0      | 0       | 0       | 0     | 0       | 0    | 0     | 1.08s / 0.89s / 1.21s         |
| `unicorn_s1`  | 1     | 10 s  | **20**  | 0      | 0       | 0       | 0     | 0       | 0    | 0     | 1.13s / 1.01s / 1.28s         |
| `unicorn_s5`  | 5     | 14 s  | **20**  | 0      | 0       | 0       | 0     | 0       | 0    | 0     | 1.11s / 0.76s / 1.37s         |
| `unicorn_s10` | 10    | 22 s  | **20**  | 0      | 0       | 0       | 0     | 0       | 0    | 0     | 1.28s / 1.09s / 1.36s         |
| `unicorn_s30` | 30    | 60 s  | **20**  | 0      | 0       | 0       | 0     | 0       | 0    | 0     | 1.10s / 0.88s / 1.29s         |

**Headlines:**

- **100/100 runs reach Desktop.** SCALE=1 is no longer the plateau it
  was in late-5 (stuck in preboot) or the corruption source it was in
  late-7b (4/6 wild-execution). The late-8 "stabilisation" holds across
  a bigger sample and across all tested scales.
- **Time-to-Desktop is SCALE-insensitive in this window.** The reported
  `[Boot +N.NNs]` is Mac-internal elapsed, not host wall-clock, so
  SCALE mostly affects *how much host time each Mac tick buys*, not
  how many Mac ticks it takes to reach Desktop. All rows land in
  1.08–1.28 s (overall min 0.76 s, max 1.37 s).
- **SCALE=10 is not faster than SCALE=1 here.** Earlier sessions
  picked SCALE=10 as default because SCALE=1 *stalled*; with the stall
  gone, SCALE=1 is as fast as anything. Default should stay at 10 for
  headroom (cheap insurance against future stochastic regressions).

**Shutdown-path SIGSEGV (orthogonal, tracked separately):** 100/100
runs on every row exit with shell-visible `Segmentation fault` at
reap time. KPX runs additionally log a `SIGSEGV pc 0x50580000` line
in-process *after* `Desktop ready`; Unicorn runs don't log the signal
but the shell still reports it — likely the handler stack-walks before
stdout is flushed, or the crash is on a thread whose output isn't
captured. Same symptom on both backends → shutdown/timeout-kill path,
not a PPC-port issue. Not blocking boot correctness. Logged for later
cleanup.

**Next:** now that baseline is honest (100% boot), attack the timing
root cause from late-5 — cheapest first. Leading candidate is
defer-first-IRQ (skip Unicorn's tick thread until `g_emulop_count`
reaches KPX's ≥105 threshold before letting the first 60 Hz tick
through). Re-run this matrix after each attempt; success criterion
is "Unicorn SCALE=1 desktop=20/20 *and* mean time-to-Desktop
approaches `kpx_s1`'s 1.08 s."

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

## 2026-04-20 session (late-8e) — perf flame graph: softmmu, not TCG, is the tax

TL;DR — `perf record -F 99 --call-graph dwarf` on a 30-s `--backend unicorn
--arch ppc --timeout 30` run (PPC ROM `/home/mick/g3.rom`, disk
`macos-7.6.1.img`) confirms Unicorn's **softmmu translation chain is ~40 %
of wall time** — this is expected architectural overhead of a TCG JIT
with hooks vs. KPX's direct PPC-on-PPC interpretation and is not a bug.
The profile also rules out two earlier theories — `uc_emu_stop` IRQ kick
costs only ~2 % and there is no malloc in the hot path (see correction
below). Debug hooks profile at ~5–7 % inclusive, but post-gating
measurement (late-8f) showed no measurable wall-time speedup — so the
hook-gating landed in late-8f is hygiene, not an optimization.

### Setup

```bash
echo 1 | sudo tee /proc/sys/kernel/perf_event_paranoid   # once
git clone --depth=1 https://github.com/brendangregg/FlameGraph ~/FlameGraph
/tmp/perf-run.sh s1  1  30 99
/tmp/perf-run.sh s10 10 30 99
```

Runner at `/tmp/perf-run.sh` does `perf record -F99 --call-graph dwarf,8192`,
then `perf script | stackcollapse-perf.pl | flamegraph.pl`. Output in
`docs/ppc/late-8-artifacts/perf/` (flame SVGs + .folded stacks + .data).

### Inclusive % by function (SCALE=1, 7531 samples, 30 s)

| %      | Function                          | What it is |
|-------:|-----------------------------------|------------|
| 25.6 % | `load_helper`                     | Softmmu memory-load helper (inclusive — includes everything it calls) |
|  9.1 % | `[unknown]`                       | Unsymbolized — overwhelmingly JIT-compiled TCG guest code |
|  8.3 % | `find_memory_mapping_ppc`         | Unicorn-side MMU translation |
|  7.5 % | `helper_lookup_tb_ptr_ppc`        | TB-pointer lookup (called per basic block) |
|  6.8 % | `flatview_translate_ppc`          | QEMU flatview translate |
|  6.7 % | `address_space_translate_internal`| QEMU as_translate |
|  5.4 % | `tb_lookup__cpu_state`            | TB cache lookup |
|  5.0 % | `helper_be_lduw_mmu_ppc`          | 16-bit big-endian load helper |
|  ~~4.5 %~~ | ~~`_int_malloc`~~             | ~~libc malloc — reachable from the softmmu path~~ **false positive, see correction below** |
|  4.1 % | `full_be_lduw_mmu`                | |
|  3.9 % | `address_space_translate`         | |
|  3.9 % | `flatview_do_translate`           | |
|  3.0 % | `operator` (lambda call op)       | Our UC_HOOK lambdas (most of it) |
|  2.3 % | `uppc_cpu_init::{lambda`          | Our hooks (direct) |
|  2.2 % | `_FUN` (lambda→C thunk)           | |
|  1.9 % | `store_helper`                    | |
|  1.6 % | `helper_check_exit_request_ppc`   | Per-TB "should I exit?" check (the `uc_emu_stop` kick path) |

SCALE=10 is within a percentage point on every row except our hooks,
which rise to **5.3 % `uppc_cpu_init::{lambda` + 4.0 % `operator`** —
more hook hits per IRQ slot when the IRQ rate is 6 Hz instead of 60 Hz
crowds out the `uc_emu_stop` path. `helper_check_exit_request_ppc` rises
to 2.0 %.

### Findings

1. **Softmmu translation is the tax, not TCG dispatch — and this is
   expected.** The `load_helper → find_memory_mapping_ppc →
   address_space_translate → flatview_translate_ppc →
   flatview_do_translate → …` chain runs on every guest load/store,
   and the inclusive cost of just `load_helper` plus what it calls is
   ~40 % of wall time. The TCG-generated guest code itself (the
   `[unknown]` bucket — perf can't symbolize JIT buffers) is only ~9–11
   %. This is the architectural cost of choosing a TCG JIT + softmmu +
   hook-dispatch backend over KPX's direct PPC-on-PPC interpreter; it
   is not a "bug" to chase. The real inflection point is that Unicorn
   calls `uc->memory_mapping(uc, paddr)` **on every memory access, even
   on TLB hit** (`subprojects/unicorn/qemu/accel/tcg/cputlb.c:1556`) —
   so the translate-chain cost scales with *access rate*, not miss
   rate. Future upstream work could cache MR pointers in TLB entries,
   but that's an optimization in Unicorn, not our layer.

2. ~~**`_int_malloc` appears in 4.5 % of samples**~~ **— correction: false positive.**
   An LD_PRELOAD malloc-counter (`/tmp/libmalloc_count.so` — wraps
   `malloc/calloc/realloc`, buckets by `__builtin_return_address`) run
   across a 15 s SCALE=1 boot recorded **33 total mallocs**, none at
   runtime rates. The `_int_malloc` frames in the perf flame graph are
   DWARF-unwinder phantoms: when perf takes a sample inside TCG-JIT'd
   code (no DWARF info), its unwinder walks the saved stack dump
   heuristically and sometimes latches onto stale PCs that resolve to
   libc symbols. Confirmed: the shim works on a trivial test (1000/1000
   mallocs captured), and the ldd output shows mac-phoenix links libc
   dynamically, so the shim intercepts correctly. **Unicorn's softmmu
   path does not malloc at runtime.** The real softmmu cost is the
   translate-chain work itself, not heap allocation.

3. **`uc_emu_stop` on IRQ is cheap — the late-8d theory is wrong.**
   `helper_check_exit_request_ppc` (the per-TB epilogue that observes
   `uc_emu_stop` flags) is only 1.6–2.0 % inclusive. Switching from
   `uc_emu_stop` to cooperative polling at the next EmulOp would reclaim
   at most ~2 % — not worth the plumbing.

4. **Our debug instrumentation costs ~5–7 %.** Lambdas from
   `uppc_cpu_init` are the mem-write hooks on 0x00000000 (lowmem WP)
   and 0x2818 (nest tracer added in late-8d) plus the per-block /
   per-code hooks. Removing these post-debugging would give back single-
   digit percent.

5. **TB lookup is expensive per block (~13 % total).**
   `helper_lookup_tb_ptr_ppc` + `tb_lookup__cpu_state` + `tb_jmp_cache_hash_func`
   sum to ~13 %. This is the TCG dispatcher hashing TB addresses on
   every basic-block boundary. A direct-jump threaded-TB path (chain
   TBs without going through the lookup) could cut this.

### Theories for next session, ordered by expected payoff

1. **Kill the per-access `find_memory_mapping` call in cputlb.c.**
   `subprojects/unicorn/qemu/accel/tcg/cputlb.c:1556` runs
   `mr = uc->memory_mapping(uc, paddr)` — a full `address_space_translate`
   — on **every memory access, even on TLB hit.** Unicorn needs the
   `MemoryRegion*` for UC_HOOK_MEM_* callbacks and for `mr->perms`
   checks. But once per page we could cache the MR pointer in the TLB
   entry's unused slot (or a parallel array keyed by tlb index) and
   reuse it until `tlb_fill` runs. If we have no MEM hooks installed
   for a given range, we can skip the lookup entirely. Expected win:
   the ~40 % softmmu-chain inclusive cost collapses to just the actual
   TLB-hit fast-path plus hook dispatch.

2. **Remove debug hooks.** The nest tracer (late-8d) and the lowmem WP
   have served their purpose. Gating them behind env vars that default
   off is a ~5–7 % win and removes noise from future profiles. **Done in
   late-8f — see below.**

3. **Widen the Unicorn TLB.** Default PPC softmmu TLB is 256 entries (×4
   MMU index classes). Classic-Mac guests touch lots of pages (low RAM,
   ROM, framebuffer, ScratchMem). Growing `CPU_TLB_BITS` in the forked
   Unicorn would reduce `tlb_fill` miss rate — which is also what drives
   the `find_memory_mapping` slowpath (see theory #1).

4. **Compare to KPX.** Run the same perf recipe on `--backend kpx` for
   a minute and compare. KPX boots to Desktop in ~3 s, so it'll finish
   boot inside the window — we can see where a fast PPC interpreter
   actually spends its time, as a bound on what Unicorn TCG could aspire
   to.

### Artifacts (`docs/ppc/late-8-artifacts/perf/`)

- `flame_s1.svg`, `perf_s1.folded`, `run_s1.log` — SCALE=1 (30 s, 99 Hz)
- `flame_s10.svg`, `perf_s10.folded`, `run_s10.log` — SCALE=10 (30 s, 99 Hz)
- `flame_s1_post.svg`, `perf_s1_post.folded`, `run_s1_post.log` — post-gating (late-8f, 25 s, 99 Hz)

`.data` files (~60 MB each) are perf-internal and should not be committed.

### 2026-04-20 late-8f — debug hook gating

Landed env-var gating for the four previously-unconditional debug hooks
in `src/cpu/cpu_unicorn_ppc.cpp`. All default to OFF except
`hook_last_pc` which is default ON (cheap, feeds crash forensics).

| Hook | Gate | Default | What it does |
|---|---|---|---|
| `hook_last_pc` (UC_HOOK_BLOCK, fires every TB) | `MACEMU_PPC_NO_BLOCK_TRACE=1` disables | **ON** | Records last 32 guest PCs into `g_uppc_last_block_pcs` ring; crash handler reads this |
| `hook_wp` (UC_HOOK_MEM_WRITE on ROM zero-pad 0x50400000..0x50500000) | `MACEMU_PPC_TRACE_ROMZ=1` | OFF | Logs writes into the ROM zero-pad region (nanokernel populating code?) |
| `hook_lowmem` (UC_HOOK_MEM_WRITE on 0..0x10 or 0..0x400) | `MACEMU_PPC_TRACE_LOWMEM=1` (narrow) or `MACEMU_PPC_VECTOR_TRACE=1` (wide) | OFF | Logs writes to the 68k exception vector table; smoking-gun for A-trap/vector corruption |
| `hook_nest` (UC_HOOK_MEM_WRITE on XLM_IRQ_NEST=0x2818) | `MACEMU_PPC_TRACE_NEST=1` | OFF | late-8d nest-cell tracer |

Verification:
- Boot smoke test (25 s, no env vars): clean, hits `DiskPrime #100`, no regression.
- Boot smoke test with all three opt-in vars set: all three banner lines
  print, `[LOWMEM-WRITE]` events fire as before.

**Perf reclaim measurement is inconclusive.** Running `perf record -F99
--call-graph dwarf,8192` post-gating (`flame_s1_post.svg`) gives **250
samples/sec vs 251 samples/sec** in the s1 baseline — no measurable
wall-time speedup. The `load_helper`/`find_memory_mapping_ppc` %s drop
dramatically in the post profile (25.6 % → 3.2 % each) but this is
**DWARF-unwinder artifact**, not a real reduction: 61 % of post-gating
samples land as bare `mac-phoenix` with no unwound stack (vs. well-
formed 10–20-frame stacks in the baseline). The folded-stack diversity
dropped from 440 to 226, consistent with more samples hitting JIT code
where DWARF can't walk through TCG-generated frames. Same work done,
just worse unwind coverage.

Takeaway: the hook gating is hygiene — removes unconditional debug
instrumentation from steady-state runs, simplifies the default perf
profile — not a perf optimization. The real perf levers remain theories
#1 (cached MR pointer in TLB) and #3 (wider TLB).

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

Updated 2026-04-20 post-prune. Removed as of late-9: `MACEMU_PPC_TRACE_DISP`,
`MACEMU_PPC_TRACE_EMULOP`, `MACEMU_PPC_TRACE_ROMZ`, `MACEMU_PPC_TRACE_LOWMEM`,
`MACEMU_PPC_VECTOR_TRACE`, `MACEMU_PPC_TRACE_NEST`, `MACEMU_PPC_TRACE_TICK`,
`MACEMU_PPC_TRACE_LOOP`, `MACEMU_PPC_TRACE_MACOS`, `MACEMU_PPC_BCTRL_WATCH`,
`MACEMU_PPC_R1ZERO`, `MACEMU_PPC_TRACE_TWI`, `MACEMU_PPC_DUMP_PC`. Their
supporting globals, hooks, and ring buffers were deleted. If you need a
specific one back, git log is the reference — don't re-scaffold for the
same symptom twice.

### Behavioral knobs (change runtime timing/IRQ shape)

| Var | Effect |
|---|---|
| `MACEMU_PPC_TICK_PERIOD_SCALE=N` | Unicorn: multiply the 16.625 ms tick period. `10` → 6 Hz. **Default 10** for Unicorn PPC (set in `uppc_tick_thread`); explicit env var overrides. |
| `MACEMU_PPC_NO_IRQ=1` | Both backends: mask 60 Hz timer IRQ (isolates deterministic path — used by the boundary comparator). |
| `MACEMU_PPC_MIN_EMULOPS_PER_IRQ=N` | Unicorn: suppress tick IRQ unless ≥N emulops elapsed since last one. Default 0 = off. Experimental IRQ-pressure gate; late-8c showed it doesn't rescue SCALE=1 boot alone. |
| `MACEMU_PPC_DEFER_FIRST_IRQ=N` | Unicorn: suppress tick IRQs until `g_emulop_count ≥ N`. Default 0 = off. Negative result (late-8c) — keep as evidence. |
| `MACEMU_PPC_NO_BLOCK_TRACE=1` | Unicorn: disable the always-on `hook_last_pc` UC_HOOK_BLOCK that feeds `g_uppc_last_block_pcs` (crash handler reads this ring). Default off = hook installed. Set only for clean perf runs. |

### Tracers (all default off; ×1 in stderr unless otherwise noted)

| Var | Effect |
|---|---|
| `MACEMU_PPC_TRACE=<path>` | Both: write per-EmulOp boundary-state line pairs (EMULOP + POST) to `<path>`. Driver of the KPX-vs-Unicorn diff workflow. |
| `MACEMU_PPC_CR2_TRACE=<lo>[:<hi>]` | Both: per-instruction `[CR]` lines on stderr for EmulOp seq in `[lo, hi)`. Auto-disables KPX JIT. |
| `MACEMU_PPC_TRACE_TRAP=1` | Unicorn: log each EXEC_NATIVE dispatch. |
| `MACEMU_PPC_TRACE_IRQ=1` | Unicorn: log every `g_pending_irq` 0→1 / 1→0 edge in `uppc_handle_interrupt`. |
| `MACEMU_PPC_TRACE_68K_ENTRY=<hex>[,<hex>...]` | Both: dump register context and recent-68k-PC ring when one of the listed 68k PCs (`r24` on Unicorn) is executed. Auto-disables KPX JIT (JIT bypasses the hook). |
| `MACEMU_PPC_TRACE_68K_MAX=N` | Both: max hits per target for `MACEMU_PPC_TRACE_68K_ENTRY` before suppression (default 5). |

### 2026-04-19 late-8c — R1ZERO false-positive + defer-first-IRQ negative result

Session objective: prove/disprove the IRQ-during-handler hypothesis for the
webserver-stall R1ZERO at pc=0x5046fa0c (prev_r1=0x05ff8048).

**Tracer built**: extended the `r1_lo_cb` block hook with a shared
`g_last_twi_*` state, added a per-instruction TWI tracer covering the
handler critical section (0x5046e700..0x5046e847) plus the R1ZERO site
(0x5046f9f0..0x5046fa20), and a global IRQ-edge watcher that logs every
`g_pending_irq` 0→1 and 1→0 transition with a 64-slot ring of recent PCs
while the flag is pending. All env-gated (`MACEMU_PPC_TRACE_TWI=1`).

**Hypothesis disproved — R1ZERO is a false positive.** The instructions at
0x5046fa00..0x5046fa10 decode to:

```
0x5046fa00: mtctr r1                ; save old r1 → CTR
0x5046fa04: lwz   r1, 0x2818(0)     ; r1 = mem[0x2818]   (counter, ~0)
0x5046fa08: addi  r1, r1, 1         ; r1++               (transient: 0 or 1)
0x5046fa0c: stw   r1, 0x2818(0)     ; mem[0x2818] = r1
0x5046fa10: lwz   r1, 0x2804(0)     ; r1 = mem[0x2804]   (restores stack)
```

This is SheepShaver's per-68k-instruction counter. r1 is transiently 0 or 1
between the load and the restore at 0x5046fa10 — the R1ZERO detector
(threshold `r1 < 0x100`) always fires here. Both historical sites
(pc=0x504a77d0 prev=0x68ffffac and pc=0x5046fa08/0c prev=0x05ff8048) are
the same counter path caught at different block boundaries. **The
detector is noise; r1 is not actually corrupted.** Saved as memory
`feedback_ppc_r1zero_false_positive.md`.

**IRQ preemption is benign.** The IRQ-edge watcher captured 200+ edges
during a 90-second webserver run. Top PCs where IRQ raised: the counter
site 0x5046fa0c and the handler return path 0x5046e0xx. Every
`[IRQ-] ... cleared after 0 emulops` — the flag flips 0→1→0 between
EmulOp dispatches. This is cooperative polling, not preemption. irq=0
on every [TWI] event captured in the handler body.

**Defer-first-IRQ did not improve success rate.** Added env knob
`MACEMU_PPC_DEFER_FIRST_IRQ=N`: suppress tick IRQs until
`g_emulop_count >= N`. At SCALE=10:

| config                              | desktop/3 |
| ----------------------------------- | --------- |
| baseline (defer=0)                  | 1/3 (5.44s) |
| MACEMU_PPC_DEFER_FIRST_IRQ=105      | 0/3       |
| MACEMU_PPC_DEFER_FIRST_IRQ=500      | 0/3       |
| MACEMU_PPC_DEFER_FIRST_IRQ=2000     | 0/3       |

Deferring the first IRQ does not help and slightly hurts. Knob kept in
tree (env-gated, off by default) as negative evidence.

**Baseline regression detected.** The late-8b claim "unicorn_s1 = 20/20
Desktop" does not reproduce on current HEAD — N=10 runs of the exact
matrix.sh command give 0/10 Desktop, all "progress stall: no EmulOp
advance for 5s (301 iters, 301 IRQs delivered)". The disk images
`macos-7.6.1.img`, `data.img`, `installers.img` have timestamps inside
this session — earlier crashed runs likely left the guest disk in a
state that no longer boots cleanly. Need to restore images from a
known-good snapshot before re-running the matrix. Not done this session.

**Next-session targets:**
1. Recover or regenerate `macos-7.6.1.img` to a clean known-good state;
   re-run `matrix.sh` to confirm unicorn_s1 is genuinely 100% again.
2. Either remove the R1ZERO detector or gate it so it doesn't fire at
   PCs 0x5046fa08/0c — current behavior is pure noise and was the entire
   chase this session.
3. If baseline is restored, the real-world perf question reopens:
   unicorn mean time-to-Desktop (1.28s) vs KPX (1.08s). But not a
   correctness issue.

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

---

### 2026-04-19 late-8d — baseline clarified: "20/20 unicorn" was a measurement artifact

**Bombshell:** every previous "baseline matrix" result attributed to the
Unicorn backend was actually KPX running under a misparsed CLI flag.
`matrix.sh` invoked the binary with `--backend=unicorn`/`--backend=kpx`
(equals form). The parser in `src/config/emulator_config.cpp:496` only
matches the space-separated `--backend unicorn` form and prints
`[Config] Unknown argument: --backend=unicorn`, falling through to the
UAE default. Then the PPC auto-promotion at line 203 converts UAE →
KPX for arch=ppc. Net effect: **both rows of every historical matrix
were KPX runs.** The "20/20 desktop at unicorn_s1" claim from late-8b
was really "20/20 desktop at kpx_s1 × 2."

Fixed `/tmp/baseline/matrix.sh` to use space-separated args. Re-ran
with proper arg handling and `MACEMU_PPC_TRACE_R1ZERO` gating in
place (see below). Real baseline at SCALE=1,10,30 and the KPX
control:

| Row | Desktop | Finder | bootblk | bail | crash |
|--|--|--|--|--|--|
| real_k1  (KPX SCALE=1)     | 9 | 0 | 1 | 0 | 0 |
| real_u1  (Unicorn SCALE=1) | 0 | 0 | 1 | 9 | 0 |
| real_u10 (Unicorn SCALE=10)| 3 | 0 | 5 | 2 | 0 |
| real_u30 (Unicorn SCALE=30)| 6 | 1 | 1 | 2 | 0 |

(N=10 per row; bail = "progress stall: no EmulOp advance for Ns".)

So the **real** baseline is: Unicorn cannot boot reliably at 60 Hz ticks
(SCALE=1) — it bails on progress stalls 9/10 times. It needs SCALE≥10
to get any Desktop boots, and even at SCALE=30 (2 Hz effective tick)
only 6/10 reach Desktop within the 60-s budget. KPX at 60 Hz is
9/10 clean. The Unicorn port has an intrinsic IRQ-pressure
sensitivity that was never actually fixed — it was masked by the
arg-parsing bug making every "Unicorn" test really be KPX.

**R1ZERO detector gated off by default.** The always-on `hook_r1`
(`UC_HOOK_BLOCK` global at `src/cpu/cpu_unicorn_ppc.cpp:1069`) did a
`uc_reg_read(UC_PPC_REG_1)` on every single PPC block. With the
false-positive discovery from late-8c, this was pure overhead — now
gated behind `MACEMU_PPC_TRACE_R1ZERO=1`. Removing it did **not**
recover SCALE=1 boot success (still 0/10), so it wasn't load-bearing
for the regression; just noise.

**Underlying signal confirmed from status doc's earlier note:**
"At default 60 Hz, Unicorn's first IRQ lands at seq=9 vs KPX's seq=105 —
~30× IRQ pressure relative to EmulOp throughput." That 30× ratio is
the real story. Unicorn's TCG path dispatches EmulOps ~30× slower
than KPX's direct interpreter, so at a fixed 60 Hz wall-clock tick,
Unicorn eats one IRQ per ~9 EmulOps vs KPX's one-per-105. Some small
fraction of those mid-dispatch IRQs land in unsafe windows and
either corrupt state or fail to progress.

**Corrupting addresses seen in real_u1 logs:**
- `UNMAPPED READ pc=0x50490088 target=0x00ffff8cb0` (wrapped negative offset)
- `UNMAPPED READ pc=0x50465fb0 target=0x00ffffxxxx` (various high addrs)
- `UNMAPPED WRITE pc=0x504610b4 target=0x00ffffffff`
- `SIGSEGV at 0x502f5152` and `0x500c89a0` (spray of ROM PCs suggesting wild-jump)

These all smell like register corruption — a base register ends up
with a sign-extended/wrapped value, causing the next memory op to
target 4 GB minus a small offset. Classic symptom of an IRQ landing
during a sequence where the handler expected atomicity.

**Next-session targets:**
1. **Focus on SCALE=10 baseline first.** Getting 3/10 → 20/20 at
   SCALE=10 is a smaller hill than fixing SCALE=1. The remaining 7/10
   at SCALE=10 are a mix of bootblk-stuck (5) and bail (2).
2. **Examine `uppc_handle_interrupt()`** in `src/cpu/cpu_unicorn_ppc.cpp`
   — it's called between `uc_emu_start` returns (cooperative at block
   boundaries per the comment at line 2322). Verify it correctly
   saves/restores the PPC register set around the nanokernel IRQ
   entry. If SheepShaver's trap-dispatch handler (`0x5046e780`) is
   mid-flight when IRQ arrives, the restore must not clobber r1/r24.
3. ~~**Try `MACEMU_PPC_MIN_EMULOPS_PER_IRQ=100`** at SCALE=1~~ — tested
   in this session at gate=100/500/2000. All three gave 0/10 Desktop
   at SCALE=1 (bail=10/10/9). Logs show `0 IRQs delivered, 0 skips`:
   the gate successfully blocks all IRQs, but the guest then stalls
   anyway in PPC-only busy-waits waiting for the timer it's not
   getting. So the knob is either too aggressive or something else
   prevents IRQ delivery even when the gate allows it — the `nest == 0`
   check at line 1958 may be false-forever in these runs.
4. **Don't trust `matrix.sh`-style scripts with `--backend=X`**.
   Fixed in place; stays fixed.
5. **Instrument XLM_IRQ_NEST**: at SCALE=1, log every nest transition
   (0→non-zero, non-zero→0) with pc, emulops. If nest gets stuck >0,
   tick thread will never fire IRQs and we've found the wedge.
   Conversely if nest is 0 and tick fires but IRQ still doesn't
   progress, problem is downstream in `uppc_handle_interrupt`.

### 2026-04-20 late-9 — disk restore + debug prune; post-Finder crash is the new wall

**Disk-image regression resolved.** The late-8c/d "baseline regression"
was `/home/mick/storage/images/macos-7.6.1.img` going corrupt (earlier
crashed runs left the guest filesystem in a non-bootable state). User
restored from `.img.bak` (Apr 11 backup). Post-restore:
- KPX on 7.6.1: Desktop renders + BridgeAgent heartbeats in ~1.6 s. Clean.
- KPX on 7.5.5: Desktop renders + graceful shutdown via event bridge. Clean.

**Unicorn status, manual UI test (user, 2026-04-20):**
- 7.6.1: headless log claims `Finder detected` at +13.96 s and `Desktop
  ready` at +14.35 s, but the framebuffer shows the hourglass cursor and
  the guest **crashes just before Desktop paints**. Headless exit 0 at
  30 s timeout, but the 30-s run covers the crash and the crash handler's
  KPX-style skip masks it from an exit-code check. Only one SIGSEGV
  logged in the run: `#1 fault at 0x5009d288 host_pc=0x7854efa5`
  (nanokernel territory, TCG-compiled code — same address as pre-restore
  tests). Flight recorder dumped to `/tmp/mp_sigsegv_trace.log`.
- 7.5.5: hard crash earlier in boot than 7.6.1. Use 7.6.1 as the
  Unicorn triage disk from here on.

**New lesson saved:** the boot_phase tracker flipping to `desktop`
is not proof Finder painted — see
`feedback_ppc_desktop_phase_vs_visual.md` in auto-memory. Always
visually confirm (or poll `/api/screenshot`) before declaring end-to-end
Unicorn boot success.

**Residual post-Desktop unmapped-read loop** (headless run only): 16
hits at `pc=0x50491348 target=0x0021cf0ca4 size=4` fire right after the
Desktop-phase flag flips, then stop. `0x0021cf0ca4` is within the 128 MB
RAM window (`0..0x08000000`) but Unicorn reports it unmapped — worth
investigating as a separate RAM-mapping gap once the pre-Desktop hang is
understood.

#### Debug scaffolding pruned

Removed from `src/cpu/cpu_unicorn_ppc.cpp` and
`src/common/crash_handler_init.cpp` in this session:

- **`MACEMU_PPC_TRACE_DISP`** — full dispatcher-entry dump (~133 lines)
- **`MACEMU_PPC_TRACE_EMULOP`** — per-EmulOp noisy log (~10 lines)
- **`MACEMU_PPC_TRACE_ROMZ`** — ROM zero-pad write watchpoint (~40 lines)
- **`MACEMU_PPC_TRACE_LOWMEM` + `MACEMU_PPC_VECTOR_TRACE`** — 68k vector
  write tracers (late-6 investigation; we already identified the byte-
  shift bug and this is now noise)
- **`MACEMU_PPC_TRACE_NEST`** + `UppcNestTrans` ring + `uppc_nest_record`
  (late-8d XLM_IRQ_NEST tracer; ~60 lines total including globals and
  ring-dump in progress-stall watchdog)
- **`MACEMU_PPC_BCTRL_WATCH`** + bctrl globals + bctrl ring print in
  crash handler (~30 lines)
- **R1ZERO** block hook and ring-dump (false positive — see
  `feedback_ppc_r1zero_false_positive.md`)
- **TWI per-instruction tracer** (late-8c)
- **`MACEMU_PPC_DUMP_PC`** multi-address code-dump hook (late-7
  investigation; one-shot purpose served)
- **`MACEMU_PPC_TRACE_TICK` / `TRACE_LOOP` / `TRACE_MACOS`** — small
  always-gated-off progress loggers

**Why prune:** this scaffolding had served its purpose (byte-shift
identified, r1-zero false positive identified, nest-cell behavior
characterized) and was bloating the file to ~2400 lines. The
still-useful gates — `NO_IRQ`, `MIN_EMULOPS_PER_IRQ`, `DEFER_FIRST_IRQ`,
`TICK_PERIOD_SCALE`, `NO_BLOCK_TRACE`, `TRACE_TRAP`, `TRACE_IRQ`,
`TRACE_68K_ENTRY` and its `g_uppc_last_r24s` r24 ring, `TRACE`, and
`CR2_TRACE` — are retained. The progress-stall watchdog, hot-skip bail,
block-window stall bail, and the `hook_last_pc` last-block ring
(feeding the crash handler) are also retained as part of the core
backend, not env-gated.

Post-prune: build clean, KPX smoke test Desktop+BridgeAgent at 1.6 s,
Unicorn headless smoke test hits Finder and then the post-Desktop
unmapped-read loop described above.

#### Next-session targets

1. **Capture what's actually executing when Unicorn hangs/crashes
   between Finder-detect and visible Desktop.** The `hook_last_pc`
   ring + `MACEMU_PPC_TRACE_68K_ENTRY` are still in place; arm
   `TRACE_68K_ENTRY` on the guest PCs seen right before the SIGSEGV
   at `0x5009d288` and confirm whether the same 68k routine is
   always the trigger. Also inspect `/tmp/mp_sigsegv_trace.log`
   from a failing run — the flight recorder captures recent block
   PCs at fault time.
2. **Investigate `0x0021cf0ca4` mapping gap.** Address is inside
   128 MB RAM yet `UNMAPPED READ` fires. Either Unicorn's RAM
   `uc_mem_map` doesn't cover that range, or the guest is generating
   a physical address that happens to look in-range but is actually
   an unmapped MMIO/device region in real-Mac semantics that we
   haven't stubbed. Check the `uc_mem_map` call in
   `cpu_unicorn_ppc.cpp` against `RAMBase / RAMSize` and compare to
   what `find_memory_mapping_ppc` returns at the faulting address.
3. **Stop trusting headless `Desktop ready`.** Add a step to the
   boot smoke test that either (a) inspects `/api/screenshot` for
   non-hourglass pixels, or (b) uses the bridge to read back a
   Finder sentinel (e.g. a known desktop icon's position). A
   one-line curl of `/api/screenshot` with a minimum-non-black
   check would suffice as a CI gate.
