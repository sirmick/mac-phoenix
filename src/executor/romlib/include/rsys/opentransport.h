#pragma once

/* MacPhoenix: Open Transport's 68k client API, native (opentransport/). */

#include <base/common.h>

namespace Executor
{
/* --executor-ot: answer Open Transport (Gestalt 'otan'). */
extern bool ROMlib_open_transport;

/* Gestalt 'otan' and 'otvr': true with the answer when OT is on and the
   Shared Library Manager is up (OT's glue binds through it); asking also
   puts the binding hook in place. */
bool ROMlib_ot_gestalt(OSType selector, int32_t *value);
}
