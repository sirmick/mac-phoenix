/*
 *  boot_progress.cpp - Mac OS boot milestone tracking
 *
 *  Replaces verbose per-EmulOp logging with concise boot milestones.
 *  Reads Mac low-memory globals to detect state transitions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "macos_util.h"
#include "m68k_registers.h"
#include "emul_op.h"
#include "boot_progress.h"
#include "shared_state.h"
#include "adb.h"
#include "../config/emulator_config.h"

/* Boot phases */
enum BootPhase {
	PHASE_PRE_RESET = 0,
	PHASE_ROM_INIT,        /* RESET EmulOp fired */
	PHASE_BOOT_GLOBS,      /* PATCH_BOOT_GLOBS done */
	PHASE_DRIVERS,         /* INSTALL_DRIVERS fired */
	PHASE_WARM_START,      /* WLSC marker written (HasMacStarted) */
	PHASE_BOOT_BLOCKS,     /* 'boot' resource loaded */
	PHASE_EXTENSIONS,      /* First INIT resource loaded */
	PHASE_FINDER_LAUNCH,   /* CurApName = "Finder" */
	PHASE_DESKTOP,         /* Finder idle (repeated STR loads) */
};

static BootPhase current_phase = PHASE_PRE_RESET;
static int log_level = -1;  /* -1 = uninitialized */
static SharedState* g_boot_shm = nullptr;  /* Shared memory for fork mode */

void boot_progress_set_shared_state(SharedState* shm)
{
	g_boot_shm = shm;
}
static uint32_t checkload_count = 0;
static uint32_t last_milestone_checkload = 0;
static bool seen_boot_resource = false;
static bool seen_init_resource = false;
static bool seen_finder = false;
static char last_app_name[64] = {0};
static struct timespec boot_start_time = {0, 0};
static uint32_t irq_count = 0;
static bool shutdown_dialog_dismissed = false;

static double elapsed_sec(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	if (boot_start_time.tv_sec == 0 && boot_start_time.tv_nsec == 0)
		boot_start_time = now;
	return (now.tv_sec - boot_start_time.tv_sec) +
	       (now.tv_nsec - boot_start_time.tv_nsec) / 1e9;
}

void set_log_level(int level)
{
	log_level = level;
}

int boot_log_level(void)
{
	if (log_level == -1) {
		log_level = 0;
	}
	return log_level;
}

/* Read CurApName ($0910) - Pascal string */
static void read_cur_app_name(char *out, int maxlen)
{
	uint8_t len = ReadMacInt8(0x0910);
	if (len == 0 || len > 31 || len >= maxlen) {
		out[0] = '\0';
		return;
	}
	for (int i = 0; i < len; i++)
		out[i] = (char)ReadMacInt8(0x0911 + i);
	out[len] = '\0';
}

/* Decode CHECKLOAD resource type from D1 register */
static void decode_resource_type(uint32_t d1, char *out)
{
	out[0] = (d1 >> 24) & 0xff;
	out[1] = (d1 >> 16) & 0xff;
	out[2] = (d1 >> 8) & 0xff;
	out[3] = d1 & 0xff;
	out[4] = '\0';
}

static const char *phase_name(BootPhase p)
{
	switch (p) {
		case PHASE_PRE_RESET:      return "pre-reset";
		case PHASE_ROM_INIT:       return "ROM init";
		case PHASE_BOOT_GLOBS:     return "boot globs";
		case PHASE_DRIVERS:        return "drivers";
		case PHASE_WARM_START:     return "warm start";
		case PHASE_BOOT_BLOCKS:    return "boot blocks";
		case PHASE_EXTENSIONS:     return "extensions";
		case PHASE_FINDER_LAUNCH:  return "Finder";
		case PHASE_DESKTOP:        return "desktop";
	}
	return "?";
}

static void milestone(const char *msg)
{
	fprintf(stderr, "[Boot +%6.2fs] %s\n", elapsed_sec(), msg);
}

static void milestonef(const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	fprintf(stderr, "[Boot +%6.2fs] %s\n", elapsed_sec(), buf);
}

