/* Copyright 1994, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>

#include <MemoryMgr.h>
#include <mman/mman.h>
#include <algorithm>

using namespace Executor;

/* #define TEMP_MEM_FAIL */
#define paramErr (-50)

int32_t Executor::C_TempFreeMem()
{
#if defined(TEMP_MEM_FAIL)
    return 0;
#else
    int32_t sysfree, applfree, retval;

    {
        TheZoneGuard guard(LM(ApplZone));

        applfree = FreeMem();
    }
    sysfree = FreeMemSys();
    retval = std::max(applfree, sysfree);
    return retval;
#endif
}

Size Executor::C_TempMaxMem(GUEST<Size> *grow_s)
{
#if defined(TEMP_MEM_FAIL)
    return 0;
#else
    int32_t sysfree, applmax, retval;
    
    {
        TheZoneGuard guard(LM(ApplZone));

        GUEST<Size> tmp;
        applmax = MaxMem(grow_s ? grow_s : &tmp);
    }
    sysfree = FreeMemSys();
    retval = std::max(applmax, sysfree);
    return retval;
#endif
}

Ptr Executor::C_TempTopMem()
{
    warning_unimplemented("");
    return nullptr;
}

Handle Executor::C_TempNewHandle(Size logical_size, GUEST<OSErr> *result_code)
{
#if defined(TEMP_MEM_FAIL)
    *result_code = memFullErr;
    return nullptr;
#else
    {
        Handle retval;

        TheZoneGuard guard(LM(ApplZone));
        if(FreeMemSys() >= FreeMem())
            LM(TheZone) = LM(SysZone);

        retval = NewHandle(logical_size);
        if(result_code)
            *result_code = LM(MemErr);
        return retval;
    }
#endif
}

void Executor::C_TempHLock(Handle h, GUEST<OSErr> *result_code)
{
    HLock(h);
    *result_code = LM(MemErr);
}

void Executor::C_TempHUnlock(Handle h, GUEST<OSErr> *result_code)
{
    HUnlock(h);
    *result_code = LM(MemErr);
}

void Executor::C_TempDisposeHandle(Handle h, GUEST<OSErr> *result_code)
{
    DisposeHandle(h);
    *result_code = LM(MemErr);
}
