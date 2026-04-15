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
#include "command_bridge.h"
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
static double g_desktop_ready_time = 0.0;
static double g_finder_ready_time = 0.0;
#define DIALOG_DISMISS_COOLDOWN 1.0   /* min seconds between dismissals */
static double g_last_dialog_dismiss_time = 0.0;


/*
 * Known boot-time dialog titles to auto-dismiss.
 * Add new entries as they are discovered on different OS versions.
 * Verified titles:
 *   ""  — Mac OS 9.0.4 (PPC): untitled StopAlert for improper shutdown
 */
static const char *const BOOT_DIALOG_WHITELIST[] = {
	"",                            /* untitled alerts (Mac OS 9.x shutdown, disk check) */
	"Please Don't Get this Often", /* Mac OS 7.5.x improper shutdown */
	NULL
};

static bool g_auto_launch_done = false;
static double g_last_auto_launch_attempt = 0.0;
static uint32_t g_pending_auto_launch_id = 0;
static int g_auto_launch_attempts = 0;
#define AUTO_LAUNCH_COOLDOWN 1.0   /* seconds between retries */
#define AUTO_LAUNCH_MAX_ATTEMPTS 20

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

/*
 * Validate a Mac-space pointer lies within RAM and leaves room for `size`
 * bytes. Rejects low I/O (< 0x400), ROM (>= 0x02000000), and oversize reads.
 */
static inline bool mac_ptr_valid(uint32_t ptr, uint32_t size)
{
	if (ptr < 0x400) return false;
	if (ptr >= RAMSize) return false;
	if (size > RAMSize - ptr) return false;
	return true;
}

/*
 * Safely read a Pascal string from Mac RAM into a C buffer.
 * Returns length copied (0 if invalid / empty). Always zero-terminates.
 * Clamps to min(31, bufsize-1) — Pascal strings can't exceed 255 but Mac
 * window/menu titles are capped at 31 per Inside Macintosh.
 */
static int safe_read_pstring(uint32_t ptr, char *buf, int bufsize)
{
	if (bufsize < 1) return 0;
	buf[0] = '\0';
	if (!mac_ptr_valid(ptr, 1)) return 0;
	uint8_t tlen = ReadMacInt8(ptr);
	if (tlen == 0) return 0;
	int cap = bufsize - 1;
	if (cap > 31) cap = 31;
	if (tlen > cap) tlen = cap;
	if (!mac_ptr_valid(ptr, 1 + tlen)) return 0;
	for (int i = 0; i < tlen; i++)
		buf[i] = static_cast<char>(ReadMacInt8(ptr + 1 + i));
	buf[tlen] = '\0';
	return tlen;
}

/* Read a window's title into buf. Returns length, 0 if none. */
static int read_window_title(uint32_t wp, char *buf, int bufsize)
{
	if (bufsize > 0) buf[0] = '\0';
	if (!mac_ptr_valid(wp, 138)) return 0;
	uint32_t title_handle = ReadMacInt32(wp + 134);
	if (!mac_ptr_valid(title_handle, 4)) return 0;
	uint32_t title_ptr = ReadMacInt32(title_handle);
	return safe_read_pstring(title_ptr, buf, bufsize);
}

/*
 * Snapshot of the front window's dialog-relevant fields. Taking a snapshot
 * once per call reduces re-reads during a traversal (the CPU thread can
 * mutate the WindowRecord between reads). Returns false if the front
 * window is missing/invalid.
 */
struct FrontWindowInfo {
	uint32_t wp;
	int16_t kind;
	bool visible;
	char title[64];
};

static bool snapshot_front_window(FrontWindowInfo *out)
{
	out->wp = 0;
	out->kind = 0;
	out->visible = false;
	out->title[0] = '\0';
	uint32_t wp = ReadMacInt32(0x09D6);  /* WindowList — front window first */
	if (!mac_ptr_valid(wp, 138)) return false;
	out->wp = wp;
	out->kind = static_cast<int16_t>(ReadMacInt16(wp + 108));
	out->visible = ReadMacInt8(wp + 110) != 0;
	read_window_title(wp, out->title, sizeof(out->title));
	return true;
}

/* True if a visible modal dialog (windowKind == 2, dialogKind) is front-most. */
static bool front_is_modal_dialog(void)
{
	FrontWindowInfo info;
	if (!snapshot_front_window(&info)) return false;
	return info.kind == 2 && info.visible;
}

/*
 * Promote PHASE_FINDER_LAUNCH → PHASE_DESKTOP when Finder's event loop is
 * idle and no modal dialog is on screen. Used by IDLE_TIME handlers on both
 * m68k and PPC paths — replaces the silent promotion that previously only
 * happened inside serialize_mac_state (window heuristic, 500 ms cadence).
 */
