#if !defined(__RSYS_VBL__)
#define __RSYS_VBL__

#include <base/traps.h>
#include <ExMacTypes.h>

#define MODULE_NAME time_vbl
#include <base/api-module.h>

namespace Executor
{
extern void ROMlib_clockonoff(LONGINT onoroff);
/* MacPhoenix: run the deferred tasks queued with DTInstall (at a tick, and
   right after a sound channel callback). */
extern void ROMlib_run_deferred_tasks();
}

#endif /* !defined(__RSYS_VBL__) */
