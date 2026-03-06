/**
 * Unicorn CPU Validation Module
 *
 * Runs Unicorn CPU in lockstep with UAE for validation during actual BasiliskII execution.
 * This allows us to verify instruction-by-instruction compatibility during ROM boot and OS execution.
 */

#ifndef UNICORN_VALIDATION_H
#define UNICORN_VALIDATION_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize Unicorn validation
 * - Creates Unicorn CPU instance
 * - Maps ROM to Unicorn
 * - Opens divergence log file
 *
 * Must be called AFTER InitAll() so ROM is loaded and patched
 */
bool unicorn_validation_init(void);

/**
 * Shutdown Unicorn validation
 * - Destroys Unicorn CPU
 * - Closes log file
 * - Prints statistics
 */
void unicorn_validation_shutdown(void);

/**
 * Validate one instruction
 * - Captures UAE state before
 * - Executes instruction on UAE
 * - Syncs memory to Unicorn
 * - Executes on Unicorn
 * - Compares states
 * - Logs divergences
 *
 * Returns: true if states match, false if divergence detected
 */
bool unicorn_validation_step(void);

/**
 * Check if validation is enabled
 */
bool unicorn_validation_enabled(void);

/**
 * Enable/disable validation at runtime
 */
void unicorn_validation_set_enabled(bool enabled);

/**
 * Get validation statistics
 */
void unicorn_validation_get_stats(uint64_t *instructions, uint64_t *divergences);

/**
 * Platform EmulOp/Trap Handlers
 *
 * These are called by the CPU wrappers when EmulOps or traps are encountered.
 * The handlers automatically check validation_state.master_cpu to determine
 * which CPU should execute the operation and which should skip.
 *
 * is_primary indicates which CPU is calling:
 * - UAE wrapper calls with is_primary=true
 * - Unicorn wrapper calls with is_primary=false
 *
 * The handler then checks DUALCPU_MASTER env var to decide:
 * - If master_cpu==UAE and is_primary==true: execute and sync to Unicorn
 * - If master_cpu==UNICORN and is_primary==false: execute and sync to UAE
 * - Otherwise: skip (secondary CPU)
 */

/* Unified handlers - automatically check master_cpu
 * Returns true if PC was advanced, false if caller should advance */
bool unicorn_validation_unified_emulop(uint16_t opcode, bool is_primary);
bool unicorn_validation_unified_trap(int vector, uint16_t opcode, bool is_primary);

#ifdef __cplusplus
}
#endif

#endif /* UNICORN_VALIDATION_H */