static void set_phase(BootPhase p)
{
	if (p > current_phase) {
		current_phase = p;
		/* Write to shared memory for fork mode */
		if (g_boot_shm) {
			strncpy(g_boot_shm->boot_phase_name, phase_name(p),
			        sizeof(g_boot_shm->boot_phase_name) - 1);
			if (g_boot_shm->boot_start_us.load(std::memory_order_relaxed) == 0) {
				struct timespec now;
				clock_gettime(CLOCK_MONOTONIC, &now);
				g_boot_shm->boot_start_us.store(
					now.tv_sec * 1000000LL + now.tv_nsec / 1000,
					std::memory_order_release);
			}
		}
	}
}

/* Map opcode to short name (returns NULL for uninteresting ops) */
static const char *emulop_name(uint16_t opcode)
{
	switch (opcode) {
		case M68K_EMUL_OP_RESET:             return "RESET";
		case M68K_EMUL_OP_PATCH_BOOT_GLOBS:  return "PATCH_BOOT_GLOBS";
		case M68K_EMUL_OP_FIX_BOOTSTACK:     return "FIX_BOOTSTACK";
		case M68K_EMUL_OP_FIX_MEMSIZE:       return "FIX_MEMSIZE";
		case M68K_EMUL_OP_INSTALL_DRIVERS:   return "INSTALL_DRIVERS";
		case M68K_EMUL_OP_SONY_OPEN:         return "SONY_OPEN";
		case M68K_EMUL_OP_DISK_OPEN:         return "DISK_OPEN";
		case M68K_EMUL_OP_CDROM_OPEN:        return "CDROM_OPEN";
		case M68K_EMUL_OP_VIDEO_OPEN:        return "VIDEO_OPEN";
		case M68K_EMUL_OP_SERIAL_OPEN:       return "SERIAL_OPEN";
		case M68K_EMUL_OP_ETHER_OPEN:        return "ETHER_OPEN";
		case M68K_EMUL_OP_CHECKLOAD:         return "CHECKLOAD";
		case M68K_EMUL_OP_IRQ:               return "IRQ";
		case M68K_EMUL_OP_CLKNOMEM:          return "CLKNOMEM";
		case M68K_EMUL_OP_SHUTDOWN:          return "SHUTDOWN";
		case M68K_EMUL_OP_IDLE_TIME:         return "IDLE_TIME";
		default: return NULL;
	}
}

/* Is this an "important" EmulOp worth showing at log level 1? */
static bool is_important_emulop(uint16_t opcode)
{
	switch (opcode) {
		case M68K_EMUL_OP_RESET:
		case M68K_EMUL_OP_PATCH_BOOT_GLOBS:
		case M68K_EMUL_OP_FIX_BOOTSTACK:
		case M68K_EMUL_OP_FIX_MEMSIZE:
		case M68K_EMUL_OP_INSTALL_DRIVERS:
		case M68K_EMUL_OP_SONY_OPEN:
		case M68K_EMUL_OP_DISK_OPEN:
		case M68K_EMUL_OP_CDROM_OPEN:
		case M68K_EMUL_OP_VIDEO_OPEN:
		case M68K_EMUL_OP_SERIAL_OPEN:
		case M68K_EMUL_OP_ETHER_OPEN:
		case M68K_EMUL_OP_SHUTDOWN:
			return true;
		default:
			return false;
	}
}

/*
 * WindowRecord layout (Inside Macintosh: Macintosh Toolbox Essentials):
 *   +0..+107: GrafPort (108 bytes)
 *     +16: portRect (Rect: top, left, bottom, right)
 *   +108: windowKind (int16) — 2 = dialogKind
 *   +110: visible (Boolean)
 *   +134: titleHandle (handle -> Pascal string)
 *   +144: nextWindow (WindowPeek)
 */

/* Read a window's title into buf. Returns length, 0 if none. */
static int read_window_title(uint32_t wp, char *buf, int bufsize)
{
	buf[0] = '\0';
	uint32_t title_handle = ReadMacInt32(wp + 134);
	if (!title_handle) return 0;
	uint32_t title_ptr = ReadMacInt32(title_handle);
	if (!title_ptr || title_ptr >= 0x02000000) return 0;
	uint8_t tlen = ReadMacInt8(title_ptr);
	if (tlen == 0 || tlen >= bufsize) return 0;
	for (int i = 0; i < tlen; i++)
		buf[i] = (char)ReadMacInt8(title_ptr + 1 + i);
	buf[tlen] = '\0';
	return tlen;
}

