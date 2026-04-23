/*
 *  prefs.h - KPX compatibility stub for SheepShaver's prefs API.
 *
 *  Three flags are pinned TRUE because they are behavior contracts inherited
 *  from SheepShaver, not user-intent knobs:
 *
 *    ignoreillegal — disabling breaks driver probes that issue unknown opcodes
 *                    (ppc-execute.cpp:68)
 *    ignoresegv    — disabling fails on legal Mac faults that the host must
 *                    swallow to keep emulation alive (cpu_ppc_kpx.cpp:1293)
 *    idlewait      — modern consumers read config::EmulatorConfig::idlewait
 *                    directly; this return value is unused, kept for ABI
 *                    compat with any remaining callers
 *
 *  No user knob exists for the three above; if you ever want one, wire it
 *  through EmulatorConfig like idlewait, then update this file.
 */

#ifndef KPX_PREFS_H
#define KPX_PREFS_H

#include "sysdeps.h"

static inline bool PrefsFindBool(const char *name)
{
    if (strcmp(name, "ignoreillegal") == 0) return true;
    if (strcmp(name, "ignoresegv") == 0) return true;
    if (strcmp(name, "idlewait") == 0) return true;
    if (strcmp(name, "gfxaccel") == 0) return true;
    return false;
}

static inline int32 PrefsFindInt32(const char *name)
{
    (void)name;
    return 0;
}

#endif /* KPX_PREFS_H */
