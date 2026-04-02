/*
 *  rsrc_patches.h - Runtime resource patching declarations (PPC)
 */

#ifndef KPX_RSRC_PATCHES_H
#define KPX_RSRC_PATCHES_H
#ifdef _COMMON_RSRC_PATCHES_H
#error "KPX compat/rsrc_patches.h conflicts with common/include/rsrc_patches.h — both included in same TU"
#endif
#define _KPX_RSRC_PATCHES_H

// Block core's rsrc_patches.h from being included after us
#define RSRC_PATCHES_H

#include "sysdeps.h"

namespace ppc {
extern void CheckLoad(uint32 type, int16 id, uint16 *p, uint32 size);
extern void CheckLoad(uint32 type, const char *name, uint8 *p, uint32 size);
}

#endif