/*
 * Check for the "improper shutdown" dialog and dismiss it with Return.
 *
 * Detection criteria (all must match):
 *   - Boot phase is exactly FINDER_LAUNCH (not before, not after)
 *   - Front window has windowKind == 2 (dialogKind)
 *   - Window title is "Please Don't Get this Often" (Mac OS internal name)
 *   - Not already dismissed (latched)
 */
static void check_shutdown_dialog(void)
{
	if (shutdown_dialog_dismissed) return;
	if (!RAMBaseHost || RAMSize == 0) return;

	uint32_t wp = ReadMacInt32(0x09D6);  /* WindowList — front window first */
	if (!wp || wp >= 0x02000000) return;

	int16_t wKind = (int16_t)ReadMacInt16(wp + 108);
	if (wKind != 2) return;  /* not a dialog */

	bool visible = ReadMacInt8(wp + 110) != 0;
	if (!visible) return;

	char title[64];
	read_window_title(wp, title, sizeof(title));
	if (strcmp(title, "Please Don't Get this Often") != 0) return;

	/* Match! Dismiss with Return keypress. */
	shutdown_dialog_dismissed = true;
	milestonef("Improper shutdown dialog detected -- auto-dismissing");
	ADBKeyDown(0x24);  /* Return */
	ADBKeyUp(0x24);
}

