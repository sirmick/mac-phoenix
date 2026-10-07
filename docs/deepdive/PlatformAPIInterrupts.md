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
PPC has its own tick + PrimeTime threads inside the KPX backend because
it needs to gate IRQs on `XLM_IRQ_NEST`.

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

## Files

- `src/common/include/platform.h` — Platform struct.
- `src/cpu/cpu_uae.c` — UAE backend, `uae_backend_trigger_interrupt`.
- `src/cpu/kpx/cpu_ppc_kpx.cpp` — PPC variant; see
  [`../ppc/README.md`](../ppc/README.md).
- `src/drivers/platform/timer_interrupt.cpp` — 60 Hz polling.
