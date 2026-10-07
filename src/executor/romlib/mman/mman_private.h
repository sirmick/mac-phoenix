
#if !defined(_MMAN_PRIVATE_H_)
#define _MMAN_PRIVATE_H_

/*
 * Copyright 1986, 1989, 1990 by Abacus Research and Development, Inc.
 * All rights reserved.
 *

 */

#include <mman/mman.h>
#include <error/error.h>
namespace Executor
{
/* the bogo new IM books implies (via a picture) that the field
     order is `location, size, flags'; and it says that the 24bit
     header is `location, size'.  but IMII implies that the 24bit
     header order is `size, location'; testing shows this is the
     correct order */
/* or, as mat would like me to say: `testing shows that good things
     are better than bad things' */

/* various flags */

/* data contained in the block */
struct block_header_t
{
    GUEST_STRUCT;
#if defined(MM_BLOCK_HEADER_SENTINEL)
    GUEST<uint8_t[SENTINEL_SIZE]> pre_sentinel;
#endif
    GUEST<uint8_t> flags;
    GUEST<uint8_t> master_ptr_flags;
    GUEST<uint8_t> reserved;
    GUEST<uint8_t> size_correction;
    GUEST<uint32_t> size;
    GUEST<uint32_t> location_u; /* sometimes it's a pointer (the zone),
					   sometimes it's an offset */
#if defined(MM_RECORD_ALLOCATION_STACK_TRACES)
    GUEST<int> alloc_debug_number;
    GUEST<void * [MM_MAX_STACK_TRACE_DEPTH]> alloc_pcs;
#endif
#if defined(MM_BLOCK_HEADER_SENTINEL)
    GUEST<uint8_t[SENTINEL_SIZE]> post_sentinel;
#endif
    GUEST<uint32_t> data[0];
};

#define BLOCK_LOCATION_OFFSET(block) ((block)->location_u)
#define BLOCK_DATA(block) ((Ptr)(block)->data)


#define BLOCK_LOCATION_ZONE(block) (guest_cast<THz>((block)->location_u))
#define BLOCK_DATA(block) ((Ptr)(block)->data)

#define USE(block) (((block)->flags >> 6) & 0x3)
#define PSIZE(block) ((block)->size)
#define SIZEC(block) ((block)->size_correction)

#define SETUSE(block, use) ((block)->flags = (((block)->flags & 0x3F) \
                                              | (use) << 6))
#define SETPSIZE(block, _size) ((block)->size = _size)
#define SETSIZEC(block, sizec) ((block)->size_correction = (sizec))

/* ### fixme; we also need to set the other reserved bits (in the
   flags and master_ptr_flags fields) to zero */
#define SETZERO(block) ((block)->reserved = 0)

#define HEADER_SIZE (sizeof(block_header_t))

/* ### bogo compat */
#define HDRSIZE HEADER_SIZE

extern uintptr_t ROMlib_syszone;
extern uintptr_t ROMlib_memtop;

#define VALID_ADDRESS(p) ((uintptr_t)(p) >= ROMlib_syszone && (uintptr_t)(p) < ROMlib_memtop)

/* handle to block pointer */
#define HANDLE_TO_BLOCK(handle)                          \
    (VALID_ADDRESS(handle) && VALID_ADDRESS(*handle) \
         ? (block_header_t *)((char *)*(handle)      \
                              - HDRSIZE)                 \
         : nullptr)

#define BLOCK_TO_HANDLE(zone, block) \
    ((Handle)((char *)(zone) + BLOCK_LOCATION_OFFSET(block)))

#define BLOCK_TO_POINTER(block) \
    ((Ptr)((char *)(block) + HDRSIZE))

#define POINTER_TO_BLOCK(pointer) \
    ((block_header_t *)((char *)(pointer)-HDRSIZE))

#define FREE_BLOCK_STATE (0x1A)
#define STATE_BITS (LOCKBIT | PURGEBIT | RSRCBIT)
#define EMPTY_STATE (0)
#define MIN_BLOCK_SIZE (12)

#define BLOCK_STATE(block) \
    ((block)->master_ptr_flags)

#define SET_BLOCK_STATE(block, state) \
    ((block)->master_ptr_flags = (state))

#ifdef TWENTYFOUR_BIT_ADDRESSING
/* extract the handle state bits */
#define HANDLE_STATE(handle, block) \
    ((handle)->raw_host_order() >> 24)

#define SET_HANDLE_STATE(handle, block, state) \
    ((handle)->raw_host_order( ((handle)->raw_host_order() & 0xFFFFFF) | ((state) << 24)))

/* set the master pointer of a handle to a given value */
#define SETMASTER(handle, ptr, state) ((handle)->raw_host_order(((state) << 24) | guestvalues::GuestTypeTraits<Ptr>::host_to_reg((Ptr)ptr)))
#else
/* extract the handle state bits */
#define HANDLE_STATE(handle, block) \
    ((block)->master_ptr_flags)

#define SET_HANDLE_STATE(handle, block, state) \
    ((block)->master_ptr_flags = (state))

/* set the master pointer of a handle to a given value */
#define SETMASTER(handle, ptr, state) (*handle = (Ptr)ptr)
#endif

#define BLOCK_NEXT(block) \
    ((block_header_t *)((char *)(block) + PSIZE(block)))

#define LSIZE(block) \
    (PSIZE(block) - SIZEC(block) - HDRSIZE)


