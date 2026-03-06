/*
 * Unified CPU Backend API
 *
 * Provides a uniform interface for different CPU emulation backends:
 * - UAE (interpretive)
 * - UAE JIT (just-in-time compilation)
 * - Unicorn (validation backend)
 * - DualCPU (UAE + Unicorn validation)
 *
 * Key design principle: Execution loop is at HIGHER level, not in backend!
 * Backends provide single-step execution and state management.
 */

#ifndef CPU_BACKEND_H
#define CPU_BACKEND_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Execution result codes
 */
typedef enum {
	CPU_EXEC_OK           = 0,  // Instruction executed successfully
	CPU_EXEC_STOPPED      = 1,  // Hit STOP instruction
	CPU_EXEC_BREAKPOINT   = 2,  // Hit breakpoint (debugging)
	CPU_EXEC_EXCEPTION    = 3,  // Unhandled exception
	CPU_EXEC_EMULOP       = 4,  // Executed EmulOp (BasiliskII trap handler)
	CPU_EXEC_DIVERGENCE   = 5,  // DualCPU: UAE and Unicorn diverged
} CPUExecResult;

/*
 * CPU Backend Interface
 *
 * All backends must implement this interface.
 * Single-step execution (execute_one) is REQUIRED.
 * Fast-path execution (execute_fast) is OPTIONAL (for JIT).
 */
typedef struct CPUBackend {
	const char *name;  // Backend name (for logging)

	//
	// Lifecycle
	//
	bool (*init)(void);      // Initialize backend (called after memory setup)
	void (*reset)(void);     // Reset CPU to initial state
	void (*destroy)(void);   // Clean up backend

	//
	// Execution - Single step (REQUIRED)
	//
	// Execute ONE instruction and return result.
	// Higher-level code implements the execution loop!
	//
	CPUExecResult (*execute_one)(void);

	//
	// Execution - Fast path (OPTIONAL, for JIT)
	//
	// Run in tight loop until STOP/EmulOp/exception.
	// If NULL, higher-level code will call execute_one() in a loop.
	// If non-NULL, can be used for performance-critical code (JIT).
	//
	void (*execute_fast)(void);

	//
	// State query
	//
	bool     (*is_stopped)(void);          // Check if CPU hit STOP instruction
	uint32_t (*get_pc)(void);              // Program Counter
	uint16_t (*get_sr)(void);              // Status Register
	uint32_t (*get_dreg)(int n);           // Data register D0-D7
	uint32_t (*get_areg)(int n);           // Address register A0-A7

	//
	// State modification
	//
	void (*set_pc)(uint32_t pc);           // Set Program Counter
	void (*set_sr)(uint16_t sr);           // Set Status Register
	void (*set_dreg)(int n, uint32_t val); // Set data register
	void (*set_areg)(int n, uint32_t val); // Set address register

	//
	// Memory access (for validation/debugging)
	//
	void (*mem_read)(uint32_t addr, void *data, uint32_t size);
	void (*mem_write)(uint32_t addr, const void *data, uint32_t size);

	//
	// Interrupt control (optional)
	//
	void (*trigger_interrupt)(int level);  // Trigger interrupt (1-7)

} CPUBackend;

/*
 * Backend selection
 */
typedef enum {
	CPU_BACKEND_UAE,        // UAE interpreter
	CPU_BACKEND_UAE_JIT,    // UAE with JIT compiler
	CPU_BACKEND_UNICORN,    // Unicorn validation backend
	CPU_BACKEND_DUALCPU,    // UAE + Unicorn validation
} CPUBackendType;

/*
 * Get the currently active CPU backend
 *
 * Returns a pointer to the backend interface structure.
 * The backend is determined by compile-time configuration and runtime flags.
 */
CPUBackend* cpu_backend_get(void);

/*
 * Set the active CPU backend
 *
 * This must be called BEFORE cpu_backend_get() or any execution.
 * Returns true on success, false on failure.
 */
bool cpu_backend_select(CPUBackendType type);

#ifdef __cplusplus
}
#endif

#endif /* CPU_BACKEND_H */
