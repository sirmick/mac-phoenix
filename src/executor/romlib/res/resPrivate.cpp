/* MacPhoenix: ResourceDispatch selectors that are private in System 7.5.5.
 *
 * To be implemented from docs/executor/cleanroom/resource-private-selectors.md.
 */

#include <base/common.h>
#include <ResourceMgr.h>
#include <res/resource.h>
#include <rsys/process.h>

using namespace Executor;

Handle Executor::C_GetResourceMapHandle(INTEGER refNum)
{
    (void)refNum;
    ROMlib_setreserr(resFNotFound);
    return nullptr;
}

SignedByte Executor::C_GetResourceMapPrivateFlags(Handle map)
{
    (void)map;
    ROMlib_setreserr(resFNotFound);
    return 0;
}

Handle Executor::C_GetOverrideMap(Handle map)
{
    (void)map;
    ROMlib_setreserr(resFNotFound);
    return nullptr;
}

Boolean Executor::C_ResourceMapIsSystem(Handle map)
{
    (void)map;
    ROMlib_setreserr(resFNotFound);
    return false;
}
