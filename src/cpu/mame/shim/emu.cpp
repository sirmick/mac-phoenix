/* emu.cpp - the shim's out-of-line definitions. */
#include "emu.h"

const attotime attotime::never(ATTOTIME_MAX_SECONDS, 0);
const attotime attotime::zero(0, 0);

profiler_state g_profiler;
