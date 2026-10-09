/*
 * uae_host_hooks.h - how the UAE interpreter reaches whoever drives it.
 *
 * Private to UAE and its cpu::Core (src/cpu/core/uae_core.cpp), which
 * fills these in for as long as it exists. Nothing else sets them.
 */
#ifndef UAE_HOST_HOOKS_H
#define UAE_HOST_HOOKS_H

#include <stdint.h>

struct UaeHostHooks {
    /* A host op ($7101-$71FF). UAE advances the PC by 2 afterwards. */
    void (*emulop)(uint16_t opcode);
    /* Interrupt level to take now, or -1. Called on the CPU thread when
     * UAE looks for interrupts (SPCFLAG_INT/DOINT, the STOP loop). */
    int (*intlev)(void);
    /* Every emulated_ticks_quantum instructions, on the CPU thread. */
    void (*poll)(void);
};

extern struct UaeHostHooks uae_host_hooks;

#endif
