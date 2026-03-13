/*
 *  emul_op_ppc.cpp - PPC SHEEP opcode dispatch and EmulOp handlers
 *
 *  Adapted from SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 *  SHEEP opcodes (0x18xxxxxx) are PPC's equivalent of M68K EmulOps.
 *  They're encoded as invalid PPC instructions that the Unicorn backend
 *  intercepts and dispatches here.
 *
 *  SHEEP sub-operations (lower 22 bits):
 *    0 = EMUL_RETURN   — return from 68k emulation
 *    1 = EXEC_RETURN   — return from Execute68k()
 *    2 = EXEC_NATIVE   — execute native (host) routine
 *    ≥3 = EMUL_OP      — selector = subop - 3, dispatch to OP_* handler
 *
 *  The CPU backend (cpu_ppc_unicorn.cpp) marshals PPC registers into
 *  M68kRegisters before calling ppc_sheep_dispatch():
 *    GPR8-15  → d[0-7]
 *    GPR16-22 → a[0-6]
 *    GPR1     → a[7] (stack pointer)
 */

#include <stdio.h>
#include <string.h>

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "emul_op_ppc.h"
#include "ppc_constants.h"
#include "main.h"
#include "macos_util.h"
#include "xpram.h"
#include "timer.h"
#include "adb.h"
#include "sony.h"
#include "disk.h"
#include "cdrom.h"
#include "scsi.h"
#include "video.h"
#include "audio.h"
#include "ether.h"
#include "serial.h"
#include "extfs.h"
#include "rsrc_patches.h"
#include "clip.h"

#define DEBUG 0
#include "debug.h"

extern bool tick_inhibit;

// PPC globals (from rom_patches_ppc.cpp)
extern uint32 KernelDataAddr;
extern uint32 RAMBase;
extern uint32 ROMBase;

// Memory globals (from cpu_context.cpp)
extern uint8_t *RAMBaseHost;
extern uint8_t *ROMBaseHost;
extern uint32_t RAMSize;
extern uint32_t ROMSize;


/*
 *  PPC EmulOp dispatch — the inner handler for EMUL_OP selectors.
 *  Receives pre-marshaled M68k registers and a selector.
 */

