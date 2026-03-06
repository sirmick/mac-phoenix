/*
 * Unified CPU Backend Implementation
 */

#include "cpu_backend.h"
#include <stdio.h>

// Forward declarations for UAE wrapper functions
// We don't include UAE headers to avoid C/C++ mixing issues
extern "C" {
	bool uae_cpu_init(void);
	void uae_cpu_reset(void);
	void uae_cpu_execute_one(void);
	uint32_t uae_get_pc(void);
	uint16_t uae_get_sr(void);
	uint32_t uae_get_dreg(int n);
	uint32_t uae_get_areg(int n);
	void uae_set_pc(uint32_t pc);
	void uae_set_sr(uint16_t sr);
	void uae_set_dreg(int n, uint32_t val);
	void uae_set_areg(int n, uint32_t val);
	void uae_mem_read(uint32_t addr, void *data, uint32_t size);
	void uae_mem_write(uint32_t addr, const void *data, uint32_t size);
}

// UAE CPU internals - forward declarations
extern "C" {
	struct regstruct {
		uint32_t regs[16];
		uint32_t pc;
		uint8_t *pc_p;
		uint8_t *pc_oldp;
		uint32_t spcflags;
		int intmask;
		uint32_t vbr, sfc, dfc;
		uint32_t usp, isp, msp;
		uint16_t sr;
		char t1, t0, s, m, x;
		char stopped;
		// ... rest doesn't matter for our use
	};

	extern struct regstruct regs;
	extern bool quit_program;
	extern uint32_t ROMBaseMac;  // For explicit reset initialization
}

/*
 * UAE Backend Implementation
 */

static bool uae_backend_init(void) {
	return uae_cpu_init();
}

static void uae_backend_reset(void) {
	// Explicit Macintosh boot state initialization (matches Unicorn and DualCPU)
	// Don't use m68k_reset() - it reads from ROM which might not be loaded yet
	uae_set_pc(ROMBaseMac + 0x2a);   // ROM entry point
	uae_set_sr(0x2700);               // Supervisor, interrupt mask 7
	for (int i = 0; i < 8; i++) {
		uae_set_dreg(i, 0);
		uae_set_areg(i, 0);
	}
	uae_set_areg(7, 0x2000);         // Initial stack pointer
}

static void uae_backend_destroy(void) {
	// UAE CPU doesn't need explicit cleanup
}

static CPUExecResult uae_backend_execute_one(void) {
	// Execute one instruction
	uae_cpu_execute_one();

	// Check result
	if (regs.stopped) {
		return CPU_EXEC_STOPPED;
	}

	// Check for EmulOp return (quit_program set by m68k_emulop_return)
	if (quit_program) {
		quit_program = false;  // Reset for next EmulOp
		return CPU_EXEC_EMULOP;
	}

	return CPU_EXEC_OK;
}

static void uae_backend_execute_fast(void) {
	// UAE interpreter: just call execute_one() in a loop
	// This is a fallback - the main loop should check if execute_fast is NULL
	// and implement the loop itself for better control
	extern void m68k_execute(void);
	m68k_execute();
}

static bool uae_backend_is_stopped(void) {
	return regs.stopped != 0;
}

static uint32_t uae_backend_get_pc(void) {
	return uae_get_pc();
}

static uint16_t uae_backend_get_sr(void) {
	return uae_get_sr();
}

static uint32_t uae_backend_get_dreg(int n) {
	return uae_get_dreg(n);
}

static uint32_t uae_backend_get_areg(int n) {
	return uae_get_areg(n);
}

static void uae_backend_set_pc(uint32_t pc) {
	uae_set_pc(pc);
}

static void uae_backend_set_sr(uint16_t sr) {
	uae_set_sr(sr);
}

static void uae_backend_set_dreg(int n, uint32_t val) {
	uae_set_dreg(n, val);
}

static void uae_backend_set_areg(int n, uint32_t val) {
	uae_set_areg(n, val);
}

static void uae_backend_mem_read(uint32_t addr, void *data, uint32_t size) {
	// Read from UAE memory
	uae_mem_read(addr, data, size);
}

static void uae_backend_mem_write(uint32_t addr, const void *data, uint32_t size) {
	// Write to UAE memory
	uae_mem_write(addr, data, size);
}

static void uae_backend_trigger_interrupt(int level) {
	// TODO: Implement interrupt triggering
	(void)level;
}

// UAE Backend interface
static CPUBackend uae_backend = {
	.name = "UAE Interpreter",
	.init = uae_backend_init,
	.reset = uae_backend_reset,
	.destroy = uae_backend_destroy,
	.execute_one = uae_backend_execute_one,
	.execute_fast = NULL,  // Use default loop in main.cpp
	.is_stopped = uae_backend_is_stopped,
	.get_pc = uae_backend_get_pc,
	.get_sr = uae_backend_get_sr,
	.get_dreg = uae_backend_get_dreg,
	.get_areg = uae_backend_get_areg,
	.set_pc = uae_backend_set_pc,
	.set_sr = uae_backend_set_sr,
	.set_dreg = uae_backend_set_dreg,
	.set_areg = uae_backend_set_areg,
	.mem_read = uae_backend_mem_read,
	.mem_write = uae_backend_mem_write,
	.trigger_interrupt = uae_backend_trigger_interrupt,
};

/*
 * Backend selection
 */

static CPUBackend *current_backend = NULL;

bool cpu_backend_select(CPUBackendType type) {
	switch (type) {
		case CPU_BACKEND_UAE:
			current_backend = &uae_backend;
			return true;

		case CPU_BACKEND_UAE_JIT:
			// TODO: Implement JIT backend
			fprintf(stderr, "UAE JIT backend not yet implemented\n");
			return false;

		case CPU_BACKEND_UNICORN:
			// TODO: Implement Unicorn backend
			fprintf(stderr, "Unicorn backend not yet implemented\n");
			return false;

		case CPU_BACKEND_DUALCPU:
			// TODO: Implement DualCPU backend
			fprintf(stderr, "DualCPU backend not yet implemented\n");
			return false;

		default:
			fprintf(stderr, "Unknown backend type: %d\n", type);
			return false;
	}
}

CPUBackend* cpu_backend_get(void) {
	if (!current_backend) {
		// Default to UAE backend
		cpu_backend_select(CPU_BACKEND_UAE);
	}
	return current_backend;
}
