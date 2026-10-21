#pragma once

namespace Executor
{
/* MacPhoenix (sysroutines.cpp): the System's low-memory routines
   ($7B0 selector-table dispatch, $668 detached-package call), as callbacks
   into C++; set at every launch's lowmem reset. */
void ROMlib_install_system_routines();
}
