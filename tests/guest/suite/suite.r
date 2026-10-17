/* suite.r — MacPhoenixSuite's resources (build.mpw): the 68k application
 * (CODE and the rest, from Link), the PowerPC fragment's 'cfrg' and the SIZE. */

#include "CodeFragmentTypes.r"
#include "Types.r"

include "MacPhoenixSuite.68k";

resource 'cfrg' (0) {
    {
        kPowerPC, kFullLib, kNoVersionNum, kNoVersionNum,
        kDefaultStackSize, kNoAppSubFolder,
        kIsApp, kOnDiskFlat, kZeroOffset, kWholeFork,
        "MacPhoenixSuite"
    }
};

#include "size.r"
