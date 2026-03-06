/**
 * UAE CPU Backend for Platform API
 *
 * Wraps UAE interpreter to conform to platform CPU interface.
 * Always available, no compile-time dependencies.
 */

#include "platform.h"
#include "sysdeps.h"  // For UAE types (uae_u32, etc.)
#include "uae_wrapper.h"
#include "uae_cpu/spcflags.h"  // For SPCFLAG_INT
#include <stdbool.h>
#include <stddef.h>

// Forward declarations for UAE execution (implemented in basilisk_glue.cpp)
struct M68kRegisters;
extern void uae_execute_68k_trap(uint16_t trap, struct M68kRegisters *r);
extern void uae_execute_68k(uint32_t addr, struct M68kRegisters *r);

// UAE internals (minimal forward declarations)
extern struct regstruct {
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
} regs;

extern bool quit_program;

// CPU Configuration
static void uae_backend_set_type(int cpu_type, int fpu_type) {
	uae_set_cpu_type(cpu_type, fpu_type);
}

// CPU Lifecycle
static bool uae_backend_init(void) {
	return uae_cpu_init();
}

static void uae_backend_reset(void) {
	uae_cpu_reset();
}

// Execution
static int uae_backend_execute_one(void) {
	uae_cpu_execute_one();

	if (regs.stopped) {
		return 1;  // CPU_EXEC_STOPPED
	}

	if (quit_program) {
		quit_program = false;
		return 4;  // CPU_EXEC_EMULOP
	}

	return 0;  // CPU_EXEC_OK
}

// Forward declare UAE's fast execution wrapper (C linkage)
extern void uae_m68k_execute_fast(void);  // Runs until quit_program is set

static void uae_backend_execute_fast(void) {
	// Use UAE's fast execution loop (executes until quit_program)
	uae_m68k_execute_fast();
}

// State Query
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

// Interrupts
static void uae_backend_trigger_interrupt(int level) {
	/* Set interrupt flag - UAE's do_specialties() will check this
	 * and call Interrupt(level) which handles everything natively:
	 * - Builds M68K exception stack frame (SR, PC, Format/Vector for 68020+)
	 * - Sets supervisor mode
	 * - Updates interrupt mask
	 * - Reads vector table
	 * - Jumps to interrupt handler
	 * RTE is handled natively by UAE interpreter
	 */
	if (level > 0 && level <= 7) {
		SPCFLAGS_SET(SPCFLAG_INT);
	}
}

/**
 * Install UAE CPU backend into platform
 */
void cpu_uae_install(Platform *p) {
	p->cpu_name = "UAE Interpreter";
	p->use_aline_emulops = false;

	// Configuration
	p->cpu_set_type = uae_backend_set_type;

	// Lifecycle
	p->cpu_init = uae_backend_init;
	p->cpu_reset = uae_backend_reset;

	// Execution
	p->cpu_execute_one = uae_backend_execute_one;
	// Disable fast path if CPU_TRACE is set (tracing only works in execute_one)
	if (getenv("CPU_TRACE")) {
		p->cpu_execute_fast = NULL;  // Force slow path for tracing
	} else {
		p->cpu_execute_fast = uae_backend_execute_fast;  // Fast continuous execution loop
	}

	// State query
	p->cpu_get_pc = uae_backend_get_pc;
	p->cpu_get_sr = uae_backend_get_sr;
	p->cpu_get_dreg = uae_backend_get_dreg;
	p->cpu_get_areg = uae_backend_get_areg;

	// Interrupts
	p->cpu_trigger_interrupt = uae_backend_trigger_interrupt;

	// Trap execution
	p->cpu_execute_68k_trap = uae_execute_68k_trap;

	// 68k subroutine execution
	p->cpu_execute_68k = uae_execute_68k;

	// EmulOp and trap handlers
	// UAE handles these internally through m68k_emulop/m68k_do_trap
	// Setting these to NULL means UAE will use its built-in handlers
	p->emulop_handler = NULL;
	p->trap_handler = NULL;

	// Memory system (for ROM patching and initialization)
	p->mem_read_byte = uae_mem_read_byte;
	p->mem_read_word = uae_mem_read_word;
	p->mem_read_long = uae_mem_read_long;
	p->mem_write_byte = uae_mem_write_byte;
	p->mem_write_word = uae_mem_write_word;
	p->mem_write_long = uae_mem_write_long;
	p->mem_mac_to_host = uae_mem_mac_to_host;
	p->mem_host_to_mac = uae_mem_host_to_mac;
}
