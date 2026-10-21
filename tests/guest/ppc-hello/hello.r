/* hello.r — PPCHello's resources: the fragment's 'cfrg' and the SIZE. */

#include "CodeFragmentTypes.r"
#include "Types.r"

resource 'cfrg' (0) {
    {
        kPowerPC, kFullLib, kNoVersionNum, kNoVersionNum,
        kDefaultStackSize, kNoAppSubFolder,
        kIsApp, kOnDiskFlat, kZeroOffset, kWholeFork,
        "PPCHello"
    }
};

resource 'SIZE' (-1) {
    reserved, acceptSuspendResumeEvents, reserved, canBackground, doesActivateOnFGSwitch,
    backgroundAndForeground, dontGetFrontClicks, ignoreChildDiedEvents, is32BitCompatible,
    isHighLevelEventAware, onlyLocalHLEvents, notStationeryAware, dontUseTextEditServices,
    reserved, reserved, reserved, 512 * 1024, 384 * 1024
};
