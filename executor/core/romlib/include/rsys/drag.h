#pragma once

/* MacPhoenix (drag.cpp): Apple's Drag Manager, the System file's five
   'proc' resources, in place of Executor's stubs (DragMgr.yaml). */

namespace Executor
{
/* Load the pieces once, make the globals (ExpandMem+$118) and route the
   DragDispatch trap to Apple's routines. Nothing without Apple's System
   file. */
void ROMlib_install_drag_manager();

/* Is Apple's Drag Manager in charge of the trap? */
bool ROMlib_drag_manager_p();

/* The exiting process's handlers go, and the cursor shield it may have
   replaced mid-drag comes back (what Apple's patch on $AAFB does). Called
   with the process's world still in place. */
void ROMlib_drag_process_exit();
}
