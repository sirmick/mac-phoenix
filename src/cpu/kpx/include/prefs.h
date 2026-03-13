/*
 *  prefs.h - KPX shim for SheepShaver preferences
 *
 *  Mac-phoenix doesn't use a Prefs system; provide stubs.
 */

#ifndef PREFS_H
#define PREFS_H

#include <stdbool.h>

static inline bool PrefsFindBool(const char *name) { return false; }

#endif /* PREFS_H */
