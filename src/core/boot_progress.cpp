/*
 *  boot_progress.cpp - Mac OS boot milestone tracking
 *
 *  Replaces verbose per-EmulOp logging with concise boot milestones.
 *  Reads Mac low-memory globals to detect state transitions.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <ctime>
#include <atomic>

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "macos_util.h"
#include "m68k_registers.h"
#include "emul_op.h"
#include "boot_progress.h"
#include "ipc_protocol.h"
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
	PHASE_DESKTOP,         /* Finder event loop idle (IDLE_TIME EmulOp) */
};

static BootPhase g_current_phase = PHASE_PRE_RESET;
static int g_log_level = -1;  /* -1 = uninitialized */
static IPCBuffer* g_ipc_buf = nullptr;     /* IPC buffer for subprocess mode */

void boot_progress_set_ipc_buffer(void* buf)
{
	g_ipc_buf = static_cast<IPCBuffer*>(buf);
}
static uint32_t g_checkload_count = 0;
static bool g_seen_boot_resource = false;
static bool g_seen_init_resource = false;
static bool g_seen_finder = false;
static char g_last_app_name[64] = {0};
static struct timespec g_boot_start_time = {0, 0};
static int g_dialogs_dismissed = 0;
static double g_last_dialog_dismiss_time = 0.0;
#define MAX_DIALOG_DISMISSALS 5       /* safety limit per boot */
#define DIALOG_DISMISS_COOLDOWN 2.0   /* seconds between dismissals */

static double elapsed_sec(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	if (g_boot_start_time.tv_sec == 0 && g_boot_start_time.tv_nsec == 0)
		g_boot_start_time = now;
	return (now.tv_sec - g_boot_start_time.tv_sec) +
	       (now.tv_nsec - g_boot_start_time.tv_nsec) / 1e9;
}

void set_log_level(int level)
{
	g_log_level = level;
}

