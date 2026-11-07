#include "Processes.r"

/* SIZE 0 and SIZE -1 differ so that a test can tell which the Process
   Manager goes by (Process.PartitionFromSizeZero: SIZE 0's). */
resource 'SIZE' (0) {
	dontSaveScreen,
	ignoreSuspendResumeEvents,
	enableOptionSwitch,
	cannotBackground,
	needsActivateOnFGSwitch,
	backgroundAndForeground,
	dontGetFrontClicks,
	ignoreChildDiedEvents,
	is32BitCompatible,
	reserved,
	reserved,
	reserved,
	reserved,
	reserved,
	reserved,
	reserved,
	3072 * 1024,
	2048 * 1024
};

resource 'SIZE' (-1) {
	dontSaveScreen,
	ignoreSuspendResumeEvents,
	enableOptionSwitch,
	cannotBackground,
	needsActivateOnFGSwitch,
	backgroundAndForeground,
	dontGetFrontClicks,
	ignoreChildDiedEvents,
	is32BitCompatible,
	reserved,
	reserved,
	reserved,
	reserved,
	reserved,
	reserved,
	reserved,
	4096 * 1024,
	2048 * 1024
};
