/* Copyright 1994, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>

#include <MemoryMgr.h>
#include <mman/mman.h>
#include <rsys/process.h>
#include <algorithm>

using namespace Executor;

/* MacPhoenix: as in System 7, temporary memory comes from the Process
   Manager heap (the free space outside every partition), not from the
   application or System heap. Without a PM heap, Executor's old rule:
   whichever of ApplZone and SysZone has more free space. */

/* #define TEMP_MEM_FAIL */
#define paramErr (-50)

int32_t Executor::C_TempFreeMem()
{
#if defined(TEMP_MEM_FAIL)
    return 0;
#else
    int32_t sysfree, applfree, retval;

    if(ROMlib_pm_zone)
    {
        TheZoneGuard guard(ROMlib_pm_zone);
        return FreeMem();
    }
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

    if(ROMlib_pm_zone)
    {
        TheZoneGuard guard(ROMlib_pm_zone);
        GUEST<Size> tmp;
        return MaxMem(grow_s ? grow_s : &tmp);
    }
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
        if(ROMlib_pm_zone)
            LM(TheZone) = ROMlib_pm_zone;
        else if(FreeMemSys() >= FreeMem())
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

/* MacPhoenix: _HeapDispatch selector $20 (MemoryMgr.yaml): the heap for
   system-wide allocations, the Process Manager's zone. */
THz Executor::C_HeapDispatchSharedZone()
{
    return ROMlib_pm_zone ? ROMlib_pm_zone : (THz)LM(SysZone);
}

/* MacPhoenix: BlockZero, BlockZeroUncached (MemoryMgr.yaml). */
void Executor::C_BlockZero(void *destPtr, Size byteCount)
{
    if(destPtr && byteCount > 0)
        memset(destPtr, 0, byteCount);
}

void Executor::C_BlockZeroUncached(void *destPtr, Size byteCount)
{
    C_BlockZero(destPtr, byteCount);
}
