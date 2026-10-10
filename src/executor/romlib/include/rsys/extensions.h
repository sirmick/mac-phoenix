#pragma once

namespace Executor
{
/* MacPhoenix (extensions.cpp): run the allowed extensions' INITs, as the
   System's loader does before the Finder starts. */
void ROMlib_load_extensions();

/* AppPacks holding the System file's PACK 0-7, as InitAllPacks leaves them
   (Apple's System file only; nothing otherwise). */
void ROMlib_install_app_packs();
}
