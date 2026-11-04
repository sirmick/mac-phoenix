/* mem.c — memory: BlockMove of 1 KB, 64 KB and 1 MB (32 MB moved each),
 * memcpy of 64 KB (32 MB), a strided walk over 1 MB. */

#include "bench.h"

#include <Memory.h>
#include <string.h>

#pragma segment Mem

enum { kMemSize = 1024L * 1024, kMoved = 32L * 1024 * 1024 };
static char *mem_a, *mem_b;

static long mem_setup(void)
{
    long i;

    mem_a = NewPtr(kMemSize);
    mem_b = NewPtr(kMemSize);
    if(!mem_a || !mem_b)
    {
        if(mem_a)
            DisposePtr(mem_a);
        bench_skip("no memory");
        return 1;
    }
    for(i = 0; i < kMemSize; i++)
        mem_a[i] = (char)i;
    return 0;
}

static long mem_done(void)
{
    DisposePtr(mem_a);
    DisposePtr(mem_b);
    return 0;
}

static long moves(long size)
{
    long n, off;

    for(n = 0; n < kMoved / size; n++)
    {
        off = (n * size) % kMemSize;
        BlockMove(mem_a + off, mem_b + off, size);
    }
    return mem_b[size - 1];
}

static long blockmove_1k(void)
{
    return moves(1024);
}

static long blockmove_64k(void)
{
    return moves(65536);
}

static long blockmove_1m(void)
{
    return moves(kMemSize);
}

static long memcpy_64k(void)
{
    long n, off;

    for(n = 0; n < kMoved / 65536; n++)
    {
        off = (n * 65536) % kMemSize;
        memcpy(mem_b + off, mem_a + off, 65536);
    }
    return mem_b[65535];
}

/* every 64th byte of 1 MB, 16 times, each pass at another offset */
static long stride(void)
{
    long sum = 0, i;
    int pass;

    for(pass = 0; pass < 16; pass++)
        for(i = pass; i < kMemSize; i += 64)
            sum += mem_a[i];
    return sum;
}

void add_mem_tests(void)
{
    bench_add("blockmove_1k", "32MB", blockmove_1k, mem_setup, mem_done);
    bench_add("blockmove_64k", "32MB", blockmove_64k, mem_setup, mem_done);
    bench_add("blockmove_1m", "32MB", blockmove_1m, mem_setup, mem_done);
    bench_add("memcpy_64k", "32MB", memcpy_64k, mem_setup, mem_done);
    bench_add("stride", "1MB/64x16", stride, mem_setup, mem_done);
}
