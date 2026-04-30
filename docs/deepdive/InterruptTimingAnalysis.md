# Interrupt timing — why UAE and Unicorn diverge

UAE and Unicorn-m68k will always reach different register state at the
same instruction count under any real workload. This is expected. The
short answer is: timer interrupts are wall-clock based, the two
backends execute at different speeds, so the timer fires at different
instruction counts in each.

## The interrupt that breaks lockstep

`INTFLAG_TIMER`, fired by the Time Manager thread when its `wakeup_time`
elapses (`src/core/timer.cpp`):

```c
clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &wakeup_time, NULL);
SetInterruptFlag(INTFLAG_TIMER);
g_platform.cpu_trigger_interrupt(intlev());     /* level 1 */
```

The CPU backends pick it up at different cadences:

- UAE checks `SPCFLAG_INT` after every instruction.
- Unicorn-m68k drains the pending IRQ at every `UC_HOOK_BLOCK`
  (basic-block boundary; mean ~1.95 instructions, max ~38 — see
  [`cpu/JitBlockSizeAnalysis.md`](cpu/JitBlockSizeAnalysis.md)).

## Concrete divergence

Under `--backend dualcpu`, UAE runs slower than Unicorn-m68k inside the
lockstep harness. The interrupt thread fires once based on wall-clock
time. UAE has executed ~28 654 instructions, takes the interrupt, SR
goes `0x2700 → 0x2708`, the handler runs and modifies D0. Unicorn-m68k
has executed many more instructions in the same wall-clock window;
when it finally takes the interrupt at its next block boundary, the
two CPUs are at different PCs with different register state.

| Register | UAE | Unicorn |
|----------|-----|---------|
| SR | 0x2708 | 0x2700 |
| D0 | 0x8EB00000 | 0x26500000 |

Both are correct. Both took the interrupt the next time the interrupt
mask allowed it. They just took it at different points in the
instruction stream.

## What this means in practice

- **Don't compare instruction-level traces** between UAE and
  Unicorn-m68k — they will diverge whenever the timer fires.
- **DualCPU lockstep** masks this by running the two backends in the
  same process under one wall clock; the lockstep harness drives both
  per-instruction. Any divergence past the first wall-clock-driven
  interrupt is real and worth investigating.
- **Functional tests** (boot-to-Finder, API + bridge dispatch, guest
  test suite) work because Mac OS doesn't depend on cycle-accurate
  interrupt timing — both backends reach Finder, populate the same
  trap table, and execute the same EmulOp sequence in the same order.

## A "deterministic mode" if you really need one

Replace the wall-clock timer with an instruction-count timer (fire
`INTFLAG_TIMER` every N instructions on both backends). This breaks
realism but produces matching traces. Useful for narrow debugging,
not production. Not implemented.

## Files

- `src/core/timer.cpp` — Time Manager thread.
- `src/cpu/uae_wrapper.cpp` — `TriggerInterrupt`, `intlev`.
- `src/cpu/unicorn_wrapper.c` — `hook_block` IRQ delivery.
- `src/drivers/platform/timer_interrupt.cpp` — 60 Hz tick.
