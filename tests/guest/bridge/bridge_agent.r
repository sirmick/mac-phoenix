/* Override default SIZE resource to declare high-level event awareness.
 * Without isHighLevelEventAware, AESend from this app returns -903
 * (noSessionErr) because the AE Manager refuses to open an IPC session. */
#include "Types.r"
#include "Processes.r"
#include "Finder.r"

/* canBackground: BridgeAgent polls for commands while another app is front.
 * Without this, Process Manager only schedules us when we're frontmost, so
 * bridge commands stall whenever the user (or a test) brings another app
 * up. */
resource 'SIZE' (-1, purgeable) {
	reserved,
	ignoreSuspendResumeEvents,
	reserved,
	canBackground,
	needsActivateOnFGSwitch,
	backgroundAndForeground,
	dontGetFrontClicks,
	ignoreChildDiedEvents,
	is32BitCompatible,
	isHighLevelEventAware,
	onlyLocalHLEvents,
	notStationeryAware,
	dontUseTextEditServices,
	reserved,
	reserved,
	reserved,
	1024 * 1024,
	1024 * 1024
};

/* Icon family — generated from tools/icons/bridge-{32,16}.png by
 * tools/png2icn.py. */
#include "icons.r"

/* BNDL + FREF + creator signature so Finder pairs the ICN# with this
 * app. Creator 'MxBA' is distinct from MacBrowser's 'MxBr' so the
 * two apps get different icons in the Finder Desktop Database. */
resource 'BNDL' (128) {
	'MxBA', 0,
	{
		'FREF', { 0, 128 },
		'ICN#', { 0, 128 }
	}
};

resource 'FREF' (128) {
	'APPL', 0, ""
};

type 'MxBA' as 'STR ';
resource 'MxBA' (0, "Owner resource") {
	"BridgeAgent — MacPhoenix automation. © 2026 mac-phoenix"
};