static void try_promote_to_desktop(void)
{
	if (g_current_phase != PHASE_FINDER_LAUNCH) return;
	if (front_is_modal_dialog()) return;
	g_desktop_ready_time = elapsed_sec();
	set_phase(PHASE_DESKTOP);
	milestone("Desktop ready (Finder idle, no modal)");
}

/*
 * Auto-dismiss known boot-time dialogs.
 *
 * Two-layer protection against dismissing user dialogs:
 *
 *   1. Whitelist: only dialogs whose window title matches
 *      BOOT_DIALOG_WHITELIST are dismissed. Application dialogs (save,
 *      print, alerts) have titles that aren't in the whitelist.
 *
 *   2. Phase gate: only active between PHASE_WARM_START and PHASE_DESKTOP.
 *      Desktop detection varies by OS version:
 *        - Mac OS 7–9: IDLE_TIME EmulOp fires when Finder's event loop
 *          goes idle → sets PHASE_DESKTOP.
 *        - System 6: IDLE_TIME is never patched, but once CurApName is
 *          "Finder" the next CHECKLOAD promotes to PHASE_DESKTOP (Finder's
 *          idle loop is already running by that point).
 *
 * Dismissal sends a Return keypress (ADB 0x24) which hits the default
 * button on standard Mac OS modal dialogs.
 *
 * Enable with --dismiss-shutdown-dialog or dismiss_shutdown_dialog in JSON.
 * Add new dialog titles to BOOT_DIALOG_WHITELIST as they are discovered.
 */
static void check_shutdown_dialog(void)
{
	if (!config::EmulatorConfig::instance().dismiss_shutdown_dialog) return;
	if (g_current_phase < PHASE_WARM_START) return;

	/* Phase gate: active from warm start through a short settle after desktop
	 * is reached. The old hard cap (MAX_DIALOG_DISMISSALS=3) + 1s cooldown +
	 * 4s settle could leave a dialog un-dismissed; rely on cooldown + phase
	 * gate instead. A dialog that appears well after desktop is considered
	 * user-facing and must not be dismissed. */
	if (g_current_phase >= PHASE_DESKTOP) {
		if (g_desktop_ready_time > 0.0 &&
		    elapsed_sec() - g_desktop_ready_time > 3.0) return;
	}

	double now = elapsed_sec();
	if (now - g_last_dialog_dismiss_time < DIALOG_DISMISS_COOLDOWN) return;

	FrontWindowInfo info;
	if (!snapshot_front_window(&info)) return;
	if (info.kind != 2 || !info.visible) return;

	bool matched = false;
	for (int i = 0; BOOT_DIALOG_WHITELIST[i] != NULL; i++) {
		if (strcmp(info.title, BOOT_DIALOG_WHITELIST[i]) == 0) {
			matched = true;
			break;
		}
	}
	if (!matched) return;

	g_dialogs_dismissed++;
	g_last_dialog_dismiss_time = now;
	milestonef("Boot dialog auto-dismissed (title='%s', #%d)", info.title, g_dialogs_dismissed);

	ADBKeyDown(0x24);  /* Return key = Mac keycode 0x24 = 36 */
	ADBKeyUp(0x24);
}

/*
 *  Fire configured auto-launch after desktop is ready.
 *  Queues a LAUNCH_APP command on the in-process command bridge.
 *  The jGNEFilter picks it up on the next GetNextEvent and executes
 *  _Launch (0xA9F2) in Finder's application context.
 *
 *  ExtFS volumes may not be mounted immediately at desktop time, so if the
 *  first attempt fails with a validation error (e.g. nsvErr -35), this
 *  function retries on subsequent IDLE_TIME ticks until the command bridge
 *  reports success or we hit the attempt limit.
 */
