/* MacPhoenix: what is at a guest address. Names the heap zone, the block
 * (kind, start, size, offset into it) and, for a resource handle, its type,
 * ID and the file it came from; low memory and unknown places by name.
 * For reports: the allocation log (--executor-debug allocs), --watch, the
 * fatal reports. */
#include <base/common.h>
#include <MemoryMgr.h>
#include <ResourceMgr.h>
#include <SegmentLdr.h>
#include <mman/mman_private.h>
#include <res/resource.h>
#include <rsys/process.h>
#include <mman/whatis.h>

#include <cstdio>

using namespace Executor;

namespace
{
const char *zone_label(THz z, char *buf, size_t n)
{
    if(z == LM(SysZone))
        return "SysZone";
    if(z == LM(ApplZone))
    {
        snprintf(buf, n, "ApplZone (%.*s)", LM(CurApName)[0], (const char *)&LM(CurApName)[1]);
        return buf;
    }
    if(z == ROMlib_pm_zone)
        return "Process Manager heap";
    snprintf(buf, n, "zone $%08X", (unsigned)US_TO_SYN68K(z));
    return buf;
}

/* The block of zone holding p, or null. */
block_header_t *block_at(THz zone, const char *p)
{
    if(!zone)
        return nullptr;
    const char *lo = (const char *)ZONE_HEAP_DATA(zone);
    const char *hi = (const char *)ZONE_BK_LIM(zone);
    if(p < lo || p >= hi)
        return nullptr;
    for(block_header_t *b = ZONE_HEAP_DATA(zone); b < ZONE_BK_LIM(zone); b = BLOCK_NEXT(b))
    {
        if(PSIZE(b) < MIN_BLOCK_SIZE || USE(b) > 2)
            return nullptr;
        if(p >= (const char *)b && p < (const char *)b + PSIZE(b))
            return b;
    }
    return nullptr;
}
}

void Executor::ROMlib_whatis(uint32_t guest, char *buf, size_t n)
{
    if(guest < 0x2800)
    {
        snprintf(buf, n, "low memory $%04X", guest);
        return;
    }
    if(!syn68k_is_guest_mapped((uintptr_t)SYN68K_TO_US(guest), 1))
    {
        snprintf(buf, n, "unmapped");
        return;
    }
    const char *p = (const char *)SYN68K_TO_US(guest);
    /* Innermost first: the application's zone lives in the PM heap. */
    THz zones[] = { LM(ApplZone), LM(SysZone), ROMlib_pm_zone };
    for(THz z : zones)
    {
        block_header_t *b = block_at(z, p);
        if(!b)
            continue;
        char zl[64];
        const char *zname = zone_label(z, zl, sizeof zl);
        uint32_t data = US_TO_SYN68K(BLOCK_DATA(b));
        int32_t off = (int32_t)(guest - data);
        if(USE(b) == FREE)
        {
            snprintf(buf, n, "%s, free block $%08X", zname, data);
            return;
        }
        if(USE(b) == NREL)
        {
            snprintf(buf, n, "%s, pointer $%08X+$%X (%u bytes)", zname, data, off,
                     (unsigned)(PSIZE(b) - HDRSIZE));
            return;
        }
        Handle h = BLOCK_TO_HANDLE(z, b);
        resmaphand map;
        typref *tr;
        resref *rr;
        if(ROMlib_findres(h, &map, &tr, &rr) == noErr)
        {
            ResType t = tr->rtyp;
            snprintf(buf, n, "%s, resource '%c%c%c%c' %d of file %d, $%08X+$%X (%u bytes%s)",
                     zname, (char)(t >> 24), (char)(t >> 16), (char)(t >> 8), (char)t,
                     (int)rr->rid, (int)(*map)->resfn, data, off,
                     (unsigned)(PSIZE(b) - HDRSIZE),
                     (BLOCK_STATE(b) & LOCKBIT) ? ", locked" : "");
            return;
        }
        snprintf(buf, n, "%s, handle $%08X -> $%08X+$%X (%u bytes%s)", zname,
                 (unsigned)US_TO_SYN68K(h), data, off, (unsigned)(PSIZE(b) - HDRSIZE),
                 (BLOCK_STATE(b) & LOCKBIT) ? ", locked" : "");
        return;
    }
    snprintf(buf, n, "outside the heaps");
}
