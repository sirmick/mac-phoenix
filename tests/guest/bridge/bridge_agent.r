/* Override default SIZE resource to declare high-level event awareness.
 * Without isHighLevelEventAware, AESend from this app returns -903
 * (noSessionErr) because the AE Manager refuses to open an IPC session. */
#include "Processes.r"

resource 'SIZE' (-1, purgeable) {
	reserved,
	ignoreSuspendResumeEvents,
	reserved,
	cannotBackground,
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
