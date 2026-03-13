/*
 *  rom_patches_ppc.h - PPC ROM patches
 *
 *  Adapted from SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 */

#ifndef ROM_PATCHES_PPC_H
#define ROM_PATCHES_PPC_H

#include "sysdeps.h"

// ROM type (set by PatchROM_PPC)
extern int PPCROMType;

// Decode ROM image (handles 4MB plain and CHRP compressed)
extern bool DecodeROM_PPC(uint8 *data, uint32 size);

// Apply all PPC ROM patches
extern bool PatchROM_PPC(void);

// Install drivers (called later during boot via EMUL_OP)
extern void InstallDrivers_PPC(void);

// Initialize XLM globals in low memory
extern void InitXLM(void);

#endif
