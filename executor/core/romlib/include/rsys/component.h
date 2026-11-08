#pragma once

#include <ProcessMgr.h>

namespace Executor
{
/* MacPhoenix Component Manager (component.cpp). */

/* Register the 'thng' resources of an open resource file, as
   RegisterComponentResourceFile does; returns how many registered.
   without_init: the file's own INIT hasn't run (QuickTime's components
   with QuickTime off): a component that wants its register message is
   left out, its code not run. */
int32_t ROMlib_register_components(INTEGER refnum, bool global, bool without_init = false);
/* MacPhoenix: a component Executor implements in C++. Its calls are the
   definitions' functions on the dispatcher `calls` (a `selector-location:
   Component` one) that name an Executor routine (COMPONENT_FUNCTION);
   open, close, register, version and can-do are answered here, anything
   else is logged and answered badComponentSelector. 'thng' resources of
   the same type are not registered after it (the first lends it its name,
   info and icon); returns its ID. */
uint32_t ROMlib_register_native_component(uint32_t type, uint32_t subtype,
                                          uint32_t manufacturer, const char *name,
                                          const char *calls, int32_t version);

/* A process quit: its components go, and any component, even a global one,
   whose code was in its partition [part_lo, part_hi) (guest addresses). */
void ROMlib_components_process_exit(const ProcessSerialNumber *psn,
                                    uint32_t part_lo = 0, uint32_t part_hi = 0);
}
