# Platform-API interrupt delivery

Timer / device code triggers M68K interrupts through one Platform
function pointer; each backend implements it however suits its CPU
core. There is no shared `PendingInterrupt` global; each backend tracks
its own pending state.

## API

```c
/* src/common/include/platform.h */
typedef struct Platform {
    /* … */
    void (*cpu_trigger_interrupt)(int level);   /* m68k 1..7 */
    /* … */
} Platform;
```

## Caller side

```c
/* src/drivers/platform/timer_interrupt.cpp — polled 60 Hz */
extern Platform g_platform;
SetInterruptFlag(INTFLAG_60HZ);
int level = intlev();                     /* derived from Mac hardware state */
if (level > 0) g_platform.cpu_trigger_interrupt(level);
```

The timer code is backend-agnostic and lives in `src/drivers/platform/`.
PPC has its own tick + PrimeTime threads inside the KPX / Unicorn-PPC
backends because those backends need to gate IRQs on `XLM_IRQ_NEST`.

## UAE implementation

```c
/* src/cpu/cpu_uae.c */
static void uae_backend_trigger_interrupt(int level) {
    if (level > 0 && level <= 7) SPCFLAGS_SET(SPCFLAG_INT);
}
```

Sets UAE's native flag. UAE's `do_specialties()` checks it after every
instruction and `Interrupt(level)` builds the m68k exception frame
natively — supervisor mode, IPL update, vector read, jump. RTE is
handled by UAE's interpreter.

## Unicorn-m68k implementation

```c
/* src/cpu/unicorn_wrapper.c */
static volatile int g_pending_interrupt_level = 0;

void unicorn_trigger_interrupt_internal(int level) {
    if (level >= 1 && level <= 7) g_pending_interrupt_level = level;
}
```

The pending level is drained at every `UC_HOOK_BLOCK`. When level
exceeds the SR mask, we manually build the m68k exception frame:

1. Read SP and PC.
2. Push PC (4 bytes, big-endian) at SP-4.
3. Push SR (2 bytes, big-endian) at SP-6.
4. Update SR — set the supervisor bit, set IPL bits to the new level.
5. Read the autovector at `VBR + (24 + level) * 4`.
6. Defer the PC write to the handler (deferred-update mechanism).
7. `uc_emu_stop()` so the new PC takes effect on the next
   `uc_emu_start()`.

## Why we don't call QEMU's `m68k_set_irq_level`

Reaching it requires walking through `uc_struct` → `CPUState` →
`M68kCPU` at struct field offsets that aren't part of the public
Unicorn API and vary by build (compiler, alignment, Unicorn version).
A hardcoded offset segfaulted; including the QEMU headers needed for
`offsetof` cascaded into the rest of QEMU's build. The manual stack
build uses `uc_reg_*` and `uc_mem_*` only — public API, portable
across compilers / archs / Unicorn versions, and exactly mirrors what
QEMU's `do_interrupt_m68k_hardirq` does.

## Outstanding niceties

- **VBR**: currently hardcoded to 0. Should read VBR for 68020+.
- **SP word alignment**: QEMU does `sp &= ~1`; we don't yet.
- **68020+ format/vector word**: not pushed.
- **Exception nesting**: not modelled.

None block boot — Mac OS 7.x doesn't rely on these. Worth tightening
when someone runs into a workload that does.

## Files

- `src/common/include/platform.h` — Platform struct.
- `src/cpu/cpu_uae.c` — UAE backend, `uae_backend_trigger_interrupt`.
- `src/cpu/unicorn_wrapper.c` — Unicorn-m68k backend, manual frame
  builder in `hook_block`.
- `src/cpu/cpu_unicorn_ppc.cpp`, `src/cpu/kpx/cpu_ppc_kpx.cpp` — PPC
  variants; see [`../../ppc/README.md`](../../ppc/README.md).
- `src/drivers/platform/timer_interrupt.cpp` — 60 Hz polling.
