/*
 *  boot_progress.h - Mac OS boot milestone tracking
 *
 *  Tracks boot progress by monitoring EmulOps and Mac low-memory globals.
 *  Emits concise milestone messages instead of per-EmulOp spam.
 *
 *  Log levels (MACEMU_LOG_LEVEL env var):
 *    0 = milestones only (default)
 *    1 = milestones + important EmulOps (RESET, INSTALL_DRIVERS, etc.)
 *    2 = all EmulOps with names
 *    3 = all EmulOps + register dumps
 */

#ifndef BOOT_PROGRESS_H
#define BOOT_PROGRESS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Call from EmulOp() to track boot progress and log at appropriate verbosity */
void boot_progress_update(uint16_t opcode, void *regs);

/* Get current log level (cached from env var) */
int boot_log_level(void);

/* Query boot state (for /api/status) */
const char* boot_progress_phase(void);
unsigned int boot_progress_checkloads(void);
double boot_progress_elapsed(void);

/* Query Mac mouse position from low-memory globals (for /api/mouse) */
void boot_progress_get_mouse(int *x, int *y);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_PROGRESS_H */
