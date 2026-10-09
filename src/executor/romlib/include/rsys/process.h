#if !defined(__rsys_process_h__)
#define __rsys_process_h__

#include <MemoryMgr.h>
#include <PPC.h>
#include <ProcessMgr.h>
#include <EventMgr.h>
#include <vector>

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

/* MacPhoenix Process Manager (process.cpp). */

/* Remember the low memory and CPU state a new process starts from (the
   first process's, just before its launch). */
extern void process_capture_template();
/* Host state that is per process: saved and restored on every switch,
   zeroed for a new process. */
extern void ROMlib_process_register_state(void *p, size_t n);
/* Is the current process one that runs on its own thread (not the first)? */
extern bool ROMlib_process_has_thread();
/* Did the launcher post the current process's opening Apple event? */
extern bool ROMlib_process_open_event_posted();
/* Called on every event call: where processes switch. */
extern void ROMlib_process_event_hook();
/* LaunchApplication with launchContinue: create the process. */
extern OSErr process_launch(LaunchParamBlockRec *lpbp);
/* launch.cpp: start the application in the (new) current process. */
extern void ROMlib_launch_process(FSSpec *app);

/* The processes, for the Application menu. */
struct ROMlib_process_entry
{
    ProcessSerialNumber psn;
    Str31 name;
    Handle icon;   /* icon suite, System heap */
    bool current;
};
extern std::vector<ROMlib_process_entry> ROMlib_process_entries();
/* The process whose signature this is; false if none. */
extern bool ROMlib_process_with_signature(OSType sig, ProcessSerialNumber *psn);

/* Layers (process.cpp): the Window Manager asks about other processes'
   windows -- clip out those in front (and, for the desktop, behind), and
   carry painting / visible regions on into the layers behind. */
extern void ROMlib_layers_clip_above(RgnHandle rgn);
extern void ROMlib_layers_clip_below(RgnHandle rgn);
extern void ROMlib_layers_paint_behind(RgnHandle rgn);
extern void ROMlib_layers_calcvis_behind(RgnHandle rgn);
/* Is the current process the front one (gets mouse and keyboard)? */
extern bool ROMlib_process_is_front();
/* A pending suspend/resume event for the current process. */
extern bool ROMlib_process_os_event(EventRecord *evt, bool remove);

/* The desktop was repainted there: an update for the desktop layer. */
extern void ROMlib_desktop_layer_invalidate(RgnHandle rgn);

/* The current process's PPC port name (its high-level events' sender). */
extern void ROMlib_process_port_name(PPCPortRec *port);
/* That process's port; false if there is no such process. */
extern bool ROMlib_process_port_name_of(const ProcessSerialNumber *psn, PPCPortRec *port);
}
#endif /* !defined (__rsys_process_h__) */
