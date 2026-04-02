/*
 *  rom_patches.h - PPC ROM patches declarations
 *
 *  KPX-specific version. Includes shared globals from core and adds
 *  PPC-only declarations in namespace ppc.
 *  It is an error to include both this and core's rom_patches.h in the same TU.
 */

#ifndef KPX_ROM_PATCHES_H
#define KPX_ROM_PATCHES_H
#ifdef _COMMON_ROM_PATCHES_H
#error "KPX rom_patches.h conflicts with common/include/rom_patches.h — both included in same TU"
#endif
#define _KPX_ROM_PATCHES_H

// Block core's rom_patches.h from being included after us
#define ROM_PATCHES_H

#include "sysdeps.h"

// Shared globals (defined in core's rom_patches.cpp)
extern uint16 ROMVersion;
extern uint32 ROMBreakpoint;
extern uint32 UniversalInfo;
extern uint32 PutScrapPatch;
extern uint32 GetScrapPatch;
extern bool PrintROMInfo;

// PPC-specific declarations
namespace ppc {
	extern int ROMType;
	bool PatchROM_PPC(void);
	void InstallDrivers(void);
	void PatchAfterStartup_PPC(void);
	void InstallExtFS(void);
}

#endif /* KPX_ROM_PATCHES_H */
