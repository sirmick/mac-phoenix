#pragma once

namespace Executor
{
/* MacPhoenix (extensions.cpp): run the allowed extensions' INITs, as the
   System's loader does before the Finder starts. */
void ROMlib_load_extensions();
}
