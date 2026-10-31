#include "Processes.r"

resource 'STR#' (128) {
    { "alpha"; "beta, the second" }
};

resource 'STR ' (129, "CGlue named") {
    "named"
};

resource 'SIZE' (-1) {
    reserved, acceptSuspendResumeEvents, reserved, canBackground, doesActivateOnFGSwitch,
    backgroundAndForeground, dontGetFrontClicks, ignoreChildDiedEvents, is32BitCompatible,
    notHighLevelEventAware, onlyLocalHLEvents, notStationeryAware, dontUseTextEditServices,
    reserved, reserved, reserved, 1024 * 1024, 1024 * 1024 };
