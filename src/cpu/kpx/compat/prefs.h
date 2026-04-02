/*
 *  prefs.h - KPX compatibility stub for preferences
 */

#ifndef KPX_PREFS_H
#define KPX_PREFS_H

#include "sysdeps.h"

// Stub preferences — must match legacy SheepShaver defaults
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
