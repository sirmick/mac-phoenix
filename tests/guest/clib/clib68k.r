/* clib68k.r — CLibTest68K's resources: an application 'cfrg' for the flat
   CFM-68K fragment in the data fork (MakeFlat's), the CFM launch stub from
   the segmented link (build.mpw), and the SIZE. */

#include "CodeFragmentTypes.r"
#include "Types.r"

resource 'cfrg' (0) {
    {
        kMotorola, kFullLib, kNoVersionNum, kNoVersionNum,
        kDefaultStackSize, kNoAppSubFolder,
        kIsApp, kOnDiskFlat, kZeroOffset, kWholeFork,
        "CLibTest68K"
    }
};

include "CLibTest68K.seg" 'CODE' (0);
include "CLibTest68K.seg" 'CODE' (6);

resource 'SIZE' (-1) {
    reserved, acceptSuspendResumeEvents, reserved, canBackground, doesActivateOnFGSwitch,
    backgroundAndForeground, dontGetFrontClicks, ignoreChildDiedEvents, is32BitCompatible,
    isHighLevelEventAware, onlyLocalHLEvents, notStationeryAware, dontUseTextEditServices,
    reserved, reserved, reserved, 1024 * 1024, 768 * 1024
};
