#pragma once

#include <ExMacTypes.h>

class PowerCore;

namespace Executor
{
unsigned long msecs_elapsed(void);
void ROMlib_SetTimewarp(int speedup, int slowdown);

extern QHdr ROMlib_timehead;

void timeInterruptHandler();
}
