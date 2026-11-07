#pragma once

namespace Executor
{
/* MacPhoenix (sound/snddispatch.cpp): SoundDispatch's dispatch-group table
   and trap stub, as the ROM's; installed per process like the trap tables. */
void ROMlib_install_sound_dispatch();
}
