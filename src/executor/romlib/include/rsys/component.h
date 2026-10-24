#pragma once

#include <ProcessMgr.h>

namespace Executor
{
/* MacPhoenix Component Manager (component.cpp). */

/* Register the 'thng' resources of an open resource file, as
   RegisterComponentResourceFile does; returns how many registered. */
int32_t ROMlib_register_components(INTEGER refnum, bool global);
/* A process quit: its local components go. */
/* A process quit: its components go, and any component, even a global one,
   whose code was in its partition [part_lo, part_hi) (guest addresses). */
/* MacPhoenix: a component Executor implements in C++: fn(params, storage)
   gets each call's ComponentParameters (guest address) and returns its
   ComponentResult. 'thng' resources of the same type are not registered
   after it (the first lends it its name, info and icon); returns its ID. */
using ROMlib_native_component_fn = int32_t (*)(uint32_t params, uint32_t storage);
uint32_t ROMlib_register_native_component(uint32_t type, uint32_t subtype,
                                          uint32_t manufacturer, const char *name,
                                          ROMlib_native_component_fn fn);

void ROMlib_components_process_exit(const ProcessSerialNumber *psn,
                                    uint32_t part_lo = 0, uint32_t part_hi = 0);
}
