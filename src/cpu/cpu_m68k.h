/*
 * cpu_m68k.h - the ROM-based 68k Mac on a cpu::Core.
 *
 * Basilisk II's machine: guest memory from CPUContext's one host block,
 * EmulOps ($71xx) to m68k::EmulOp, interrupts from InterruptFlags (level 1
 * autovector, NMI on level 7). The core is any 68k cpu::Core ("uae",
 * "musashi").
 */
#pragma once

#include "platform.h"
#include "cpu_core.h"

/* Fill p's CPU and memory entries (platform_cpu.h plus this machine's
 * init, reset, run loop and debugger key). core names the cpu::Core;
 * jit asks it for its JIT if it has one. */
void cpu_m68k_install(Platform *p, const char *core, bool jit);

/* The guest address space; CPUContext maps its host block into it. */
cpu::GuestMemory &cpu_m68k_memory();
