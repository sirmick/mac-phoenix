/* Copyright 1986-1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in TextEdit.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <TextEdit.h>
#include <MemoryMgr.h>
#include <ScrapMgr.h>

#include <quickdraw/cquick.h>
#include <mman/mman.h>

using namespace Executor;

OSErr Executor::TEFromScrap()
{
    GUEST<int32_t> l;
    int32_t m;

    m = GetScrap(LM(TEScrpHandle), "TEXT"_4, &l);
    if(m < 0)
    {
        EmptyHandle(LM(TEScrpHandle));
        LM(TEScrpLength) = 0;
    }
    else
        LM(TEScrpLength) = m;
    return m < 0 ? m : noErr;
}

OSErr Executor::TEToScrap()
{
    int32_t m;

    HLockGuard guard(LM(TEScrpHandle));

    m = PutScrap(LM(TEScrpLength), "TEXT"_4,
                 *LM(TEScrpHandle));
    return m < 0 ? m : 0;
}

Handle Executor::TEScrapHandle()
{
    return LM(TEScrpHandle);
}

int32_t Executor::TEGetScrapLength()
{
    return LM(TEScrpLength);
}

void Executor::TESetScrapLength(int32_t ln)
{
    LM(TEScrpLength) = ln;
}
