/* MacPhoenix: ResourceDispatch selectors that are private in System 7.5.5.
 *
 * Written from docs/executor/cleanroom/resource-private-selectors.md (the
 * clean-room spec); the names are ours. A map's "attribute byte" is the
 * low byte of Executor's resfatr word (the map's attributes as
 * GetResFileAttrs returns them: read-only $80, compact $40, changed $20,
 * the low bits private).
 */

#include <base/common.h>
#include <ResourceMgr.h>
#include <res/resource.h>
#include <rsys/process.h>

using namespace Executor;

namespace
{
enum
{
    private_flags_mask = 0x1E, /* bits 1-4 */
    override_map_bit = 0x02,
};

inline uint8_t attribute_byte(resmaphand map)
{
    return (uint8_t)(*map)->resfatr;
}
}

/* Selector 0: the map of an open resource file, 0 meaning the System. */
Handle Executor::C_GetResourceMapHandle(INTEGER refNum)
{
    if(refNum == 0)
        refNum = LM(SysMap);
    resmaphand map = ROMlib_rntohandl(refNum, nullptr);
    ROMlib_setreserr(map ? noErr : resFNotFound);
    return (Handle)map;
}

/* Selector 8: the map's private attribute bits. */
SignedByte Executor::C_GetResourceMapPrivateFlags(Handle map)
{
    if(!map)
    {
        ROMlib_setreserr(nilHandleErr);
        return 0;
    }
    ROMlib_setreserr(noErr);
    return (SignedByte)(attribute_byte((resmaphand)map) & private_flags_mask);
}

/* Selector 6: the topmost of the run of override maps directly above
   `map` in the chain, or `map` itself when the map above isn't one. */
Handle Executor::C_GetOverrideMap(Handle map)
{
    resmaphand run_start = nullptr;
    resmaphand m;
    WALKMAPTOP(m)
    if((Handle)m == map)
    {
        ROMlib_setreserr(noErr);
        return run_start ? (Handle)run_start : map;
    }
    if(attribute_byte(m) & override_map_bit)
    {
        if(!run_start)
            run_start = m;
    }
    else
        run_start = nullptr;
    EWALKMAP()

    ROMlib_setreserr(resFNotFound);
    return nullptr;
}

/* Selector -1: whether a map is the System file's or a file's opened in
   system mode. */
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
