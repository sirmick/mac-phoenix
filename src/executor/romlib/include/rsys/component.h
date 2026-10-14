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
void ROMlib_components_process_exit(const ProcessSerialNumber *psn,
                                    uint32_t part_lo = 0, uint32_t part_hi = 0);
}
