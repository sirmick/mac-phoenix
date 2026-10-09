/*
 * platform_cpu.h - g_platform's CPU and memory entries on a cpu::Core.
 *
 * Every machine (ROM-based 68k, Executor) fills these Platform entries the
 * same way, so code that reads guest memory or registers through
 * g_platform works on any of them:
 *
 *   mem_read_* / mem_write_*, mem_mac_to_host, mem_host_to_mac
 *                          the GuestMemory
 *   cpu_get_pc / _sr / _dreg / _areg
 *   cpu_request_stop       Core::request_stop
 *   cpu_trigger_interrupt  Core::request_attention (the level is a hint;
 *                          the machine's Host::attention sets the line)
 *   cpu_execute_68k        call guest code until it returns
 *   cpu_execute_68k_trap   run one A-trap
 *   flush_code_cache       Core::invalidate (everything)
 *
 * The machine adds what is its own: cpu_init, cpu_reset, cpu_execute_fast,
 * invoke_debug. Execute68k-style calls end on the "return to host" host op
 * ($7100), which the machine's Host must answer with Stop, leaving the PC
 * on it (cpu_core.h).
 *
 * 68k only for now; a PowerPC machine gets its own execute entries.
 */
#pragma once

#include "cpu_core.h"
#include "platform.h"

namespace cpu {

/* Point p's entries at the active core and memory. Several tables may be
 * installed (main keeps a copy of g_platform); they all follow the
 * active ones. */
void platform_install(Platform *p);

/* The address space the memory entries use. Set it before anything reads
 * guest memory; it may still be unmapped. */
void platform_set_memory(GuestMemory *memory);
/* The core the CPU entries use (nullptr: none yet). */
void platform_set_core(Core *core);

GuestMemory *platform_memory();
Core *platform_core();

}  // namespace cpu
