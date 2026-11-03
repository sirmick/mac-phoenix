/* bench.r — MacPhoenixBench's resources (build.mpw): the 68k application
 * (CODE and the rest, from Link), the PowerPC fragment's 'cfrg' and the SIZE. */

#include "CodeFragmentTypes.r"
#include "Types.r"

include "MacPhoenixBench.68k";

resource 'cfrg' (0) {
    {
        kPowerPC, kFullLib, kNoVersionNum, kNoVersionNum,
        kDefaultStackSize, kNoAppSubFolder,
        kIsApp, kOnDiskFlat, kZeroOffset, kWholeFork,
        "MacPhoenixBench"
    }
};

resource 'SIZE' (-1) {
    reserved, acceptSuspendResumeEvents, reserved, canBackground, doesActivateOnFGSwitch,
    backgroundAndForeground, dontGetFrontClicks, ignoreChildDiedEvents, is32BitCompatible,
    isHighLevelEventAware, onlyLocalHLEvents, notStationeryAware, dontUseTextEditServices,
    reserved, reserved, reserved, 8 * 1024 * 1024, 6 * 1024 * 1024
};