int boot_log_level(void)
{
	if (g_log_level == -1) {
		g_log_level = 0;
	}
	return g_log_level;
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
		out[i] = static_cast<char>(ReadMacInt8(0x0911 + i));
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
	if (p > g_current_phase) {
		g_current_phase = p;
		/* Write to IPC buffer for subprocess mode */
		if (g_ipc_buf) {
			snprintf(g_ipc_buf->boot_phase, sizeof(g_ipc_buf->boot_phase),
			         "%s", phase_name(p));
			if (IPC_ATOMIC_LOAD(g_ipc_buf->boot_start_us) == 0) {
				struct timespec now;
				clock_gettime(CLOCK_MONOTONIC, &now);
				IPC_ATOMIC_STORE(g_ipc_buf->boot_start_us,
					now.tv_sec * 1000000LL + now.tv_nsec / 1000);
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
		buf[i] = static_cast<char>(ReadMacInt8(title_ptr + 1 + i));
	buf[tlen] = '\0';
	return tlen;
}

/*
 * Check for the "improper shutdown" dialog and dismiss it with Return.
 *
 * Detection criteria (all must match):
 *   - Front window has windowKind == 2 (dialogKind)
 *   - Window is visible
 *   - Haven't exceeded dismissal limit (safety)
 *   - Cooldown elapsed since last dismissal (avoid rapid-fire)
 *
 * Handles multiple dialogs across OS versions:
 *   - Mac OS 7.x: "improper shutdown" dialog
 *   - Mac OS 8.x: "improper shutdown", rebuild desktop
 *   - Mac OS 9.x: Disk First Aid, extensions conflict, etc.
 */
static void check_shutdown_dialog(void)
{
	if (g_dialogs_dismissed >= MAX_DIALOG_DISMISSALS) return;

	/* Cooldown: don't dismiss the same dialog twice in quick succession */
	double now = elapsed_sec();
	if (now - g_last_dialog_dismiss_time < DIALOG_DISMISS_COOLDOWN) return;

	uint32_t wp = ReadMacInt32(0x09D6);  /* WindowList — front window first */
	if (!wp || wp >= RAMSize) return;  /* sanity: must be within RAM */

	int16_t wKind = static_cast<int16_t>(ReadMacInt16(wp + 108));
	bool visible = ReadMacInt8(wp + 110) != 0;

	if (wKind != 2) return;  /* not a dialog */
	if (!visible) return;

	char title[64];
	read_window_title(wp, title, sizeof(title));

	fprintf(stderr, "[DIALOG] Visible dialog found: wKind=%d title='%s' — dismissing (#%d)\n",
	        wKind, title, g_dialogs_dismissed + 1);

	/* Dismiss via Return key press — works for most Mac OS modal dialogs.
	 * The mouse click approach is fragile (wrong coordinates for different
	 * dialog types/resolutions). Return hits the default button. */
	g_dialogs_dismissed++;
	g_last_dialog_dismiss_time = now;
	milestonef("Dialog auto-dismissed (title='%s', #%d)", title, g_dialogs_dismissed);

	ADBKeyDown(0x24);  /* Return key = Mac keycode 0x24 = 36 */
	ADBKeyUp(0x24);
}

void boot_progress_update(uint16_t opcode, void *regs_ptr)
{
	int level = boot_log_level();
	auto *r = static_cast<M68kRegisters *>(regs_ptr);

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
			if (g_current_phase < PHASE_ROM_INIT) {
				milestone("ROM init started (RESET)");
				set_phase(PHASE_ROM_INIT);
			}
			break;

		case M68K_EMUL_OP_PATCH_BOOT_GLOBS:
			if (g_current_phase < PHASE_BOOT_GLOBS) {
				milestone("Boot globals patched");
				set_phase(PHASE_BOOT_GLOBS);
			}
			break;

		case M68K_EMUL_OP_INSTALL_DRIVERS:
			if (g_current_phase < PHASE_DRIVERS) {
				milestone("Installing drivers");
				set_phase(PHASE_DRIVERS);
			}
			break;

		case M68K_EMUL_OP_CHECKLOAD: {
			g_checkload_count++;
			if (g_ipc_buf) {
				IPC_ATOMIC_STORE(g_ipc_buf->checkload_count, g_checkload_count);
			}

			/* Decode resource type */
			char type[5];
			decode_resource_type(r->d[1], type);
			int16_t id = static_cast<int16_t>(ReadMacInt16(r->a[2]));

			/* Level 2+: log every CHECKLOAD */
			if (level >= 2) {
				fprintf(stderr, "    CHECKLOAD #%u type='%s' id=%d\n",
				        g_checkload_count, type, id);
			}

			/* Detect WLSC warm start (if not already detected) */
			if (g_current_phase < PHASE_WARM_START && HasMacStarted()) {
				milestonef("Mac warm start complete (WLSC) after %u resources", g_checkload_count);
				set_phase(PHASE_WARM_START);
			}

			/* Detect boot blocks */
			if (!g_seen_boot_resource && memcmp(type, "boot", 4) == 0) {
				g_seen_boot_resource = true;
				milestonef("Loading boot blocks (resource #%u)", g_checkload_count);
				set_phase(PHASE_BOOT_BLOCKS);
			}

			/* Detect first INIT (extension loading phase) */
			if (!g_seen_init_resource && memcmp(type, "INIT", 4) == 0) {
				g_seen_init_resource = true;
				milestonef("Loading extensions (first INIT at resource #%u)", g_checkload_count);
				set_phase(PHASE_EXTENSIONS);
			}

			/* Log level 1: periodic progress every 500 resources */
			if (level >= 1 && g_checkload_count % 500 == 0) {
				milestonef("Resource #%u loaded (phase: %s)", g_checkload_count, phase_name(g_current_phase));
			}

			/* Log level 0: periodic progress every 1000 resources */
			if (level == 0 && g_checkload_count % 1000 == 0) {
				milestonef("%u resources loaded (phase: %s)", g_checkload_count, phase_name(g_current_phase));
			}

			break;
		}

		case M68K_EMUL_OP_IRQ:
			/* Detect Finder launch — sole place for CurApName checking */
			if (!g_seen_finder && g_current_phase >= PHASE_BOOT_BLOCKS) {
				char app_name[64];
				read_cur_app_name(app_name, sizeof(app_name));
				if (app_name[0] && strcmp(app_name, g_last_app_name) != 0) {
					snprintf(g_last_app_name, sizeof(g_last_app_name), "%s", app_name);
					if (level >= 1)
						milestonef("App launched: '%s'", app_name);
				}
				if (strcmp(app_name, "Finder") == 0) {
					g_seen_finder = true;
					milestonef("Finder launched");
					set_phase(PHASE_FINDER_LAUNCH);
				}
			}
			/* Check for improper shutdown dialog during boot (appears before Finder) */
			if (g_dialogs_dismissed < MAX_DIALOG_DISMISSALS && g_current_phase >= PHASE_WARM_START
			    && config::EmulatorConfig::instance().dismiss_shutdown_dialog) {
				check_shutdown_dialog();
			}
			break;

		case M68K_EMUL_OP_IDLE_TIME:
			/* IDLE_TIME fires when the app event loop is idle (no events pending).
			 * First IDLE_TIME after Finder launch = desktop is fully drawn and responsive. */
			if (g_seen_finder && g_current_phase < PHASE_DESKTOP) {
				milestonef("Desktop ready (Finder idle)");
				set_phase(PHASE_DESKTOP);
			}
			/* Check for improper shutdown dialog once desktop is up */
			if (g_dialogs_dismissed < MAX_DIALOG_DISMISSALS && g_current_phase >= PHASE_FINDER_LAUNCH
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

/*
 *  Platform-level boot event handler (architecture-independent).
 *  Maps BootEvent enums to the same state machine as boot_progress_update.
 */
void boot_progress_report(enum BootEvent event, void *regs_ptr)
{
	M68kRegisters *r = (M68kRegisters *)regs_ptr;

	switch (event) {
		case BOOT_EVENT_RESET:
			if (g_current_phase < PHASE_ROM_INIT) {
				milestone("ROM init started (RESET)");
				set_phase(PHASE_ROM_INIT);
			}
			break;

		case BOOT_EVENT_PATCH_BOOT_GLOBS:
			if (g_current_phase < PHASE_BOOT_GLOBS) {
				milestone("Boot globals patched");
				set_phase(PHASE_BOOT_GLOBS);
			}
			break;

		case BOOT_EVENT_INSTALL_DRIVERS: {
			/* On m68k: fires before WLSC. On PPC: fires after WLSC.
			 * Always log the milestone, advance phase if not already past it. */
			static bool seen_install_drivers = false;
			if (!seen_install_drivers) {
				seen_install_drivers = true;
				milestone("Installing drivers");
				if (g_current_phase < PHASE_DRIVERS)
					set_phase(PHASE_DRIVERS);
			}
			break;
		}

		case BOOT_EVENT_CHECKLOAD: {
			g_checkload_count++;
			if (g_ipc_buf) {
				IPC_ATOMIC_STORE(g_ipc_buf->checkload_count, g_checkload_count);
			}

			int level = boot_log_level();

			/* Detect WLSC warm start */
			if (g_current_phase < PHASE_WARM_START && HasMacStarted()) {
				milestonef("Mac warm start complete (WLSC) after %u resources", g_checkload_count);
				set_phase(PHASE_WARM_START);
			}

			/* Detect boot blocks */
			if (r && !g_seen_boot_resource) {
				char type[5];
				decode_resource_type(r->d[1], type);
				if (memcmp(type, "boot", 4) == 0) {
					g_seen_boot_resource = true;
					milestonef("Loading boot blocks (resource #%u)", g_checkload_count);
					set_phase(PHASE_BOOT_BLOCKS);
				}
				if (!g_seen_init_resource && memcmp(type, "INIT", 4) == 0) {
					g_seen_init_resource = true;
					milestonef("Loading extensions (first INIT at resource #%u)", g_checkload_count);
					set_phase(PHASE_EXTENSIONS);
				}
			}

			/* Periodic progress */
			if (level >= 1 && g_checkload_count % 500 == 0) {
				milestonef("Resource #%u loaded (phase: %s)", g_checkload_count, phase_name(g_current_phase));
			}
			if (level == 0 && g_checkload_count % 1000 == 0) {
				milestonef("%u resources loaded (phase: %s)", g_checkload_count, phase_name(g_current_phase));
			}
			break;
		}

		case BOOT_EVENT_IRQ: {
			/* Detect Finder launch via CurApName */
			if (!g_seen_finder && g_current_phase >= PHASE_BOOT_BLOCKS) {
				char app_name[64];
				read_cur_app_name(app_name, sizeof(app_name));
				if (app_name[0] && strcmp(app_name, g_last_app_name) != 0) {
					snprintf(g_last_app_name, sizeof(g_last_app_name), "%s", app_name);
					if (boot_log_level() >= 1)
						milestonef("App launched: '%s'", app_name);
				}
				if (strcmp(app_name, "Finder") == 0) {
					g_seen_finder = true;
					milestonef("Finder launched");
					set_phase(PHASE_FINDER_LAUNCH);
				}
			}
			/* Check for improper shutdown dialog during boot (appears before Finder) */
			if (g_dialogs_dismissed < MAX_DIALOG_DISMISSALS && g_current_phase >= PHASE_WARM_START
			    && config::EmulatorConfig::instance().dismiss_shutdown_dialog) {
				check_shutdown_dialog();
			}
			break;
		}

		case BOOT_EVENT_IDLE_TIME:
			if (g_seen_finder && g_current_phase < PHASE_DESKTOP) {
				milestonef("Desktop ready (Finder idle)");
				set_phase(PHASE_DESKTOP);
			}
			if (g_dialogs_dismissed < MAX_DIALOG_DISMISSALS && g_current_phase >= PHASE_FINDER_LAUNCH
			    && config::EmulatorConfig::instance().dismiss_shutdown_dialog) {
				check_shutdown_dialog();
			}
			break;
	}
}

const char* boot_progress_phase(void)
{
	return phase_name(g_current_phase);
}

static int phase_ordinal(const char *name)
{
	static const struct { const char *name; int ordinal; } phases[] = {
		{"pre-reset",   PHASE_PRE_RESET},
		{"ROM init",    PHASE_ROM_INIT},
		{"boot globs",  PHASE_BOOT_GLOBS},
		{"drivers",     PHASE_DRIVERS},
		{"warm start",  PHASE_WARM_START},
		{"boot blocks", PHASE_BOOT_BLOCKS},
		{"extensions",  PHASE_EXTENSIONS},
		{"Finder",      PHASE_FINDER_LAUNCH},
		{"desktop",     PHASE_DESKTOP},
	};
	for (const auto &p : phases) {
		if (strcmp(p.name, name) == 0)
			return p.ordinal;
	}
	return -1;
}

int boot_progress_phase_reached(const char *name)
{
	int target = phase_ordinal(name);
	if (target < 0) return 0;
	return static_cast<int>(g_current_phase) >= target;
}

int boot_progress_phase_reached_by_name(const char *current_phase, const char *target_phase)
{
	int cur = phase_ordinal(current_phase);
	int tgt = phase_ordinal(target_phase);
	if (cur < 0 || tgt < 0) return 0;
	return cur >= tgt;
}

unsigned int boot_progress_checkloads(void)
{
	return g_checkload_count;
}

double boot_progress_elapsed(void)
{
	return elapsed_sec();
}

void boot_progress_get_mouse(int *x, int *y)
{
	/* Mac low-memory globals: MTemp Y at 0x828, X at 0x82a (what ADB wrote) */
	*y = static_cast<int16_t>(ReadMacInt16(0x828));
	*x = static_cast<int16_t>(ReadMacInt16(0x82a));
}

void boot_progress_get_cursor_state(MacCursorState *state)
{
	/* MTemp: written by ADB interrupt handler */
	state->mtemp_y = static_cast<int16_t>(ReadMacInt16(0x828));
	state->mtemp_x = static_cast<int16_t>(ReadMacInt16(0x82a));
	/* RawMouse: written by ADB interrupt handler */
	state->raw_y = static_cast<int16_t>(ReadMacInt16(0x82c));
	state->raw_x = static_cast<int16_t>(ReadMacInt16(0x82e));
	/* Mouse: written by Mac OS Cursor Manager (proof of processing) */
	state->cursor_y = static_cast<int16_t>(ReadMacInt16(0x830));
	state->cursor_x = static_cast<int16_t>(ReadMacInt16(0x832));
	/* Cursor Manager flags */
	state->crsr_busy = ReadMacInt8(0x8cd);
	state->crsr_new = ReadMacInt8(0x8ce);
	state->crsr_couple = ReadMacInt8(0x8cf);
}

/*
 * Export cursor state from Mac low-memory globals to IPC SHM buffer.
 * Called at 60Hz from PPC tick thread so the parent process can serve
 * GET /api/mouse with up-to-date cursor positions.
 */
void boot_progress_export_cursor_to_ipc(void)
{
	if (!g_ipc_buf) return;
	/* Mouse: Cursor Manager output (what the user sees) */
	IPC_ATOMIC_STORE(g_ipc_buf->shm_cursor_x, static_cast<uint32_t>(static_cast<uint16_t>(ReadMacInt16(0x832))));
	IPC_ATOMIC_STORE(g_ipc_buf->shm_cursor_y, static_cast<uint32_t>(static_cast<uint16_t>(ReadMacInt16(0x830))));
	/* RawMouse */
	IPC_ATOMIC_STORE(g_ipc_buf->shm_raw_x, static_cast<uint32_t>(static_cast<uint16_t>(ReadMacInt16(0x82e))));
	IPC_ATOMIC_STORE(g_ipc_buf->shm_raw_y, static_cast<uint32_t>(static_cast<uint16_t>(ReadMacInt16(0x82c))));
	/* MTemp */
	IPC_ATOMIC_STORE(g_ipc_buf->shm_mtemp_x, static_cast<uint32_t>(static_cast<uint16_t>(ReadMacInt16(0x82a))));
	IPC_ATOMIC_STORE(g_ipc_buf->shm_mtemp_y, static_cast<uint32_t>(static_cast<uint16_t>(ReadMacInt16(0x828))));
	/* Cursor Manager flags */
	IPC_ATOMIC_STORE(g_ipc_buf->shm_crsr_new, static_cast<uint32_t>(ReadMacInt8(0x8ce)));
	IPC_ATOMIC_STORE(g_ipc_buf->shm_crsr_couple, static_cast<uint32_t>(ReadMacInt8(0x8cf)));
	IPC_ATOMIC_STORE(g_ipc_buf->shm_crsr_busy, static_cast<uint32_t>(ReadMacInt8(0x8cd)));
}

/*
 * Export current app name from Mac low-memory to IPC SHM buffer.
 * Called at 60Hz from PPC tick thread for parent's GET /api/app.
 */
void boot_progress_export_app_to_ipc(void)
{
	if (!g_ipc_buf) return;
	char app_name[32];
	read_cur_app_name(app_name, sizeof(app_name));
	if (app_name[0] && strcmp(app_name, g_ipc_buf->cur_app_name) != 0) {
		snprintf(g_ipc_buf->cur_app_name, sizeof(g_ipc_buf->cur_app_name), "%s", app_name);
	}
}
