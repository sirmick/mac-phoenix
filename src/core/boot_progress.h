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

/* Query detailed Mac cursor state from low-memory globals */
typedef struct {
    int mtemp_x, mtemp_y;       /* 0x82A/0x828: MTemp - what ADB wrote */
    int raw_x, raw_y;           /* 0x82E/0x82C: RawMouse - ADB raw position */
    int cursor_x, cursor_y;     /* 0x832/0x830: Mouse - Cursor Manager output */
    int crsr_new;               /* 0x8CE: cursor position changed flag */
    int crsr_couple;            /* 0x8CF: cursor coupled to mouse flag */
    int crsr_busy;              /* 0x8CD: cursor manager busy flag */
} MacCursorState;

void boot_progress_get_cursor_state(MacCursorState *state);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_PROGRESS_H */
