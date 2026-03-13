/*
 *  ppc_constants.h - PPC emulation constants
 *
 *  Adapted from SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *  Original: xlowmem.h, emul_op.h, cpu_emulation.h, thunks.h
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef PPC_CONSTANTS_H
#define PPC_CONSTANTS_H

#include "sysdeps.h"

// ========================================
// Memory Layout (PPC Mac)
// ========================================

const uint32 PPC_ROM_SIZE      = 0x400000;   // 4MB ROM
const uint32 PPC_ROM_AREA_SIZE = 0x500000;   // 5MB ROM area (extra for patches)
const uint32 PPC_ROM_BASE      = 0x00400000; // ROM at 4MB in Mac address space

const uintptr KERNEL_DATA_BASE  = 0x68ffe000; // Address of Kernel Data
const uintptr KERNEL_DATA2_BASE = 0x5fffe000; // Alternate address of Kernel Data
const uint32  KERNEL_AREA_SIZE  = 0x2000;      // Size of Kernel Data area

const uintptr DR_EMULATOR_BASE = 0x68070000;  // Address of DR emulator code
const uint32  DR_EMULATOR_SIZE = 0x10000;      // Size of DR emulator code
const uintptr DR_CACHE_BASE    = 0x69000000;  // Address of DR cache
const uint32  DR_CACHE_SIZE    = 0x80000;      // Size of DR Cache

// ========================================
// Kernel Data Structures
// ========================================

// MacOS 68k Emulator Data
struct PPC_EmulatorData {
	uint32 v[0x400];
};

// MacOS Kernel Data
struct PPC_KernelData {
	uint32 v[0x400];
	PPC_EmulatorData ed;
};

// ========================================
// Extra Low Memory Globals (XLM, 0x2800..)
// ========================================

// Modes for XLM_RUN_MODE
#define MODE_68K     0   // 68k emulator active
#define MODE_NATIVE  1   // Switched to native mode
#define MODE_EMUL_OP 2   // 68k emulator active, within EMUL_OP routine

#define XLM_SIGNATURE              0x2800  // SheepShaver signature
#define XLM_KERNEL_DATA            0x2804  // Pointer to Kernel Data
#define XLM_TOC                    0x2808  // TOC pointer of emulator
#define XLM_SHEEP_OBJ              0x280c  // Pointer to SheepShaver object
#define XLM_RUN_MODE               0x2810  // Current run mode
#define XLM_68K_R25                0x2814  // 68k emulator's r25 (interrupt level)
#define XLM_IRQ_NEST               0x2818  // Interrupt disable nesting counter
#define XLM_PVR                    0x281c  // Theoretical PVR
#define XLM_BUS_CLOCK              0x2820  // Bus clock speed in Hz
#define XLM_EMUL_RETURN_PROC       0x2824  // Pointer to EMUL_RETURN routine
#define XLM_EXEC_RETURN_PROC       0x2828  // Pointer to EXEC_RETURN routine
#define XLM_EMUL_OP_PROC           0x282c  // Pointer to EMUL_OP routine
#define XLM_EMUL_RETURN_STACK      0x2830  // Stack pointer for EMUL_RETURN
#define XLM_RES_LIB_TOC            0x2834  // TOC pointer of Resources library
#define XLM_GET_RESOURCE           0x2838  // Pointer to native GetResource()
#define XLM_GET_1_RESOURCE         0x283c  // Pointer to native Get1Resource()
#define XLM_GET_IND_RESOURCE       0x2840  // Pointer to native GetIndResource()
#define XLM_GET_1_IND_RESOURCE     0x2844  // Pointer to native Get1IndResource()
#define XLM_R_GET_RESOURCE         0x2848  // Pointer to native RGetResource()
#define XLM_EXEC_RETURN_OPCODE     0x284c  // EXEC_RETURN opcode for Execute68k()
#define XLM_ZERO_PAGE              0x2850  // Pointer to read-only zero page
#define XLM_R13                    0x2854  // Pointer to .sdata section
#define XLM_GET_NAMED_RESOURCE     0x2858  // Pointer to native GetNamedResource()
#define XLM_GET_1_NAMED_RESOURCE   0x285c  // Pointer to native Get1NamedResource()

#define XLM_ETHER_AO_GET_HWADDR   0x28b0  // Ethernet A0_get_ethernet_address()
#define XLM_ETHER_AO_ADD_MULTI    0x28b4  // Ethernet A0_enable_multicast()
#define XLM_ETHER_AO_DEL_MULTI    0x28b8  // Ethernet A0_disable_multicast()
#define XLM_ETHER_AO_SEND_PACKET  0x28bc  // Ethernet A0_transmit_packet()
#define XLM_ETHER_INIT            0x28c0  // Ethernet InitStreamModule()
#define XLM_ETHER_TERM            0x28c4  // Ethernet TerminateStreamModule()
#define XLM_ETHER_OPEN            0x28c8  // Ethernet ether_open()
#define XLM_ETHER_CLOSE           0x28cc  // Ethernet ether_close()
#define XLM_ETHER_WPUT            0x28d0  // Ethernet ether_wput()
#define XLM_ETHER_RSRV            0x28d4  // Ethernet ether_rsrv()
#define XLM_VIDEO_DOIO            0x28d8  // Video DoDriverIO()

// ========================================
// PowerPC Opcodes
// ========================================

const uint32 POWERPC_NOP       = 0x60000000;
const uint32 POWERPC_ILLEGAL   = 0x00000000;
const uint32 POWERPC_BLR       = 0x4e800020;
const uint32 POWERPC_BCTR      = 0x4e800420;
const uint32 POWERPC_EMUL_OP   = 0x18000000; // Base opcode for EMUL_OP (PPC emulation)

// ========================================
// 68k Opcodes (used in ROM patching)
// ========================================

const uint16 M68K_ILLEGAL      = 0x4afc;
const uint16 M68K_NOP          = 0x4e71;
const uint16 M68K_RTS          = 0x4e75;
const uint16 M68K_RTD          = 0x4e74;
const uint16 M68K_JMP          = 0x4ef9;
const uint16 M68K_JMP_A0       = 0x4ed0;
const uint16 M68K_JSR          = 0x4eb9;
const uint16 M68K_JSR_A0       = 0x4e90;

// ========================================
// EMUL_OP Selectors
// ========================================

enum {
	OP_BREAK, OP_XPRAM1, OP_XPRAM2, OP_XPRAM3,
	OP_NVRAM1, OP_NVRAM2, OP_NVRAM3,
	OP_FIX_MEMTOP, OP_FIX_MEMSIZE, OP_FIX_BOOTSTACK,
	OP_SONY_OPEN, OP_SONY_PRIME, OP_SONY_CONTROL, OP_SONY_STATUS,
	OP_DISK_OPEN, OP_DISK_PRIME, OP_DISK_CONTROL, OP_DISK_STATUS,
	OP_CDROM_OPEN, OP_CDROM_PRIME, OP_CDROM_CONTROL, OP_CDROM_STATUS,
	OP_AUDIO_DISPATCH,
	OP_SOUNDIN_OPEN, OP_SOUNDIN_PRIME, OP_SOUNDIN_CONTROL, OP_SOUNDIN_STATUS, OP_SOUNDIN_CLOSE,
	OP_ADBOP,
	OP_INSTIME, OP_RMVTIME, OP_PRIMETIME, OP_MICROSECONDS,
	OP_ZERO_SCRAP, OP_PUT_SCRAP, OP_GET_SCRAP,
	OP_DEBUG_STR, OP_INSTALL_DRIVERS, OP_NAME_REGISTRY,
	OP_RESET, OP_IRQ,
	OP_SCSI_DISPATCH, OP_SCSI_ATOMIC,
	OP_CHECK_SYSV,
	OP_NTRB_17_PATCH, OP_NTRB_17_PATCH2, OP_NTRB_17_PATCH3, OP_NTRB_17_PATCH4,
	OP_CHECKLOAD,
	OP_EXTFS_COMM, OP_EXTFS_HFS,
	OP_IDLE_TIME, OP_IDLE_TIME_2,
	OP_MAX
};

// Extended 68k opcodes
const uint16 M68K_EMUL_RETURN          = 0xfe40;
const uint16 M68K_EXEC_RETURN          = 0xfe41;
const uint16 M68K_EXEC_NATIVE          = 0xfe42;
const uint16 M68K_EMUL_BREAK           = 0xfe43;

// Pre-computed M68K_EMUL_OP_* constants
const uint16 M68K_EMUL_OP_XPRAM1           = M68K_EMUL_BREAK + OP_XPRAM1;
const uint16 M68K_EMUL_OP_XPRAM2           = M68K_EMUL_BREAK + OP_XPRAM2;
const uint16 M68K_EMUL_OP_XPRAM3           = M68K_EMUL_BREAK + OP_XPRAM3;
const uint16 M68K_EMUL_OP_NVRAM1           = M68K_EMUL_BREAK + OP_NVRAM1;
const uint16 M68K_EMUL_OP_NVRAM2           = M68K_EMUL_BREAK + OP_NVRAM2;
const uint16 M68K_EMUL_OP_NVRAM3           = M68K_EMUL_BREAK + OP_NVRAM3;
const uint16 M68K_EMUL_OP_FIX_MEMTOP       = M68K_EMUL_BREAK + OP_FIX_MEMTOP;
const uint16 M68K_EMUL_OP_FIX_MEMSIZE      = M68K_EMUL_BREAK + OP_FIX_MEMSIZE;
const uint16 M68K_EMUL_OP_FIX_BOOTSTACK    = M68K_EMUL_BREAK + OP_FIX_BOOTSTACK;
const uint16 M68K_EMUL_OP_SONY_OPEN        = M68K_EMUL_BREAK + OP_SONY_OPEN;
const uint16 M68K_EMUL_OP_SONY_PRIME       = M68K_EMUL_BREAK + OP_SONY_PRIME;
const uint16 M68K_EMUL_OP_SONY_CONTROL     = M68K_EMUL_BREAK + OP_SONY_CONTROL;
const uint16 M68K_EMUL_OP_SONY_STATUS      = M68K_EMUL_BREAK + OP_SONY_STATUS;
const uint16 M68K_EMUL_OP_DISK_OPEN        = M68K_EMUL_BREAK + OP_DISK_OPEN;
const uint16 M68K_EMUL_OP_DISK_PRIME       = M68K_EMUL_BREAK + OP_DISK_PRIME;
const uint16 M68K_EMUL_OP_DISK_CONTROL     = M68K_EMUL_BREAK + OP_DISK_CONTROL;
const uint16 M68K_EMUL_OP_DISK_STATUS      = M68K_EMUL_BREAK + OP_DISK_STATUS;
const uint16 M68K_EMUL_OP_CDROM_OPEN       = M68K_EMUL_BREAK + OP_CDROM_OPEN;
const uint16 M68K_EMUL_OP_CDROM_PRIME      = M68K_EMUL_BREAK + OP_CDROM_PRIME;
const uint16 M68K_EMUL_OP_CDROM_CONTROL    = M68K_EMUL_BREAK + OP_CDROM_CONTROL;
const uint16 M68K_EMUL_OP_CDROM_STATUS     = M68K_EMUL_BREAK + OP_CDROM_STATUS;
const uint16 M68K_EMUL_OP_AUDIO_DISPATCH   = M68K_EMUL_BREAK + OP_AUDIO_DISPATCH;
const uint16 M68K_EMUL_OP_SOUNDIN_OPEN     = M68K_EMUL_BREAK + OP_SOUNDIN_OPEN;
const uint16 M68K_EMUL_OP_SOUNDIN_CLOSE    = M68K_EMUL_BREAK + OP_SOUNDIN_CLOSE;
const uint16 M68K_EMUL_OP_SOUNDIN_PRIME    = M68K_EMUL_BREAK + OP_SOUNDIN_PRIME;
const uint16 M68K_EMUL_OP_SOUNDIN_CONTROL  = M68K_EMUL_BREAK + OP_SOUNDIN_CONTROL;
const uint16 M68K_EMUL_OP_SOUNDIN_STATUS   = M68K_EMUL_BREAK + OP_SOUNDIN_STATUS;
const uint16 M68K_EMUL_OP_ADBOP            = M68K_EMUL_BREAK + OP_ADBOP;
const uint16 M68K_EMUL_OP_INSTIME          = M68K_EMUL_BREAK + OP_INSTIME;
const uint16 M68K_EMUL_OP_RMVTIME          = M68K_EMUL_BREAK + OP_RMVTIME;
const uint16 M68K_EMUL_OP_PRIMETIME        = M68K_EMUL_BREAK + OP_PRIMETIME;
const uint16 M68K_EMUL_OP_MICROSECONDS     = M68K_EMUL_BREAK + OP_MICROSECONDS;
const uint16 M68K_EMUL_OP_ZERO_SCRAP       = M68K_EMUL_BREAK + OP_ZERO_SCRAP;
const uint16 M68K_EMUL_OP_PUT_SCRAP        = M68K_EMUL_BREAK + OP_PUT_SCRAP;
const uint16 M68K_EMUL_OP_GET_SCRAP        = M68K_EMUL_BREAK + OP_GET_SCRAP;
const uint16 M68K_EMUL_OP_DEBUG_STR        = M68K_EMUL_BREAK + OP_DEBUG_STR;
const uint16 M68K_EMUL_OP_INSTALL_DRIVERS  = M68K_EMUL_BREAK + OP_INSTALL_DRIVERS;
const uint16 M68K_EMUL_OP_NAME_REGISTRY    = M68K_EMUL_BREAK + OP_NAME_REGISTRY;
const uint16 M68K_EMUL_OP_RESET            = M68K_EMUL_BREAK + OP_RESET;
const uint16 M68K_EMUL_OP_IRQ              = M68K_EMUL_BREAK + OP_IRQ;
const uint16 M68K_EMUL_OP_SCSI_DISPATCH    = M68K_EMUL_BREAK + OP_SCSI_DISPATCH;
const uint16 M68K_EMUL_OP_SCSI_ATOMIC      = M68K_EMUL_BREAK + OP_SCSI_ATOMIC;
const uint16 M68K_EMUL_OP_CHECK_SYSV       = M68K_EMUL_BREAK + OP_CHECK_SYSV;
const uint16 M68K_EMUL_OP_NTRB_17_PATCH    = M68K_EMUL_BREAK + OP_NTRB_17_PATCH;
const uint16 M68K_EMUL_OP_NTRB_17_PATCH2   = M68K_EMUL_BREAK + OP_NTRB_17_PATCH2;
const uint16 M68K_EMUL_OP_NTRB_17_PATCH3   = M68K_EMUL_BREAK + OP_NTRB_17_PATCH3;
const uint16 M68K_EMUL_OP_NTRB_17_PATCH4   = M68K_EMUL_BREAK + OP_NTRB_17_PATCH4;
const uint16 M68K_EMUL_OP_CHECKLOAD        = M68K_EMUL_BREAK + OP_CHECKLOAD;
const uint16 M68K_EMUL_OP_EXTFS_COMM       = M68K_EMUL_BREAK + OP_EXTFS_COMM;
const uint16 M68K_EMUL_OP_EXTFS_HFS        = M68K_EMUL_BREAK + OP_EXTFS_HFS;
const uint16 M68K_EMUL_OP_IDLE_TIME        = M68K_EMUL_BREAK + OP_IDLE_TIME;
const uint16 M68K_EMUL_OP_IDLE_TIME_2      = M68K_EMUL_BREAK + OP_IDLE_TIME_2;

// ========================================
// ROM Types
// ========================================

enum {
	ROMTYPE_TNT,
	ROMTYPE_ALCHEMY,
	ROMTYPE_ZANZIBAR,
	ROMTYPE_GAZELLE,
	ROMTYPE_GOSSAMER,
	ROMTYPE_NEWWORLD
};

// ========================================
// Native Function Selectors (thunks)
// ========================================

enum {
	NATIVE_PATCH_NAME_REGISTRY,
	NATIVE_VIDEO_INSTALL_ACCEL,
	NATIVE_VIDEO_VBL,
	NATIVE_VIDEO_DO_DRIVER_IO,
	NATIVE_ETHER_AO_GET_HWADDR,
	NATIVE_ETHER_AO_ADD_MULTI,
	NATIVE_ETHER_AO_DEL_MULTI,
	NATIVE_ETHER_AO_SEND_PACKET,
	NATIVE_ETHER_IRQ,
	NATIVE_ETHER_INIT,
	NATIVE_ETHER_TERM,
	NATIVE_ETHER_OPEN,
	NATIVE_ETHER_CLOSE,
	NATIVE_ETHER_WPUT,
	NATIVE_ETHER_RSRV,
	NATIVE_SERIAL_NOTHING,
	NATIVE_SERIAL_OPEN,
	NATIVE_SERIAL_PRIME_IN,
	NATIVE_SERIAL_PRIME_OUT,
	NATIVE_SERIAL_CONTROL,
	NATIVE_SERIAL_STATUS,
	NATIVE_SERIAL_CLOSE,
	NATIVE_GET_RESOURCE,
	NATIVE_GET_1_RESOURCE,
	NATIVE_GET_IND_RESOURCE,
	NATIVE_GET_1_IND_RESOURCE,
	NATIVE_R_GET_RESOURCE,
	NATIVE_MAKE_EXECUTABLE,
	NATIVE_CHECK_LOAD_INVOC,
	NATIVE_NQD_SYNC_HOOK,
	NATIVE_NQD_BITBLT_HOOK,
	NATIVE_NQD_FILLRECT_HOOK,
	NATIVE_NQD_UNKNOWN_HOOK,
	NATIVE_NQD_BITBLT,
	NATIVE_NQD_INVRECT,
	NATIVE_NQD_FILLRECT,
	NATIVE_NAMED_CHECK_LOAD_INVOC,
	NATIVE_GET_NAMED_RESOURCE,
	NATIVE_GET_1_NAMED_RESOURCE,
	NATIVE_OP_MAX
};

// ========================================
// Helpers
// ========================================

#ifndef FOURCC
#define FOURCC(a,b,c,d) \
	(((uint32)(a)<<24)|((uint32)(b)<<16)|((uint32)(c)<<8)|(uint32)(d))
#endif

// PPC default hardware parameters (Gossamer / Beige G3)
const uint32 PPC_PVR_750_V3_1    = 0x00080301;  // PowerPC 750 (G3) v3.1
const uint32 PPC_BUS_CLOCK_HZ    = 66000000;     // 66 MHz bus
const uint32 PPC_CPU_CLOCK_HZ    = 266000000;    // 266 MHz CPU

#endif // PPC_CONSTANTS_H
