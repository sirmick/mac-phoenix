// Stubs for UAE 68k code dependencies
// UAE code is compiled from source but never executes during PPC boot.
// These stubs satisfy linker references.

#include <stdint.h>
#include <cstddef>

extern "C" {

// CPU trace infrastructure (cpu_trace.c)
int cpu_trace_memory_enabled = 0;
void cpu_trace_log_mem_read(uint32_t, uint32_t, int) {}
void cpu_trace_log_interrupt_trigger(int) {}
void cpu_trace_log_interrupt_taken(int) {}

// Timer (timer_interrupt.cpp)
void poll_timer_interrupt(void) {}

// CPU state (newcpu.cpp uses these)
int CPUType = 4;      // 68040
int FPUType = 1;      // 68881
int PendingInterrupt = 0;

// Interrupt level
int intlev(void) { return 0; }

}

// EmulOp — UAE's version (different signature from KPX)
struct M68kRegisters;
void EmulOp(unsigned short, M68kRegisters*) {}
