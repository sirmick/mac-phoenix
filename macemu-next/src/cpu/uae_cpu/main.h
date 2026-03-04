/**
 * Minimal main.h stub for UAE CPU compilation
 */

#ifndef MAIN_H
#define MAIN_H

/* CPU types */
#define CPU_68000 0
#define CPU_68010 1
#define CPU_68020 2
#define CPU_68030 3
#define CPU_68040 4

/* FPU types */
#define FPU_NONE 0
#define FPU_68881 1
#define FPU_68882 2
#define FPU_68040 3

/* Global CPU/FPU type - will be set by UAE wrapper */
extern int CPUType;
extern int FPUType;

/* Interrupt flags - defined in uae_wrapper.cpp */
// NOTE: Using uint32_t to match uae_wrapper.h declaration
extern volatile uint32_t InterruptFlags;

#define INTFLAG_60HZ 1

/* ROM/RAM info - will be set by UAE wrapper */
extern uint8 *ROMBaseHost;
extern uint32 ROMSize;
extern uint8 *RAMBaseHost;
extern uint32 RAMSize;

/* Quit flag */
extern volatile bool QuitEmulator;

/* M68k Registers structure (for EmulOp and Execute68k) */
#include "m68k_registers.h"

/* Minimal functions */
// TriggerInterrupt and TriggerNMI are implemented in basilisk_glue.cpp
static inline void SetInterruptFlag(uint32 flag) { (void)flag; }
static inline void ClearInterruptFlag(uint32 flag) { (void)flag; }

#endif /* MAIN_H */
