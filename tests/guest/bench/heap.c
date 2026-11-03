/* heap.c — the Memory Manager: NewHandle + DisposeHandle 10,000 times (in
 * tens of 1,000 handles of 16 to 1,024 bytes), HLock + HUnlock and MoveHHi
 * of 1,000 handles, a compaction (every other handle of 2,000 freed,
 * CompactMem), 5 times. */

#include "bench.h"

#include <Memory.h>

#pragma segment Heap

enum { kHandles = 2000 };
static Handle *hs;

static long heap_setup(void)
{
    if((hs = (Handle *)NewPtrClear(kHandles * sizeof(Handle))) == NULL)
    {
        bench_skip("no memory");
        return 1;
    }
    return 0;
}

static long heap_done(void)
{
    int i;

    for(i = 0; i < kHandles; i++)
        if(hs[i])
            DisposeHandle(hs[i]);
    DisposePtr((Ptr)hs);
    return 0;
}

static long newhandle(void)
{
    int round, i;
    long sum = 0;

    for(round = 0; round < 10; round++)
    {
        for(i = 0; i < 1000; i++)
            hs[i] = NewHandle(16 + (i * 37) % 1009);
        for(i = 999; i >= 0; i--)
            if(hs[i])
            {
                sum++;
                DisposeHandle(hs[i]);
                hs[i] = NULL;
            }
    }
    return sum;
}

static long lockmove_setup(void)
{
    int i;

    if(heap_setup())
        return 1;
    for(i = 0; i < 1000; i++)
        hs[i] = NewHandle(64 + i % 256);
    return 0;
}

static long lockmove(void)
{
    int i;

    for(i = 0; i < 1000; i++)
        if(hs[i])
        {
            HLock(hs[i]);
            HUnlock(hs[i]);
            MoveHHi(hs[i]);
        }
    return 0;
}

static long compact(void)
{
    int round, i;
    long got = 0;

    for(round = 0; round < 5; round++)
    {
        for(i = 0; i < kHandles; i++)
            hs[i] = NewHandle(200 + (i * 13) % 300);
        for(i = 0; i < kHandles; i += 2)
            if(hs[i])
            {
                DisposeHandle(hs[i]);
                hs[i] = NULL;
            }
        got += CompactMem(maxSize);
        for(i = 1; i < kHandles; i += 2)
            if(hs[i])
            {
                DisposeHandle(hs[i]);
                hs[i] = NULL;
            }
    }
    return got;
}

void add_heap_tests(void)
{
    bench_add("newhandle", "10000", newhandle, heap_setup, heap_done);
    bench_add("lockmove", "1000", lockmove, lockmove_setup, heap_done);
    bench_add("compact", "5x2000", compact, heap_setup, heap_done);
}
