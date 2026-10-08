/* MacPhoenix: ResourceDispatch selectors that are private in System 7.5.5.
 * Identified from a traced real boot and Apple's code (tools/macdecode:
 * atraps.py, ghidra.py); names are best guesses, see learned.yaml. */

#include <base/common.h>
#include <ResourceMgr.h>
#include <res/resource.h>

using namespace Executor;

/* Selector 0: the map handle of an open resource file (0 = System file).
   Apple's walks the chain from TopMapHndl comparing each map's refnum. */
Handle Executor::C_GetResourceMapHandle(INTEGER refNum)
{
    if(refNum == 0)
        refNum = LM(SysMap);
    resmaphand map = ROMlib_rntohandl(refNum, nullptr);
    ROMlib_setreserr(map ? noErr : resFNotFound);
    return (Handle)map;
}
