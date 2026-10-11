/*
 *  host_ppc.cpp - SheepShaver host helpers both PPC cores need
 *
 *  MakeExecutable, the interrupt nesting count and the interrupt signal
 *  stack. They lived in cpu_ppc_kpx.cpp, which only builds on x86 hosts
 *  (Kheperix's dyngen); mame-ppc needs them on every host (docs/cpu/PLAN.md,
 *  C2d).
 */

#include "sysdeps.h"
#include "kpx_cpu_emulation.h"
#include "main.h"
#include "xlowmem.h"

// FlushCodeCache: Kheperix's, in cpu_ppc_kpx.cpp on x86 and a no-op in
// kpx_stub.cpp elsewhere. mame-ppc verifies its blocks instead.
void FlushCodeCache(uintptr start, uintptr end);

void MakeExecutable(int dummy, uint32 start, uint32 length)
{
	(void)dummy;
	if ((start >= ROMBase) && (start < (ROMBase + ROM_SIZE)))
		return;
	FlushCodeCache(start, start + length);
}

// Interrupt enable/disable, as SheepShaver's main_unix.cpp: DisableInterrupt
// increments XLM_IRQ_NEST to block HandleInterrupt, EnableInterrupt
// decrements it. (main.cpp has weak no-op stand-ins for builds without PPC.)
void DisableInterrupt(void)
{
	WriteMacInt32(XLM_IRQ_NEST, int32(ReadMacInt32(XLM_IRQ_NEST)) + 1);
}

void EnableInterrupt(void)
{
	WriteMacInt32(XLM_IRQ_NEST, int32(ReadMacInt32(XLM_IRQ_NEST)) - 1);
}

// Signal stack for PPC interrupt handling. Must be in Mac address space. Set
// to ROMEnd (= ROMBase + ROM_AREA_SIZE) during initialization, matching
// legacy SheepShaver (main_unix.cpp:2340).
static uintptr sig_stack = 0;
static const uint32 SIG_STACK_SIZE = 0x10000;  // 64KB

uintptr SignalStackBase(void)
{
	return sig_stack + SIG_STACK_SIZE;
}

// Called from cpu_context.cpp after the ROM area is allocated.
extern "C" void kpx_set_signal_stack(uintptr addr)
{
	sig_stack = addr;
}
