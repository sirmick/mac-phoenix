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

extern "C" {
void cpu_trace_log_mem_read(uint32_t, uint32_t, int) {}
bool cpu_trace_memory_enabled(void) { return false; }
void cpu_trace_log_interrupt_trigger(int) {}
void cpu_trace_log_interrupt_taken(int, uint32_t) {}
}