#define BLOCK_SET_LOCATION_OFFSET(block, loc) \
    ((block)->location_u = loc)
#define BLOCK_SET_LOCATION_ZONE(block, loc) \
    ((block)->location_u = guest_cast<uint32_t>(loc))
#define BLOCK_SET_RESERVED(block)

/* Zone record accessor macros */
#define ZONE_HEAP_DATA(zone) ((block_header_t *)&(zone)->heapData)

#define ZONE_BK_LIM(zone) (guest_cast<block_header_t *>((zone)->bkLim))
#define ZONE_PURGE_PTR(zone) ((zone)->purgePtr)
#define ZONE_HFST_FREE(zone) ((zone)->hFstFree)
#define ZONE_ZCB_FREE(zone) ((zone)->zcbFree)
#define ZONE_GZ_PROC(zone) ((zone)->gzProc)
#define ZONE_MORE_MAST(zone) ((zone)->moreMast)
#define ZONE_PURGE_PROC(zone) ((zone)->purgeProc)
#define ZONE_ALLOC_PTR(zone) ((zone)->allocPtr)


#define MEM_DEBUG_P() ERROR_ENABLED_P(ERROR_TRAP_FAILURE)

extern OSErr ROMlib_relalloc(Size, block_header_t **);
extern void ROMlib_setupblock(block_header_t *, uint32_t, short, Handle, ...);
extern void ROMlib_freeblock(block_header_t *);
extern bool ROMlib_makespace(block_header_t **, uint32_t);
extern bool ROMlib_locked(block_header_t *);
extern void ROMlib_moveblock(block_header_t *, block_header_t *, uint32_t);
extern int32_t ROMlib_amtfree(block_header_t *);
extern bool ROMlib_pushblock(block_header_t *, block_header_t *);
extern void ROMlib_coalesce(block_header_t *blk);

void mm_set_block_fields_offset(block_header_t *block,
                                unsigned state, unsigned use,
                                unsigned size_correction,
                                uint32_t physical_size, uint32_t location);

void mm_set_block_fields_zone(block_header_t *block,
                              unsigned state, unsigned use,
                              unsigned size_correction,
                              uint32_t physical_size, THz location);

extern void checkallocptr(void);

#if ERROR_SUPPORTED_P(ERROR_MEMORY_MANAGER_SLAM)

typedef struct
{
    int32_t n_rel;
    int32_t n_nrel;
    int32_t n_free;
    int32_t largest_free;
    int32_t total_free;
} zone_info_t;

typedef Zone *ZonePtr;

struct pblock_t
{
    GUEST_STRUCT;
    GUEST<ZonePtr> sp;
    GUEST<Ptr> lp;
    GUEST<INTEGER> mm;
    GUEST<ProcPtr> gz;
};

extern void ROMlib_sledgehammer_zone(THz zone, bool print_p,
                                     const char *fn, const char *file,
                                     int lineno, const char *where,
                                     zone_info_t *infop);
extern void ROMlib_sledgehammer_zones(const char *fn,
                                      const char *file, int lineno,
                                      const char *where,
                                      zone_info_t *info_array);

#define MM_SLAM(where)                                     \
    do                                                     \
    {                                                      \
        if(ERROR_ENABLED_P(ERROR_MEMORY_MANAGER_SLAM))     \
            ROMlib_sledgehammer_zones(__PRETTY_FUNCTION__, \
                                      __FILE__, __LINE__,  \
                                      where, nullptr);        \
    } while(false)

#define MM_SLAM_ZONE(zone, where)                         \
    do                                                    \
    {                                                     \
        if(ERROR_ENABLED_P(ERROR_MEMORY_MANAGER_SLAM))    \
            ROMlib_sledgehammer_zone(zone, false,         \
                                     __PRETTY_FUNCTION__, \
                                     __FILE__, __LINE__,  \
                                     where, nullptr);        \
    } while(false)

#else /* No ERROR_MEMORY_MANAGER_SLAM */

#define MM_SLAM(where)
#define MM_SLAM_ZONE(zone, where)

#endif /* No ERROR_MEMORY_MANAGER_SLAM */

#if ERROR_SUPPORTED_P(ERROR_TRAP_FAILURE)
#define GEN_MEM_ERR(err)                                            \
    do                                                              \
    {                                                               \
        if((err) != noErr)                                          \
            warning_trap_failure("returning err %ld", (long)(err)); \
    } while(false)
#else
#define GEN_MEM_ERR(err)
#endif

/* block types, or `use' */
#define FREE 0
#define NREL 1
#define REL 2

extern void mman_heap_death(const char *func, const char *where);

/* Preprocessor sludge to get __LINE__ as a string for HEAP_DEATH macro. */
#define __HEAP_DEATH(func, file, l) mman_heap_death(func, file #l)
#define _HEAP_DEATH(func, file, l) __HEAP_DEATH(func, file, l)
#define HEAP_DEATH() \
    _HEAP_DEATH(__PRETTY_FUNCTION__, " in " __FILE__ ":", __LINE__)

#define HEAPEND (LM(HeapEnd) + MIN_BLOCK_SIZE) /* temporary ctm hack */

static_assert(sizeof(block_header_t) == 12);
static_assert(sizeof(pblock_t) == 14);
}
#endif /* _MMAN_PRIVATE_H */
