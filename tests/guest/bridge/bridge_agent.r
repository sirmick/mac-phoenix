/* Override default SIZE resource to declare high-level event awareness.
 * Without isHighLevelEventAware, AESend from this app returns -903
 * (noSessionErr) because the AE Manager refuses to open an IPC session. */
#include "Processes.r"

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
