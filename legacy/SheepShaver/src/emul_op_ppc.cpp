/*
 *  emul_op.cpp - 68k opcodes for ROM patches
 *
 *  SheepShaver (C) 1997-2008 Christian Bauer and Marc Hellwig
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

#include <stdio.h>

#include "sysdeps.h"
#include "main.h"
#include "version.h"
#include "prefs.h"
#include "cpu_emulation.h"
#include "xlowmem.h"
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
#include "clip.h"
#include "extfs.h"
#include "macos_util.h"
#include "rom_patches.h"
#include "rsrc_patches.h"
#include "name_registry.h"
#include "user_strings.h"
#include "emul_op.h"
#include "thunks.h"

#define DEBUG 0
#include "debug.h"

#include "boot_progress.h"

extern bool tick_inhibit;

void PlayStartupSound();

// TVector of MakeExecutable
static uint32 MakeExecutableTvec;


/*
 *  Execute EMUL_OP opcode (called by 68k emulator)
 */

static int emulop_total = 0;
void EmulOp(M68kRegisters *r, uint32 pc, int selector)
{
	if (selector != OP_IRQ)
		++emulop_total;
	// Track boot progress via platform-level events
	{
		enum BootEvent event = (enum BootEvent)-1;
		switch (selector) {
			case OP_RESET:           event = BOOT_EVENT_RESET; break;
			case OP_INSTALL_DRIVERS: event = BOOT_EVENT_INSTALL_DRIVERS; break;
			case OP_CHECKLOAD:       event = BOOT_EVENT_CHECKLOAD; break;
			case OP_IRQ:             event = BOOT_EVENT_IRQ; break;
			case OP_IDLE_TIME:       event = BOOT_EVENT_IDLE_TIME; break;
			case OP_IDLE_TIME_2:     event = BOOT_EVENT_IDLE_TIME; break;
		}
		if ((int)event >= 0) boot_progress_report(event, r);
	}

	switch (selector) {
		case OP_BREAK:				// Breakpoint
			printf("*** Breakpoint\n");
			Dump68kRegs(r);
			break;

		case OP_XPRAM1: {			// Read/write from/to XPRam
			uint32 len = r->d[3];
			uint8 *adr = Mac2HostAddr(r->a[3]);
			D(bug("XPRAMReadWrite d3: %08lx, a3: %p\n", len, adr));
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
			{
				static int nvr_count = 0;
				if (++nvr_count <= 30)
					fprintf(stderr, "[NVRAM-R] #%d ofs=0x%04x val=0x%02x\n",
						nvr_count, ofs & 0x1fff, r->d[0]);
			}
			bool localtalk = !(XPRAM[0x13e0] || XPRAM[0x13e1]);	// LocalTalk enabled?
			switch (ofs) {
				case 0x13e0:			// Disable LocalTalk (use EtherTalk instead)
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
			{
				static int nvw_count = 0;
				if (++nvw_count <= 30)
					fprintf(stderr, "[NVRAM-W] #%d ofs=0x%04x val=0x%02x\n",
						nvw_count, r->d[0] & 0x1fff, r->d[1] & 0xff);
			}
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

		case OP_FIX_MEMTOP:			// Fixes MemTop in BootGlobs during startup
			D(bug("Fix MemTop\n"));
			WriteMacInt32(BootGlobsAddr - 20, RAMBase + RAMSize);	// MemTop
			r->a[6] = RAMBase + RAMSize;
			break;

		case OP_FIX_MEMSIZE: {		// Fixes physical/logical RAM size during startup
			uint32 phys = ReadMacInt32(0x1ef8);
			uint32 logical = ReadMacInt32(0x1ef4);
			uint32 diff = phys - logical;
			fprintf(stderr, "[FIX_MEMSIZE] BEFORE: phys=0x%08x log=0x%08x diff=0x%08x RAMSize=0x%08x RAMBase=0x%08x\n",
					phys, logical, diff, RAMSize, RAMBase);
			fprintf(stderr, "[FIX_MEMSIZE] MemTop=0x%08x BufPtr=0x%08x ScrnBase=0x%08x\n",
					ReadMacInt32(0x108), ReadMacInt32(0x10c), ReadMacInt32(0x824));
			WriteMacInt32(0x1ef8, RAMSize);
			WriteMacInt32(0x1ef4, RAMSize - diff);
			fprintf(stderr, "[FIX_MEMSIZE] AFTER: phys=0x%08x log=0x%08x\n", RAMSize, RAMSize - diff);
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
		case OP_DISK_PRIME: {
			int16 disk_result = DiskPrime(r->a[0], r->a[1]);
			if (disk_result != 0) {
				static int disk_err_count = 0;
				if (++disk_err_count <= 20)
					fprintf(stderr, "[DISK] DiskPrime ERROR %d (pb=%08x dce=%08x)\n",
						disk_result, r->a[0], r->a[1]);
			}
			r->d[0] = disk_result;
			break;
		}
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
			{
				uint32 adb_addr = ReadMacInt32(r->a[0]);
				uint8 *adb_data = Mac2HostAddr(adb_addr);
				D(bug("OP_ADBOP: d0=0x%02x a0=0x%08x -> data_addr=0x%08x\n",
						r->d[0], r->a[0], adb_addr));
				ADBOp(r->d[0], adb_data);
			}
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

		case OP_DEBUG_STR:			// DebugStr() shows warning message
			if (PrefsFindBool("nogui")) {
				uint8 *pstr = Mac2HostAddr(ReadMacInt32(r->a[7] + 4));
				char str[256];
				int i;
				for (i=0; i<pstr[0]; i++)
					str[i] = pstr[i+1];
				str[i] = 0;
				WarningAlert(str);
			}
			break;

		case OP_INSTALL_DRIVERS: {	// Patch to install our own drivers during startup
			fprintf(stderr, "[EMULOP] OP_INSTALL_DRIVERS start\n");
			// Install drivers
			InstallDrivers();
			fprintf(stderr, "[EMULOP] InstallDrivers done, patching MakeExecutable...\n");

			// Patch MakeExecutable()
			MakeExecutableTvec = FindLibSymbol("\023PrivateInterfaceLib", "\016MakeExecutable");
			fprintf(stderr, "[EMULOP] MakeExecutable TVECT at 0x%08x\n", MakeExecutableTvec);
			WriteMacInt32(MakeExecutableTvec, NativeFunction(NATIVE_MAKE_EXECUTABLE));
#if !EMULATED_PPC
			WriteMacInt32(MakeExecutableTvec + 4, (uint32)TOC);
#endif

			// Patch DebugStr()
			static const uint8 proc_template[] = {
				M68K_EMUL_OP_DEBUG_STR >> 8, M68K_EMUL_OP_DEBUG_STR & 0xFF,
				0x4e, 0x74,			// rtd	#4
				0x00, 0x04
			};
			BUILD_SHEEPSHAVER_PROCEDURE(proc);
			WriteMacInt32(0x1dfc, proc);
			fprintf(stderr, "[EMULOP] OP_INSTALL_DRIVERS complete\n");
			break;
		}

		case OP_NAME_REGISTRY:		// Patch Name Registry and initialize CallUniversalProc
			r->d[0] = (uint32)-1;
			PatchNameRegistry();
			InitCallUniversalProc();
			break;

		case OP_RESET:				// Early in MacOS reset
			D(bug("*** RESET ***\n"));
			fprintf(stderr, "[RESET] KD+0x5b4=%08x KD+0x1720=0x%08x\n",
				ReadMacInt32(KernelDataAddr + 0x5b4),
				ReadMacInt32(KernelDataAddr + 0x1720));
			// Don't start flight recorder here — record_step callbacks
			// change JIT behavior and may cause KD+0x1720 divergence
			tick_inhibit = true;
			CDROMRemount(); // for System 7.x
			TimerReset();
			MacOSUtilReset();
			EtherResetCachedAllocation();
			ether_reset();
			AudioReset();
#ifdef USE_SDL_AUDIO
			PlayStartupSound();
#endif
			// Enable DR emulator (disabled for now)
			if (PrefsFindBool("jit68k") && 0) {
				D(bug("DR activated\n"));
				WriteMacInt32(KernelDataAddr + 0x17a0, 3);		// Prepare for DR emulator activation
				WriteMacInt32(KernelDataAddr + 0x17c0, DR_CACHE_BASE);
				WriteMacInt32(KernelDataAddr + 0x17c4, DR_CACHE_SIZE);
				WriteMacInt32(KernelDataAddr + 0x1b04, DR_CACHE_BASE);
				WriteMacInt32(KernelDataAddr + 0x1b00, DR_EMULATOR_BASE);
				memcpy((void *)DR_EMULATOR_BASE, (void *)(ROMBase + 0x370000), DR_EMULATOR_SIZE);
				MakeExecutable(0, DR_EMULATOR_BASE, DR_EMULATOR_SIZE);
			}
			tick_inhibit = false;
			break;

		case OP_IRQ:			// Level 1 interrupt
			WriteMacInt16(ReadMacInt32(KernelDataAddr + 0x67c), 0);
			r->d[0] = 0;
			if (HasMacStarted()) {
				if (InterruptFlags & INTFLAG_VIA) {
					ClearInterruptFlag(INTFLAG_VIA);
#if !PRECISE_TIMING
					TimerInterrupt();
#endif
					ExecuteNative(NATIVE_VIDEO_VBL);

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
					ExecuteNative(NATIVE_ETHER_IRQ);
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
//			D(bug("SCSIDispatch(%d)\n", sel));
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
					printf("FATAL: SCSIDispatch: illegal selector\n");
					stack = 0;
					//!! SysError(12)
			}
			r->a[0] = ret;
			r->a[7] += stack;
			break;
		}

		case OP_SCSI_ATOMIC:		// SCSIAtomic() replacement
			D(bug("SCSIAtomic\n"));
			r->d[0] = (uint32)-7887;
			break;

		case OP_CHECK_SYSV: {		// Check we are not using MacOS < 8.1 with a NewWorld ROM
			r->a[1] = r->d[1];
			r->a[0] = ReadMacInt32(r->d[1]);
			uint32 sysv = ReadMacInt16(r->a[0]);
			D(bug("Detected MacOS version %d.%d.%d\n", (sysv >> 8) & 0xf, (sysv >> 4) & 0xf, sysv & 0xf));
			if (ROMType == ROMTYPE_NEWWORLD && sysv < 0x0801)
				r->d[1] = 0;
			break;
		}

		case OP_NTRB_17_PATCH:
			r->a[2] = ReadMacInt32(r->a[7]);
			r->a[7] += 4;
			if (ReadMacInt16(r->a[2] + 6) == 17)
				PatchNativeResourceManager();
			break;

		case OP_NTRB_17_PATCH2:
			r->a[7] += 8;
			PatchNativeResourceManager();
			break;

		case OP_NTRB_17_PATCH3:
			r->a[2] = ReadMacInt32(r->a[7]);
			r->a[7] += 4;
		 	D(bug("%d %d\n", ReadMacInt16(r->a[2]), ReadMacInt16(r->a[2] + 6)));
			if (ReadMacInt16(r->a[2]) == 11 && ReadMacInt16(r->a[2] + 6) == 17)
				PatchNativeResourceManager();
			break;

		case OP_NTRB_17_PATCH4: {
			r->d[0] = ReadMacInt16(r->a[7]);
			r->a[7] += 2;
			uint16 ntrb_type = ReadMacInt16(r->a[2]);
			uint16 ntrb_id = ReadMacInt16(r->a[2] + 6);
			static int ntrb4_count = 0;
			if (++ntrb4_count <= 5)
				fprintf(stderr, "[NTRB4] #%d type=%d id=%d\n", ntrb4_count, ntrb_type, ntrb_id);
			if (ntrb_type == 11 && ntrb_id == 17) {
				fprintf(stderr, "[NTRB4] MATCH! Calling PatchNativeResourceManager\n");
				PatchNativeResourceManager();
			}
			break;
		}

		case OP_CHECKLOAD: {		// vCheckLoad() patch
			// Start flight recorder after 1300 checkloads (right before idle loop)
			{
				void kpx_flight_recorder_cmd(int cmd);
				static int cl_count = 0;
				if (++cl_count == 1000) {
					fprintf(stderr, "[FR] Starting flight recorder at CHECKLOAD #1300\n");
					kpx_flight_recorder_cmd(1); // 1=start
				}
			}
			uint32 type = ReadMacInt32(r->a[7]);
			r->a[7] += 4;
			int16 id = ReadMacInt16(r->a[2]);
			if (r->a[0] == 0)
				break;
			uint32 adr = ReadMacInt32(r->a[0]);
			if (adr == 0)
				break;
			uint16 *p = (uint16 *)Mac2HostAddr(adr);
			uint32 size = ReadMacInt32(adr - 8) & 0xffffff;
			D(bug("CHECKLOAD type='%c%c%c%c' id=%d size=%u\n",
					(type>>24)&0xff, (type>>16)&0xff, (type>>8)&0xff, type&0xff, id, size));
			fprintf(stderr, "[CL] type='%c%c%c%c' id=%d size=%u\n", (type>>24)&0xff, (type>>16)&0xff, (type>>8)&0xff, type&0xff, id, size);
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
			// Sleep if no events pending
			if (ReadMacInt32(0x14c) == 0)
				idle_wait();
			r->a[0] = ReadMacInt32(0x2b6);
			break;

		case OP_IDLE_TIME_2:
			// Sleep if no events pending
			if (ReadMacInt32(0x14c) == 0)
				idle_wait();
			r->d[0] = (uint32)-2;
			break;

		default:
			printf("FATAL: EMUL_OP called with bogus selector %08x\n", selector);
			QuitEmulator();
			break;
	}
}
