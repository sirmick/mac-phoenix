/*
 *  boot_progress.h - Mac OS boot milestone tracking
 *
 *  Tracks boot progress by monitoring EmulOps and Mac low-memory globals.
 *  Emits concise milestone messages instead of per-EmulOp spam.
 *
 *  Log levels (--log-level flag):
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

/* Set IPC buffer for subprocess mode (child writes boot progress here) */
void boot_progress_set_ipc_buffer(void* buf);

/* Call from EmulOp() to track boot progress and log at appropriate verbosity.
 * opcode is architecture-specific (M68K_EMUL_OP_* for m68k, OP_* for PPC). */
void boot_progress_update(uint16_t opcode, void *regs);

/* Platform-level boot events — architecture-independent.
 * Both m68k and PPC map their arch-specific opcodes to these. */
enum BootEvent {
    BOOT_EVENT_RESET,
    BOOT_EVENT_PATCH_BOOT_GLOBS,
    BOOT_EVENT_INSTALL_DRIVERS,
    BOOT_EVENT_CHECKLOAD,
    BOOT_EVENT_IRQ,
    BOOT_EVENT_IDLE_TIME,
};

/* Call from any CPU backend to report a boot event */
void boot_progress_report(enum BootEvent event, void *regs);

/* Get current log level (cached from env var or set via set_log_level) */
int boot_log_level(void);

/* Set log level from config (call at startup) */
void set_log_level(int level);

/* Query boot state (for /api/status) */
const char* boot_progress_phase(void);
unsigned int boot_progress_checkloads(void);
double boot_progress_elapsed(void);

/* Returns true if the current phase is >= the named phase (e.g. "Finder", "desktop").
 * Unknown phase names always return false. */
int boot_progress_phase_reached(const char *phase_name);

/* Compare two phase name strings: returns true if current_phase >= target_phase.
 * For use in fork mode where the parent reads phase names from shared memory. */
int boot_progress_phase_reached_by_name(const char *current_phase, const char *target_phase);

/* Query Mac mouse position from low-memory globals (for /api/mouse) */
void boot_progress_get_mouse(int *x, int *y);

/* Export cursor state and app name from Mac low-memory globals to IPC SHM buffer.
 * Called from PPC tick thread at 60Hz so the parent process can read cursor/app state. */
void boot_progress_export_cursor_to_ipc(void);
void boot_progress_export_app_to_ipc(void);

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
