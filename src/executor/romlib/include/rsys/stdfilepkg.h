#pragma once

/* MacPhoenix (stdfilepkg.cpp): Apple's Standard File, the System file's
   PACK 3, in place of Executor's (stdfile.cpp). */

namespace Executor
{
/* Route the Pack3 trap to the System's package, locked in AppPacks.
   Nothing without Apple's System file; Executor's dialogs stay the
   fallback. Called at init and again after the extensions, as the Help
   package is. */
void ROMlib_install_standard_file();

/* Is Apple's package in charge of the trap? */
bool ROMlib_standard_file_package_p();
}
