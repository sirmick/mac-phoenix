/* MacPhoenix: ResourceDispatch selectors that are private in System 7.5.5.
 * Identified from a traced real boot and Apple's code (tools/macdecode:
 * atraps.py, ghidra.py); names are best guesses, see learned.yaml. */

#include <base/common.h>
#include <ResourceMgr.h>
#include <res/resource.h>
#include <rsys/process.h>

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

SignedByte Executor::C_GetResourceMapPrivateFlags(Handle map)
{
    if(!map)
    {
        ROMlib_setreserr(-109); /* nilHandleErr */
        return 0;
    }
    ROMlib_setreserr(noErr);
    return (*(resmaphand)map)->resfatr & 0x1E;
}

/* Selector 6: the override maps (attribute bit 1, inserted above their
   base map by selector 5) that sit directly on top of a map; the topmost
   of them, or the map itself without any. Apple's walks from TopMapHndl
   remembering the first override map of the current run. */
Handle Executor::C_GetOverrideMap(Handle map)
{
    ROMlib_setreserr(noErr);
    Handle run = nullptr;
    for(Handle h = LM(TopMapHndl); h; h = (*(resmaphand)h)->nextmap)
    {
        if(h == map)
            return run ? run : map;
        if((*(resmaphand)h)->resfatr & 0x02)
        {
            if(!run)
                run = h;
        }
        else
            run = nullptr;
    }
    ROMlib_setreserr(resFNotFound);
    return nullptr;
}

/* Selector -1: does a resource map belong to the system? True of the
   System file's map and of a file opened in system mode (Apple's tests
   two longs in the map's header record, through its attribute word);
   ResErr is resFNotFound for nil. Apple's Help Manager asks of the file a
   balloon's resource came from. */
Boolean Executor::C_ResourceMapIsSystem(Handle map)
{
    if(!map)
    {
        ROMlib_setreserr(resFNotFound);
        return false;
    }
    ROMlib_setreserr(noErr);
    if(map == LM(SysMapHndl))
        return true;
    return ROMlib_system_resource_file_p((*(resmaphand)map)->resfn);
}
