/* cpu.c — integer work: a Dhrystone-like loop, a sieve, a sort, CRC-32. */

#include "bench.h"

#include <Memory.h>
#include <string.h>

#pragma segment Cpu

/* ---- dhry: Dhrystone 2.1's kinds of statement (record copies, string
 * copies and compares, array stores, calls, small branches), not its
 * exact program ---- */

enum { kDhryLoops = 20000 };

typedef struct Rec
{
    struct Rec *next;
    int kind, value;
    char str[31];
} Rec;

static Rec rec_a, rec_b;
static int arr1[50], arr2[50][50];
static char str1[31], str2[31];
static int glob_int;
static char glob_ch;

static int func1(char a, char b)
{
    return a == b ? 0 : 1;
}

static int func2(const char *a, const char *b)
{
    int i = 2;
    char c = 'A';

    while(i <= 2)
        if(func1(a[i], b[i + 1]) == 1)
        {
            c = 'A';
            i++;
        }
    if(c >= 'W' && c < 'Z')
        i = 7;
    if(c == 'R')
        return 1;
    if(strcmp(a, b) > 0)
    {
        glob_int = i + 7;
        return 1;
    }
    return 0;
}

static void proc7(int a, int b, int *out)
{
    *out = b + a + 2;
}

static void proc8(int *a1, int (*a2)[50], int x, int y)
{
    int i, loc = x + 5;

    a1[loc] = y;
    a1[loc + 1] = a1[loc];
    a1[loc + 30] = loc;
    for(i = loc; i <= loc + 1; i++)
        a2[loc][i] = loc;
    a2[loc][loc - 1] += 1;
    a2[loc + 20][loc] = a1[loc];
    glob_int = 5;
}

static void proc3(Rec **p)
{
    if(rec_a.next)
        *p = rec_a.next;
    proc7(10, glob_int, &rec_a.value);
}

static void proc1(Rec *p)
{
    Rec *n = p->next;

    *n = rec_a;
    p->value = 5;
    n->value = p->value;
    n->next = p->next;
    proc3(&n->next);
    if(n->kind == 0)
    {
        n->value = 6;
        proc7(n->value, 10, &n->value);
    }
    else
        *p = *n;
}

static long dhry(void)
{
    long run;
    int i1, i2, i3;

    rec_a.next = &rec_b;
    rec_a.kind = 0;
    rec_a.value = 40;
    strcpy(rec_a.str, "DHRYSTONE PROGRAM, SOME STRING");
    strcpy(str1, "DHRYSTONE PROGRAM, 1'ST STRING");
    for(run = 0; run < kDhryLoops; run++)
    {
        glob_ch = 'A';
        i1 = 2;
        i2 = 3;
        strcpy(str2, "DHRYSTONE PROGRAM, 2'ND STRING");
        glob_int = !func2(str1, str2);
        while(i1 < i2)
        {
            i3 = 5 * i1 - i2;
            proc7(i1, i2, &i3);
            i1++;
        }
        proc8(arr1, arr2, i1, i3);
        proc1(&rec_a);
        for(glob_ch = 'A'; glob_ch <= 'B'; glob_ch++)
            if(func1(glob_ch, 'C') == 0)
                i3 = run;
        i2 = i2 * i1;
        i1 = i2 / i3;
        i2 = 7 * (i2 - i3) - i1;
    }
    return glob_int + i1 + i2 + rec_a.value;
}

/* ---- sieve: the primes below 8192, 20 times ---- */

static char flags[8192];

static long sieve(void)
{
    long count = 0;
    int iter, i, k;

    for(iter = 0; iter < 20; iter++)
    {
        count = 0;
        for(i = 0; i < 8192; i++)
            flags[i] = 1;
        for(i = 2; i < 8192; i++)
            if(flags[i])
            {
                for(k = i + i; k < 8192; k += i)
                    flags[k] = 0;
                count++;
            }
    }
    return count;
}

/* ---- sort: quicksort of 20,000 longs (the same pseudo-random ones each
 * run) ---- */

enum { kSortN = 20000 };
static long *sort_v;

static long sort_setup(void)
{
    if((sort_v = (long *)NewPtr(kSortN * sizeof(long))) == NULL)
    {
        bench_skip("no memory");
        return 1;
    }
    return 0;
}

static long sort_done(void)
{
    DisposePtr((Ptr)sort_v);
    return 0;
}

static void quick(long *v, long lo, long hi)
{
    long i, j, p, t;

    while(lo < hi)
    {
        p = v[(lo + hi) / 2];
        i = lo;
        j = hi;
        while(i <= j)
        {
            while(v[i] < p)
                i++;
            while(v[j] > p)
                j--;
            if(i <= j)
            {
                t = v[i], v[i] = v[j], v[j] = t;
                i++, j--;
            }
        }
        if(j - lo < hi - i)
        {
            quick(v, lo, j);
            lo = i;
        }
        else
        {
            quick(v, i, hi);
            hi = j;
        }
    }
}

static long sort(void)
{
    unsigned long seed = 12345;
    long i;

    for(i = 0; i < kSortN; i++)
    {
        seed = seed * 1103515245ul + 12345;
        sort_v[i] = (long)(seed >> 8);
    }
    quick(sort_v, 0, kSortN - 1);
    return sort_v[kSortN / 2];
}

/* ---- crc32: over 1 MB, table-driven ---- */

enum { kCrcSize = 1024L * 1024 };
static unsigned long crc_table[256];
static unsigned char *crc_buf;

static long crc_setup(void)
{
    unsigned long c;
    long i;
    int k;

    if((crc_buf = (unsigned char *)NewPtr(kCrcSize)) == NULL)
    {
        bench_skip("no memory");
        return 1;
    }
    for(i = 0; i < 256; i++)
    {
        c = i;
        for(k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320ul ^ (c >> 1) : c >> 1;
        crc_table[i] = c;
    }
    for(i = 0; i < kCrcSize; i++)
        crc_buf[i] = (unsigned char)(i * 7 + (i >> 8));
    return 0;
}

static long crc_done(void)
{
    DisposePtr((Ptr)crc_buf);
    return 0;
}

static long crc32(void)
{
    unsigned long c = 0xFFFFFFFFul;
    const unsigned char *p = crc_buf, *end = crc_buf + kCrcSize;

    while(p < end)
        c = crc_table[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return (long)~c;
}

void add_cpu_tests(void)
{
    bench_add("dhry", "20000_loops", dhry, NULL, NULL);
    bench_add("sieve", "8192x20", sieve, NULL, NULL);
    bench_add("sort", "20000_longs", sort, sort_setup, sort_done);
    bench_add("crc32", "1MB", crc32, crc_setup, crc_done);
}
