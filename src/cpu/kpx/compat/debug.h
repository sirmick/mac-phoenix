/*
 *  debug.h - KPX compatibility debug macros
 *
 *  Matches SheepShaver pattern: D() and bug are re-defined each time
 *  this file is included, based on current DEBUG value.
 */

#ifndef KPX_DEBUG_H
#define KPX_DEBUG_H
#ifdef _COMMON_DEBUG_H
#error "KPX compat/debug.h conflicts with common/include/debug.h — both included in same TU"
#endif
#define _KPX_DEBUG_H

// Block core's debug.h from being included after us
#define DEBUG_H

#include <stdio.h>
#define bug(...) fprintf(stderr, __VA_ARGS__)

#endif /* KPX_DEBUG_H */

// Re-define D() based on current DEBUG setting (no guard - intentional)
#undef D
#if DEBUG
#define D(x) (x);
#else
#define D(x) ;
#endif
