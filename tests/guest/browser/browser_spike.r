/* SIZE resource: small heap, no special flags needed. The spike is a
 * foreground app — it doesn't need canBackground or HLE awareness. */
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
	notHighLevelEventAware,
	onlyLocalHLEvents,
	notStationeryAware,
	dontUseTextEditServices,
	reserved,
	reserved,
	reserved,
	/* sizeof(BrowserShm) ≈ 1.7 MiB plus headroom for Toolbox + heap. */
	4 * 1024 * 1024,
	4 * 1024 * 1024
};