static void ppc_emul_op(M68kRegisters *r, uint32 pc, int selector)
{
	D(bug("PPC EmulOp %04x at %08x\n", selector, pc));
	switch (selector) {
		case OP_BREAK:				// Breakpoint
			printf("*** PPC Breakpoint ***\n");
			printf("d0 %08x d1 %08x d2 %08x d3 %08x\n",
				r->d[0], r->d[1], r->d[2], r->d[3]);
			printf("d4 %08x d5 %08x d6 %08x d7 %08x\n",
				r->d[4], r->d[5], r->d[6], r->d[7]);
			printf("a0 %08x a1 %08x a2 %08x a3 %08x\n",
				r->a[0], r->a[1], r->a[2], r->a[3]);
			printf("a4 %08x a5 %08x a6 %08x a7 %08x\n",
				r->a[4], r->a[5], r->a[6], r->a[7]);
			break;

		case OP_XPRAM1: {			// Read/write from/to XPRam
			uint32 len = r->d[3];
			uint8 *adr = Mac2HostAddr(r->a[3]);
			D(bug("XPRAMReadWrite d3: %08x, a3: %p\n", len, adr));
			int ofs = len & 0xffff;
			len >>= 16;
			if (len & 0x8000) {
				len &= 0x7fff;
				for (uint32 i=0; i<len; i++)
					XPRAM[((ofs + i) & 0xff) + 0x1300] = *adr++;
			} else {
				for (uint32 i=0; i<len; i++)
					*adr++ = XPRAM[((ofs + i) & 0xff) + 0x1300];
			}
			break;
		}

		case OP_XPRAM2:				// Read from XPRam
			r->d[1] = XPRAM[(r->d[1] & 0xff) + 0x1300];
			break;

		case OP_XPRAM3:				// Write to XPRam
			XPRAM[(r->d[1] & 0xff) + 0x1300] = r->d[2];
			break;

		case OP_NVRAM1: {			// Read from NVRAM
			int ofs = r->d[0];
			r->d[0] = XPRAM[ofs & 0x1fff];
			bool localtalk = !(XPRAM[0x13e0] || XPRAM[0x13e1]);
			switch (ofs) {
				case 0x13e0:
					if (localtalk)
						r->d[0] = 0x00;
					break;
				case 0x13e1:
					if (localtalk)
						r->d[0] = 0x01;
					break;
				case 0x13e2:
					if (localtalk)
						r->d[0] = 0x00;
					break;
				case 0x13e3:
					if (localtalk)
						r->d[0] = 0x0a;
					break;
			}
			break;
		}

		case OP_NVRAM2:				// Write to NVRAM
			XPRAM[r->d[0] & 0x1fff] = r->d[1];
			break;

		case OP_NVRAM3:				// Read/write from/to NVRAM
			if (r->d[3]) {
				r->d[0] = XPRAM[(r->d[4] + 0x1300) & 0x1fff];
			} else {
				XPRAM[(r->d[4] + 0x1300) & 0x1fff] = r->d[5];
				r->d[0] = 0;
			}
			break;

		case OP_FIX_MEMTOP: {		// Fixes MemTop in BootGlobs during startup
			D(bug("Fix MemTop\n"));
			uint32 BootGlobsAddr = RAMBase + RAMSize - 0x1c;
			WriteMacInt32(BootGlobsAddr - 20, RAMBase + RAMSize);
			r->a[6] = RAMBase + RAMSize;
			break;
		}

		case OP_FIX_MEMSIZE: {		// Fixes physical/logical RAM size during startup
			D(bug("Fix MemSize\n"));
			uint32 diff = ReadMacInt32(0x1ef8) - ReadMacInt32(0x1ef4);
			WriteMacInt32(0x1ef8, RAMSize);
			WriteMacInt32(0x1ef4, RAMSize - diff);
			break;
		}

		case OP_FIX_BOOTSTACK:		// Fixes boot stack pointer in boot 3 resource
			D(bug("Fix BootStack\n"));
			r->a[1] = r->a[7] = RAMBase + RAMSize * 3 / 4;
			break;

		case OP_SONY_OPEN:			// Floppy driver functions
			r->d[0] = SonyOpen(r->a[0], r->a[1]);
			break;
		case OP_SONY_PRIME:
			r->d[0] = SonyPrime(r->a[0], r->a[1]);
			break;
		case OP_SONY_CONTROL:
			r->d[0] = SonyControl(r->a[0], r->a[1]);
			break;
		case OP_SONY_STATUS:
			r->d[0] = SonyStatus(r->a[0], r->a[1]);
			break;

		case OP_DISK_OPEN:			// Disk driver functions
			r->d[0] = DiskOpen(r->a[0], r->a[1]);
			break;
		case OP_DISK_PRIME:
			r->d[0] = DiskPrime(r->a[0], r->a[1]);
			break;
		case OP_DISK_CONTROL:
			r->d[0] = DiskControl(r->a[0], r->a[1]);
			break;
		case OP_DISK_STATUS:
			r->d[0] = DiskStatus(r->a[0], r->a[1]);
			break;

		case OP_CDROM_OPEN:			// CD-ROM driver functions
			r->d[0] = CDROMOpen(r->a[0], r->a[1]);
			break;
		case OP_CDROM_PRIME:
			r->d[0] = CDROMPrime(r->a[0], r->a[1]);
			break;
		case OP_CDROM_CONTROL:
			r->d[0] = CDROMControl(r->a[0], r->a[1]);
			break;
		case OP_CDROM_STATUS:
			r->d[0] = CDROMStatus(r->a[0], r->a[1]);
			break;

		case OP_AUDIO_DISPATCH:		// Audio component functions
			r->d[0] = AudioDispatch(r->a[3], r->a[4]);
			break;

		case OP_SOUNDIN_OPEN:		// Sound input driver functions
			r->d[0] = SoundInOpen(r->a[0], r->a[1]);
			break;
		case OP_SOUNDIN_PRIME:
			r->d[0] = SoundInPrime(r->a[0], r->a[1]);
			break;
		case OP_SOUNDIN_CONTROL:
			r->d[0] = SoundInControl(r->a[0], r->a[1]);
			break;
		case OP_SOUNDIN_STATUS:
			r->d[0] = SoundInStatus(r->a[0], r->a[1]);
			break;
		case OP_SOUNDIN_CLOSE:
			r->d[0] = SoundInClose(r->a[0], r->a[1]);
			break;

		case OP_ADBOP:				// ADBOp() replacement
			ADBOp(r->d[0], Mac2HostAddr(ReadMacInt32(r->a[0])));
			break;

		case OP_INSTIME:			// InsTime() replacement
			r->d[0] = InsTime(r->a[0], r->d[1]);
			break;
		case OP_RMVTIME:			// RmvTime() replacement
			r->d[0] = RmvTime(r->a[0]);
			break;
		case OP_PRIMETIME:			// PrimeTime() replacement
			r->d[0] = PrimeTime(r->a[0], r->d[0]);
			break;

		case OP_MICROSECONDS:		// Microseconds() replacement
			Microseconds(r->a[0], r->d[0]);
			break;

		case OP_ZERO_SCRAP:			// ZeroScrap() patch
			ZeroScrap();
			break;

		case OP_PUT_SCRAP:			// PutScrap() patch
			PutScrap(ReadMacInt32(r->a[7] + 8), Mac2HostAddr(ReadMacInt32(r->a[7] + 4)), ReadMacInt32(r->a[7] + 12));
			break;

		case OP_GET_SCRAP:			// GetScrap() patch
			GetScrap((void **)Mac2HostAddr(ReadMacInt32(r->a[7] + 4)), ReadMacInt32(r->a[7] + 8), ReadMacInt32(r->a[7] + 12));
			break;

		case OP_DEBUG_STR: {		// DebugStr() shows warning message
			uint8 *pstr = Mac2HostAddr(ReadMacInt32(r->a[7] + 4));
			char str[256];
			int i;
			for (i=0; i<pstr[0]; i++)
				str[i] = pstr[i+1];
			str[i] = 0;
			fprintf(stderr, "[PPC] DebugStr: %s\n", str);
			break;
		}

		case OP_INSTALL_DRIVERS:	// Patch to install our own drivers during startup
			// TODO: Requires thunks/NativeTVECT for MakeExecutable, DebugStr patching
			fprintf(stderr, "[PPC] OP_INSTALL_DRIVERS: stub (requires thunks)\n");
			break;

		case OP_NAME_REGISTRY:		// Patch Name Registry and initialize CallUniversalProc
			r->d[0] = (uint32)-1;
			// TODO: PatchNameRegistry() and InitCallUniversalProc() require thunks
			fprintf(stderr, "[PPC] OP_NAME_REGISTRY: stub (requires thunks)\n");
			break;

		case OP_RESET:				// Early in MacOS reset
			D(bug("*** PPC RESET ***\n"));
			fprintf(stderr, "[PPC] OP_RESET\n");
			tick_inhibit = true;
			CDROMRemount();
			TimerReset();
			AudioReset();
			tick_inhibit = false;
			break;

		case OP_IRQ:			// Level 1 interrupt
			WriteMacInt16(ReadMacInt32(KernelDataAddr + 0x67c), 0);
			r->d[0] = 0;
			if (HasMacStarted()) {
				if (InterruptFlags & INTFLAG_60HZ) {
					ClearInterruptFlag(INTFLAG_60HZ);
#if !PRECISE_TIMING
					TimerInterrupt();
#endif
					// TODO: ExecuteNative(NATIVE_VIDEO_VBL) requires thunks

					static int tick_counter = 0;
					if (++tick_counter >= 60) {
						tick_counter = 0;
						SonyInterrupt();
						DiskInterrupt();
						CDROMInterrupt();
					}

					r->d[0] = 1;
				}
				if (InterruptFlags & INTFLAG_SERIAL) {
					ClearInterruptFlag(INTFLAG_SERIAL);
					SerialInterrupt();
				}
				if (InterruptFlags & INTFLAG_ETHER) {
					ClearInterruptFlag(INTFLAG_ETHER);
					// TODO: ExecuteNative(NATIVE_ETHER_IRQ) requires thunks
				}
				if (InterruptFlags & INTFLAG_TIMER) {
					ClearInterruptFlag(INTFLAG_TIMER);
					TimerInterrupt();
				}
				if (InterruptFlags & INTFLAG_AUDIO) {
					ClearInterruptFlag(INTFLAG_AUDIO);
					AudioInterrupt();
				}
				if (InterruptFlags & INTFLAG_ADB) {
					ClearInterruptFlag(INTFLAG_ADB);
					ADBInterrupt();
				}
			} else
				r->d[0] = 1;
			break;

		case OP_SCSI_DISPATCH: {	// SCSIDispatch() replacement
			uint32 ret = ReadMacInt32(r->a[7]);
			uint16 sel = ReadMacInt16(r->a[7] + 4);
			r->a[7] += 6;
			int stack;
			switch (sel) {
				case 0:		// SCSIReset
					WriteMacInt16(r->a[7], SCSIReset());
					stack = 0;
					break;
				case 1:		// SCSIGet
					WriteMacInt16(r->a[7], SCSIGet());
					stack = 0;
					break;
				case 2:		// SCSISelect
				case 11:	// SCSISelAtn
					WriteMacInt16(r->a[7] + 2, SCSISelect(ReadMacInt8(r->a[7] + 1)));
					stack = 2;
					break;
				case 3:		// SCSICmd
					WriteMacInt16(r->a[7] + 6, SCSICmd(ReadMacInt16(r->a[7]), Mac2HostAddr(ReadMacInt32(r->a[7] + 2))));
					stack = 6;
					break;
				case 4:		// SCSIComplete
					WriteMacInt16(r->a[7] + 12, SCSIComplete(ReadMacInt32(r->a[7]), ReadMacInt32(r->a[7] + 4), ReadMacInt32(r->a[7] + 8)));
					stack = 12;
					break;
				case 5:		// SCSIRead
				case 8:		// SCSIRBlind
					WriteMacInt16(r->a[7] + 4, SCSIRead(ReadMacInt32(r->a[7])));
					stack = 4;
					break;
				case 6:		// SCSIWrite
				case 9:		// SCSIWBlind
					WriteMacInt16(r->a[7] + 4, SCSIWrite(ReadMacInt32(r->a[7])));
					stack = 4;
					break;
				case 10:	// SCSIStat
					WriteMacInt16(r->a[7], SCSIStat());
					stack = 0;
					break;
				case 12:	// SCSIMsgIn
					WriteMacInt16(r->a[7] + 4, 0);
					stack = 4;
					break;
				case 13:	// SCSIMsgOut
					WriteMacInt16(r->a[7] + 2, 0);
					stack = 2;
					break;
				case 14:	// SCSIMgrBusy
					WriteMacInt16(r->a[7], SCSIMgrBusy());
					stack = 0;
					break;
				default:
					printf("FATAL: SCSIDispatch: illegal selector %d\n", sel);
					stack = 0;
			}
			r->a[0] = ret;
			r->a[7] += stack;
			break;
		}

		case OP_SCSI_ATOMIC:		// SCSIAtomic() replacement
			D(bug("SCSIAtomic\n"));
			r->d[0] = (uint32)-7887;
			break;

		case OP_CHECK_SYSV: {		// Check MacOS version
			r->a[1] = r->d[1];
			r->a[0] = ReadMacInt32(r->d[1]);
			uint32 sysv = ReadMacInt16(r->a[0]);
			D(bug("Detected MacOS version %d.%d.%d\n", (sysv >> 8) & 0xf, (sysv >> 4) & 0xf, sysv & 0xf));
			break;
		}

		case OP_NTRB_17_PATCH:
			r->a[2] = ReadMacInt32(r->a[7]);
			r->a[7] += 4;
			// TODO: PatchNativeResourceManager() requires thunks
			break;

		case OP_NTRB_17_PATCH2:
			r->a[7] += 8;
			// TODO: PatchNativeResourceManager() requires thunks
			break;

		case OP_NTRB_17_PATCH3:
			r->a[2] = ReadMacInt32(r->a[7]);
			r->a[7] += 4;
			// TODO: PatchNativeResourceManager() requires thunks
			break;

		case OP_NTRB_17_PATCH4:
			r->d[0] = ReadMacInt16(r->a[7]);
			r->a[7] += 2;
			// TODO: PatchNativeResourceManager() requires thunks
			break;

		case OP_CHECKLOAD: {		// vCheckLoad() patch
			uint32 type = ReadMacInt32(r->a[7]);
			r->a[7] += 4;
			int16 id = ReadMacInt16(r->a[2]);
			if (r->a[0] == 0)
				break;
			uint32 adr = ReadMacInt32(r->a[0]);
			if (adr == 0)
				break;
			uint8 *p = Mac2HostAddr(adr);
			uint32 size = ReadMacInt32(adr - 8) & 0xffffff;
			CheckLoad(type, id, p, size);
			break;
		}

		case OP_EXTFS_COMM:			// External file system routines
			WriteMacInt16(r->a[7] + 14, ExtFSComm(ReadMacInt16(r->a[7] + 12), ReadMacInt32(r->a[7] + 8), ReadMacInt32(r->a[7] + 4)));
			break;

		case OP_EXTFS_HFS:
			WriteMacInt16(r->a[7] + 20, ExtFSHFS(ReadMacInt32(r->a[7] + 16), ReadMacInt16(r->a[7] + 14), ReadMacInt32(r->a[7] + 10), ReadMacInt32(r->a[7] + 6), ReadMacInt16(r->a[7] + 4)));
			break;

		case OP_IDLE_TIME:
			if (ReadMacInt32(0x14c) == 0)
				idle_wait();
			r->a[0] = ReadMacInt32(0x2b6);
			break;

		case OP_IDLE_TIME_2:
			if (ReadMacInt32(0x14c) == 0)
				idle_wait();
			r->d[0] = (uint32)-2;
			break;

		default:
			printf("FATAL: PPC EMUL_OP called with bogus selector %08x\n", selector);
			break;
	}
}


