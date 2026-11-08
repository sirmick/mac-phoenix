/* qttest.r — QTTest's resources (build.mpw): the 68k application (CODE
 * and the rest, from Link), the PowerPC fragment's 'cfrg' and the SIZE. */

#include "CodeFragmentTypes.r"
#include "Types.r"

include "QTTest.68k";

resource 'cfrg' (0) {
    {
        kPowerPC, kFullLib, kNoVersionNum, kNoVersionNum,
        kDefaultStackSize, kNoAppSubFolder,
        kIsApp, kOnDiskFlat, kZeroOffset, kWholeFork,
        "QTTest"
    }
};

resource 'SIZE' (-1) {
    reserved, acceptSuspendResumeEvents, reserved, canBackground, doesActivateOnFGSwitch,
    backgroundAndForeground, dontGetFrontClicks, ignoreChildDiedEvents, is32BitCompatible,
    isHighLevelEventAware, onlyLocalHLEvents, notStationeryAware, dontUseTextEditServices,
    reserved, reserved, reserved, 6 * 1024 * 1024, 4 * 1024 * 1024
};
