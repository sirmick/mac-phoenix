#pragma once

#include <ProcessMgr.h>

namespace Executor
{
/* MacPhoenix Component Manager (component.cpp). */

/* Register the 'thng' resources of an open resource file, as
   RegisterComponentResourceFile does; returns how many registered. */
int32_t ROMlib_register_components(INTEGER refnum, bool global);
/* A process quit: its local components go. */
void ROMlib_components_process_exit(const ProcessSerialNumber *psn);
}
