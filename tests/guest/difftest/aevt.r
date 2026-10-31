/* aevt: the Retro68 default SIZE (-1) with high-level events asked for,
   as SIZE 0 (the one the Process Manager takes first): the launch's 'oapp'
   comes to the probe as an Apple event. */
#include "Processes.r"

resource 'SIZE' (0) {
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
