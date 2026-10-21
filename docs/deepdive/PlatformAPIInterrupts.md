# Platform-API interrupt delivery

Timer and device code raise guest interrupts through one Platform function
pointer. On the 68k ROM machine that pointer only asks the core for
attention; the machine decides the interrupt level from `InterruptFlags` on
the CPU thread.

## API

```c
/* src/common/include/platform.h */
typedef struct {
    /* … */
    void (*cpu_trigger_interrupt)(int level);   /* level is a hint */
    void (*invoke_debug)(void);                 /* Programmer's Key */
    /* … */
} Platform;

extern volatile uint32_t InterruptFlags;   /* INTFLAG_60HZ, _1HZ, _AUDIO, _NMI, … */
void SetInterruptFlag(uint32_t flag);
void ClearInterruptFlag(uint32_t flag);
```

## Caller side

```c++
/* src/drivers/platform/timer_interrupt.cpp — guest_tick(), 60.15 Hz */
SetInterruptFlag(INTFLAG_60HZ);            /* + INTFLAG_1HZ every 60 ticks */
if (g_platform.cpu_trigger_interrupt)
    g_platform.cpu_trigger_interrupt((InterruptFlags & INTFLAG_NMI) ? 7 : 1);
```

`setup_timer_interrupt()` starts the tick thread; each tick also refreshes
video and exports cursor, app name and Mac state to the IPC buffer. With
`--deterministic` the guest part of the tick runs on the CPU thread at
virtual-clock intervals instead. Other sources (audio, Ethernet, serial,
ADB) set their flag and call `TriggerInterrupt()`
(`src/cpu/uae_wrapper.cpp`), which wakes an idle CPU and calls the same
pointer with level 1.

## 68k ROM machine

```c++
/* src/cpu/core/platform_cpu.cpp */
void trigger_interrupt(int) { if (core) core->request_attention(); }

/* src/cpu/cpu_m68k.cpp, RomHost — on the CPU thread */
void attention(cpu::Core &c) override {
    if (InterruptFlags & INTFLAG_NMI) {
        ClearInterruptFlag(INTFLAG_NMI);
        c.set_irq(7);                       /* NMI: an edge */
    }
    c.set_irq(base_level());                /* 1 while any other flag is set */
}
```

`request_attention()` may be called from any thread; the core calls
`Host::attention()` at the next instruction boundary. `set_irq()` is the
level-sensitive interrupt line: the core takes the autovector when the
level is above the SR mask. The ROM's level-1 handler is patched to the
`IRQ` EmulOp (`src/core/emul_op.cpp`), which services and clears the flags;
`RomHost::host_op()` lowers the line again afterwards.

Per core:

- **uae** (`src/cpu/core/uae_core.cpp`) — `request_attention()` sets
  `SPCFLAG_INT | SPCFLAG_DOINT`; UAE's `m68k_do_specialties()` asks the
  `intlev` hook, which serves the attention request and returns the line's
  level, and `Interrupt()` builds the exception frame. This also wakes a
  STOP.
- **mame-68k** (`src/cpu/mame/mame_68k_core.cpp`) — `run()` executes short
  slices and serves attention between them; `set_irq()` inside a slice ends
  it so the new level is seen at once.

## PPC machine

The PPC installers (`src/cpu/kpx/cpu_ppc_kpx.cpp`,
`src/cpu/kpx/cpu_ppc_mame.cpp`) run their own 60 Hz tick thread and deliver
interrupts through the nanokernel (`HandleInterrupt`, gated on
`XLM_IRQ_NEST`); see [`../ppc/README.md`](../ppc/README.md).

## Files

- `src/common/include/platform.h` — Platform struct, `InterruptFlags` API.
- `src/drivers/platform/timer_interrupt.cpp` — 60 Hz tick thread.
- `src/cpu/core/platform_cpu.cpp` — `cpu_trigger_interrupt` on a `cpu::Core`.
- `src/cpu/cpu_m68k.cpp` — `RomHost::attention()`, interrupt level.
- `src/cpu/core/uae_core.cpp`, `src/cpu/mame/mame_68k_core.cpp` — per core.
