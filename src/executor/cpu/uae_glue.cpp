/*
 * uae_glue.cpp - the few symbols MacPhoenix's UAE core expects from the
 * rest of the emulator, provided for the Executor child.
 *
 * The ROM emulator gets these from src/core and src/cpu; the Executor
 * child has no ROM, no ADB, no timer driver, so most are inert.
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "platform.h"

Platform g_platform;

int CPUType = 4;   /* 68040 */
int FPUType = 0;
bool TwentyFourBitAddressing = false;
volatile bool PendingInterrupt = false;

struct M68kRegisters;
namespace m68k {
void EmulOp(uint16_t opcode, M68kRegisters *)
{
    /* Unreachable: the facade installs g_platform.m68k_emulop_handler. */
    fprintf(stderr, "executor: stray EmulOp %04x\n", opcode);
    abort();
}
}

extern "C" {
/* mac-phoenix routes these through g_platform; same here. */
int intlev(void) { return g_platform.m68k_intlev ? g_platform.m68k_intlev() : -1; }
uint64_t poll_timer_interrupt(void)
{
    if(g_platform.m68k_poll_interrupts)
        g_platform.m68k_poll_interrupts();
    return 0;
}
void cpu_trace_log_mem_read(uint32_t, uint32_t, int) {}
bool cpu_trace_memory_enabled(void) { return false; }
void cpu_trace_log_interrupt_trigger(int) {}
void cpu_trace_log_interrupt_taken(int, uint32_t) {}
}
