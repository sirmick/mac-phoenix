#if !defined(__rsys_process_h__)
#define __rsys_process_h__

#include <MemoryMgr.h>
#include <PPC.h>

namespace Executor
{
typedef struct size_resource
{
    GUEST_STRUCT;
    GUEST<int16_t> flags;
    GUEST<int32_t> pref_size;
    GUEST<int32_t> min_size;
} size_resource_t;

static_assert(sizeof(size_resource_t) == 10);

/* The Process Manager heap: above the System heap, up to BufPtr.
   ROMlib_InitZones (re)creates it and forgets any partition in it. */
extern THz ROMlib_pm_zone;
extern void process_reset_heap(THz pm_zone);

/* Lay out an application's partition in the Process Manager heap, as
   7.5.5 does (docs/executor/MEMORY_MAP.md): reads SIZE and CODE 0 from
   the application file in the current directory, then sets ApplZone,
   HeapEnd, ApplLimit, CurStackBase, CurrentA5 and MemTop.  The heap is
   left as TheZone, so the application's resource map lands in it. */
extern void process_layout_partition(ConstStringPtr app_name);

/* The current process's PPC port name (its high-level events' sender). */
extern void ROMlib_process_port_name(PPCPortRec *port);
}
#endif /* !defined (__rsys_process_h__) */