static void maybe_fire_auto_launch(void)
{
	if (g_auto_launch_done) return;
	const std::string& path = config::EmulatorConfig::instance().auto_launch_app;
	if (path.empty()) return;

	/* Don't dispatch until Finder is *really* idle. Three gates replace the
	 * old fixed 4s DESKTOP_SETTLE_SEC, closing the "4th dialog at +3.5s"
	 * race:
	 *   1. PHASE_DESKTOP held for >= 2s (brief settle after idle).
	 *   2. No modal dialog currently front-most.
	 *   3. No dismiss fired in the last 1s (lets a just-sent Return key be
	 *      processed before we poke the Process Manager).
	 */
	static const double DESKTOP_SETTLE_SEC = 2.0;

	double now = elapsed_sec();
	if (g_desktop_ready_time == 0.0) return;  /* shouldn't happen — caller gates on PHASE_DESKTOP */
	if (now - g_desktop_ready_time < DESKTOP_SETTLE_SEC) return;
	if (front_is_modal_dialog()) return;
	if (now - g_last_dialog_dismiss_time < 1.0) return;

	/* Write bridge command file if bridge is enabled */
	const auto& cfg = config::EmulatorConfig::instance();
	if (!cfg.bridge_enabled || cfg.bridge_dir.empty()) {
		g_auto_launch_done = true;
		return;
	}

	/* Check for result from previous attempt */
	std::string res_path = cfg.bridge_dir + "/_bridge_result";
	if (g_pending_auto_launch_id != 0) {
		FILE* rf = fopen(res_path.c_str(), "r");
		if (rf) {
			char buf[32] = {};
			fgets(buf, sizeof(buf), rf);
			fclose(rf);
			remove(res_path.c_str());
			int err = atoi(buf);
			if (err == 0) {
				milestonef("Auto-launch: '%s' succeeded", path.c_str());
				g_auto_launch_done = true;
				return;
			}
			milestonef("Auto-launch: attempt %d failed (err=%d)", g_auto_launch_attempts, err);
			g_pending_auto_launch_id = 0;
		} else {
			return;  /* still pending */
		}
	}

	if (now - g_last_auto_launch_attempt < AUTO_LAUNCH_COOLDOWN) return;

	if (g_auto_launch_attempts >= AUTO_LAUNCH_MAX_ATTEMPTS) {
		if (!g_auto_launch_done) {
			milestonef("Auto-launch: giving up on '%s' after %d attempts",
			           path.c_str(), g_auto_launch_attempts);
			g_auto_launch_done = true;
		}
		return;
	}

	g_auto_launch_attempts++;
	g_last_auto_launch_attempt = now;
	milestonef("Auto-launch: writing bridge cmd '%s' (attempt %d)",
	           path.c_str(), g_auto_launch_attempts);

	std::string cmd_path = cfg.bridge_dir + "/_bridge_cmd";
	FILE* f = fopen(cmd_path.c_str(), "w");
	if (f) {
		fprintf(f, "LAUNCH %s", path.c_str());
		fclose(f);
		g_pending_auto_launch_id = 1;  /* flag as pending */
	}
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

			/* Log level 0: periodic progress every 1000 resources (10000 during desktop) */
			{
				unsigned interval = (g_current_phase >= PHASE_FINDER_LAUNCH) ? 10000 : 1000;
				if (level == 0 && g_checkload_count % interval == 0) {
					milestonef("%u resources loaded (phase: %s)", g_checkload_count, phase_name(g_current_phase));
				}
			}

			break;
		}

		case M68K_EMUL_OP_IRQ:
			/* Advance to WARM_START from IRQ if HasMacStarted() — System 6
			 * may not fire enough CHECKLOAD events for the CHECKLOAD path
			 * to detect WLSC. */
			if (g_current_phase < PHASE_WARM_START && HasMacStarted()) {
				milestonef("Mac warm start complete (WLSC) after %u resources", g_checkload_count);
				set_phase(PHASE_WARM_START);
			}
			check_shutdown_dialog();
			break;

		case M68K_EMUL_OP_IDLE_TIME:
			check_shutdown_dialog();
			try_promote_to_desktop();
			if (g_current_phase >= PHASE_DESKTOP) {
				maybe_fire_auto_launch();
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

		case BOOT_EVENT_IRQ:
			check_shutdown_dialog();
			break;

		case BOOT_EVENT_IDLE_TIME:
			check_shutdown_dialog();
			try_promote_to_desktop();
			if (g_current_phase >= PHASE_DESKTOP) {
				maybe_fire_auto_launch();
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

/*
 * Serialize Mac OS state (windows, globals) into a JSON string.
 * Writes into buf (max bufsize bytes). Used by both IPC export and
 * in-process /api/status handler.
 */
static void serialize_mac_state(char *buf, int bufsize)
{
	int pos = 0;

	/* App name */
	char app_name[32];
	read_cur_app_name(app_name, sizeof(app_name));

	/* Globals */
	uint32_t ticks = ReadMacInt32(0x016A);
	bool menu_bar = ReadMacInt32(0x0A1C) != 0;

	pos += snprintf(buf + pos, bufsize - pos,
		"{\"app\":\"%s\",\"ticks\":%u,\"menu_bar\":%s,\"windows\":[",
		app_name, ticks, menu_bar ? "true" : "false");

	/* Walk WindowList — also detect Finder via characteristic windows */
	uint32_t wp = ReadMacInt32(0x09D6);
	int count = 0;
	bool found_finder = false;
	while (wp && wp < RAMSize && count < 20 && pos < bufsize - 100) {
		if (count > 0) buf[pos++] = ',';

		/* Read title */
		char title[64] = "";
		read_window_title(wp, title, sizeof(title));

		int16_t kind = static_cast<int16_t>(ReadMacInt16(wp + 108));
		bool visible = ReadMacInt8(wp + 110) != 0;
		int16_t top = static_cast<int16_t>(ReadMacInt16(wp + 16));
		int16_t left = static_cast<int16_t>(ReadMacInt16(wp + 18));
		int16_t bottom = static_cast<int16_t>(ReadMacInt16(wp + 20));
		int16_t right = static_cast<int16_t>(ReadMacInt16(wp + 22));

		/* Detect Finder's characteristic windows:
		 *   "Desktop" kind 20 — Mac OS 7.x–9.x
		 *   "Clipboard" kind 17 — System 6 */
		if ((strcmp(title, "Desktop") == 0 && kind == 20) ||
		    (strcmp(title, "Clipboard") == 0 && kind == 17)) {
			found_finder = true;
		}

		/* Escape quotes in title */
		char escaped[128];
		int ei = 0;
		for (int i = 0; title[i] && ei < (int)sizeof(escaped) - 2; i++) {
			if (title[i] == '"' || title[i] == '\\')
				escaped[ei++] = '\\';
			escaped[ei++] = title[i];
		}
		escaped[ei] = '\0';

		pos += snprintf(buf + pos, bufsize - pos,
			"{\"title\":\"%s\",\"kind\":%d,\"visible\":%s,\"rect\":[%d,%d,%d,%d]}",
			escaped, kind, visible ? "true" : "false",
			left, top, right, bottom);

		wp = ReadMacInt32(wp + 144);
		count++;
	}

	pos += snprintf(buf + pos, bufsize - pos, "]}");

	/* BridgeAgent (Startup Items app) running implies Finder reached
	 * desktop — that's the only way Startup Items launch. Treat its
	 * presence as equivalent to seeing the Desktop window, even though
	 * BridgeAgent is frontmost and Finder's desktop window isn't. */
	if (!found_finder && strcmp(app_name, "BridgeAgent") == 0) {
		found_finder = true;
	}

	/* Advance phase when the characteristic Finder windows appear.
	 *
	 *   - First sighting → PHASE_FINDER_LAUNCH (logged).
	 *   - Subsequent sightings with no front modal → PHASE_DESKTOP (logged).
	 *
	 * Desktop promotion is the same rule as try_promote_to_desktop(), kept
	 * here as a fallback when IDLE_TIME doesn't fire (e.g. System 6, ROMs
	 * without IDLE_TIME hooks). Either path will advance — never silently. */
	if (found_finder && !g_seen_finder) {
		g_seen_finder = true;
		g_finder_ready_time = elapsed_sec();
		milestone("Finder detected (window heuristic)");
		set_phase(PHASE_FINDER_LAUNCH);
	}
	if (g_current_phase == PHASE_FINDER_LAUNCH && found_finder) {
		try_promote_to_desktop();
	}
}

/* Set Finder phase only (not desktop) — used by PPC fallback where
 * window heuristic doesn't work. Does NOT set PHASE_DESKTOP to avoid
 * triggering auto-launch timing prematurely. */
void boot_progress_set_phase_finder(void) {
	if (!g_seen_finder) {
		g_seen_finder = true;
		g_finder_ready_time = elapsed_sec();
		milestone("Finder detected (CurApName PPC fallback)");
		set_phase(PHASE_FINDER_LAUNCH);
	}
}

/* In-process cached state for /api/status */
static char g_mac_state_cache[2048] = "{}";
static int g_mac_state_tick = 0;

/*
 * Export Mac OS state to IPC buffer or in-process cache.
 * Called at 60Hz; only rebuilds every 30 ticks (~500ms).
 */
void boot_progress_export_mac_state(void)
{
	if (++g_mac_state_tick < 30) return;
	g_mac_state_tick = 0;

	if (g_ipc_buf) {
		serialize_mac_state(g_ipc_buf->mac_state_json,
		                    sizeof(g_ipc_buf->mac_state_json));
	} else {
		serialize_mac_state(g_mac_state_cache, sizeof(g_mac_state_cache));
	}
}

const char* boot_progress_get_mac_state(void)
{
	return g_mac_state_cache;
}
