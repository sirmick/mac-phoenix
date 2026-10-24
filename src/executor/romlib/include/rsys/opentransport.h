#pragma once

/* MacPhoenix: Open Transport's 68k client API, native (opentransport/). */

#include <base/common.h>

namespace Executor
{
/* Answer Open Transport (Gestalt 'otan'); on unless --no-executor-ot. */
extern bool ROMlib_open_transport;

/* Gestalt 'otan' and 'otvr': true with the answer when OT is on; asking
   also puts the Shared Library Manager entries OT's glue binds through in
   place (Executor's own, or wrappers on Apple's ASLM). */
bool ROMlib_ot_gestalt(OSType selector, int32_t *value);

/* An application is about to start: Apple's ASLM, if it is running, gets
   its wrappers now, so a library manager the application makes before it
   asks Gestalt 'otan' (the TCP/IP control panel does) answers OT's
   function sets with Executor's. */
void ROMlib_ot_process_start();

/* A process quits: its OT clients' providers closed, the Shared Library
   Manager records its clients kept in [part_lo, part_hi) freed. */
void ROMlib_ot_process_exit(uint32_t part_lo, uint32_t part_hi);

/* SystemTask: the current process's scheduled OT system tasks run. */
void ROMlib_ot_system_task();
}