/*
 *  SHEEP opcode dispatch — called by the PPC CPU backend.
 *
 *  The CPU backend is responsible for:
 *  1. Detecting SHEEP opcodes (0x18xxxxxx)
 *  2. Marshaling PPC registers into M68kRegisters:
 *     GPR8-15 → d[0-7], GPR16-22 → a[0-6], GPR1 → a[7]
 *  3. Calling this function
 *  4. Marshaling results back to PPC registers
 *
 *  For EMUL_RETURN/EXEC_RETURN/EXEC_NATIVE, 'r' may be NULL.
 */

bool ppc_sheep_dispatch(uint32_t opcode, M68kRegisters *r, uint32_t pc)
{
	uint32_t subop = opcode & 0x003FFFFF;

	switch (subop) {
		case 0:  // EMUL_RETURN — return from 68k emulation
			D(bug("PPC SHEEP: EMUL_RETURN\n"));
			WriteMacInt32(XLM_RUN_MODE, MODE_NATIVE);
			return true;

		case 1:  // EXEC_RETURN — return from Execute68k()
			D(bug("PPC SHEEP: EXEC_RETURN\n"));
			return true;

		case 2:  // EXEC_NATIVE — execute native (host) routine
			D(bug("PPC SHEEP: EXEC_NATIVE\n"));
			// TODO: Requires thunk dispatch table
			fprintf(stderr, "[PPC] EXEC_NATIVE: stub (requires thunks)\n");
			return true;

		default: {
			// EMUL_OP: selector = subop - 3
			int selector = subop - 3;
			if (r) {
				ppc_emul_op(r, pc, selector);
			} else {
				fprintf(stderr, "[PPC] EMUL_OP selector %d called without registers!\n", selector);
			}
			return true;
		}
	}
}