void boot_progress_update(uint16_t opcode, void *regs_ptr)
{
	int level = boot_log_level();
	M68kRegisters *r = (M68kRegisters *)regs_ptr;

	/* Level 2+: log all EmulOps with names */
	if (level >= 2) {
		const char *name = emulop_name(opcode);
		if (name)
			fprintf(stderr, "  [EmulOp] %04x %s\n", opcode, name);
		else
			fprintf(stderr, "  [EmulOp] %04x\n", opcode);
	}

	/* Level 3: register dumps for important ops */
	if (level >= 3 && is_important_emulop(opcode)) {
		fprintf(stderr, "    d0=%08x d1=%08x a0=%08x a7=%08x sr=%04x\n",
		        r->d[0], r->d[1], r->a[0], r->a[7], r->sr);
	}

	/* Track boot milestones (always, regardless of level) */
	switch (opcode) {
		case M68K_EMUL_OP_RESET:
			if (current_phase < PHASE_ROM_INIT) {
				milestone("ROM init started (RESET)");
				set_phase(PHASE_ROM_INIT);
			}
			break;

		case M68K_EMUL_OP_PATCH_BOOT_GLOBS:
			if (current_phase < PHASE_BOOT_GLOBS) {
				milestone("Boot globals patched");
				set_phase(PHASE_BOOT_GLOBS);
			}
			break;

		case M68K_EMUL_OP_INSTALL_DRIVERS:
			if (current_phase < PHASE_DRIVERS) {
				milestone("Installing drivers");
				set_phase(PHASE_DRIVERS);
			}
			break;

		case M68K_EMUL_OP_CHECKLOAD: {
			checkload_count++;
			if (g_boot_shm) {
				g_boot_shm->checkload_count.store(checkload_count, std::memory_order_relaxed);
			}

			/* Decode resource type */
			char type[5];
			decode_resource_type(r->d[1], type);
			int16_t id = (int16_t)ReadMacInt16(r->a[2]);

			/* Level 2+: log every CHECKLOAD */
			if (level >= 2) {
				fprintf(stderr, "    CHECKLOAD #%u type='%s' id=%d\n",
				        checkload_count, type, id);
			}

			/* Detect WLSC warm start (if not already detected) */
			if (current_phase < PHASE_WARM_START && HasMacStarted()) {
				milestonef("Mac warm start complete (WLSC) after %u resources", checkload_count);
				set_phase(PHASE_WARM_START);
			}

			/* Detect boot blocks */
			if (!seen_boot_resource && memcmp(type, "boot", 4) == 0) {
				seen_boot_resource = true;
				milestonef("Loading boot blocks (resource #%u)", checkload_count);
				set_phase(PHASE_BOOT_BLOCKS);
			}

			/* Detect first INIT (extension loading phase) */
			if (!seen_init_resource && memcmp(type, "INIT", 4) == 0) {
				seen_init_resource = true;
				milestonef("Loading extensions (first INIT at resource #%u)", checkload_count);
				set_phase(PHASE_EXTENSIONS);
			}

			/* Check CurApName periodically for app launch detection */
			if (checkload_count % 50 == 0 || (checkload_count > 500 && checkload_count % 10 == 0)) {
				char app_name[64];
				read_cur_app_name(app_name, sizeof(app_name));
				if (app_name[0] && strcmp(app_name, last_app_name) != 0) {
					strncpy(last_app_name, app_name, sizeof(last_app_name) - 1);
					if (strcmp(app_name, "Finder") == 0) {
						if (!seen_finder) {
							seen_finder = true;
							milestonef("Finder launched (resource #%u) -- desktop ready", checkload_count);
							set_phase(PHASE_FINDER_LAUNCH);
						}
					} else if (level >= 1) {
						milestonef("App launched: '%s' (resource #%u)", app_name, checkload_count);
					}
				}
			}

			/* Log level 1: periodic progress every 500 resources */
			if (level >= 1 && checkload_count % 500 == 0) {
				milestonef("Resource #%u loaded (phase: %s)", checkload_count, phase_name(current_phase));
			}

			/* Log level 0: periodic progress every 1000 resources */
			if (level == 0 && checkload_count % 1000 == 0) {
				milestonef("%u resources loaded (phase: %s)", checkload_count, phase_name(current_phase));
			}

			break;
		}

		case M68K_EMUL_OP_IRQ:
			irq_count++;
			/* Check for Finder on each IRQ (60Hz) once we're past extensions */
			if (!seen_finder && current_phase >= PHASE_EXTENSIONS) {
				char app_name[64];
				read_cur_app_name(app_name, sizeof(app_name));
				if (strcmp(app_name, "Finder") == 0) {
					seen_finder = true;
					milestonef("Finder launched -- desktop ready");
					set_phase(PHASE_FINDER_LAUNCH);
				}
			}
			/* Check for improper shutdown dialog during Finder launch phase only */
			if (!shutdown_dialog_dismissed && current_phase == PHASE_FINDER_LAUNCH
			    && irq_count % 30 == 0  /* ~2Hz check rate */
			    && config::EmulatorConfig::instance().dismiss_shutdown_dialog) {
				check_shutdown_dialog();
			}
			break;

		default:
			/* Level 1: log important EmulOps */
			if (level >= 1 && is_important_emulop(opcode)) {
				const char *name = emulop_name(opcode);
				fprintf(stderr, "[Boot] EmulOp: %s\n", name ? name : "?");
			}
			break;
	}
}

const char* boot_progress_phase(void)
{
	return phase_name(current_phase);
}

unsigned int boot_progress_checkloads(void)
{
	return checkload_count;
}

double boot_progress_elapsed(void)
{
	return elapsed_sec();
}

void boot_progress_get_mouse(int *x, int *y)
{
	/* Mac low-memory globals: MTemp Y at 0x828, X at 0x82a (what ADB wrote) */
	*y = (int16_t)ReadMacInt16(0x828);
	*x = (int16_t)ReadMacInt16(0x82a);
}

void boot_progress_get_cursor_state(MacCursorState *state)
{
	/* MTemp: written by ADB interrupt handler */
	state->mtemp_y = (int16_t)ReadMacInt16(0x828);
	state->mtemp_x = (int16_t)ReadMacInt16(0x82a);
	/* RawMouse: written by ADB interrupt handler */
	state->raw_y = (int16_t)ReadMacInt16(0x82c);
	state->raw_x = (int16_t)ReadMacInt16(0x82e);
	/* Mouse: written by Mac OS Cursor Manager (proof of processing) */
	state->cursor_y = (int16_t)ReadMacInt16(0x830);
	state->cursor_x = (int16_t)ReadMacInt16(0x832);
	/* Cursor Manager flags */
	state->crsr_busy = ReadMacInt8(0x8cd);
	state->crsr_new = ReadMacInt8(0x8ce);
	state->crsr_couple = ReadMacInt8(0x8cf);
}
