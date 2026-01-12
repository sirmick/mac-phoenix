/*
 *  rom_patches_aline.cpp - ROM patches using A-line EmulOps
 *
 *  Uses A-line opcodes (0xAE00-0xAE3F) for EmulOps
 *  This provides 2-byte EmulOps that work with Unicorn JIT
 */

#include <string.h>
#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "emul_op.h"
#include "macos_util.h"
#include "slot_rom.h"
#include "sony.h"
#include "disk.h"
#include "cdrom.h"
#include "video.h"
#include "extfs.h"
#include "prefs.h"
#include "rom_patches.h"

#define DEBUG 0
#include "debug.h"

// Helper function from rom_patches.cpp
static uint32 find_rom_data(uint32 start, uint32 end, const uint8 *data, uint32 data_len)
{
	uint32 ofs = start;
	while (ofs < end - data_len) {
		if (!memcmp(ROMBaseHost + ofs, data, data_len))
			return ofs;
		ofs++;
	}
	return 0;
}

// ========================================
// A-Line EmulOp Emission
// ========================================
// Convert legacy 0x71xx opcodes to 0xAExx format
// The lower 6 bits (0x3F) are preserved for the EmulOp number

// Convert legacy EmulOp (0x7100-0x713F) to A-line format (0xAE00-0xAE3F)
static inline uint16 legacy_to_aline(uint16 legacy_opcode)
{
	// Extract EmulOp number (0-63) from 0x71xx
	uint16 emulop_num = legacy_opcode & 0x3F;
	// Return A-line format: 0xAE00 + emulop_num
	return 0xAE00 | emulop_num;
}

// Emit A-line EmulOp (2 bytes)
static inline void emit_emulop(uint16 **wp, uint16 opcode)
{
	uint16 aline_opcode = legacy_to_aline(opcode);
	**wp = htons(aline_opcode);
	(*wp)++;
}

// Driver arrays with A-line EmulOps
static const uint8 sony_driver_aline[] = {	// Replacement for .Sony driver
	// Driver header
	SonyDriverFlags >> 8, SonyDriverFlags & 0xff, 0, 0, 0, 0, 0, 0,
	0x00, 0x18,  // Open() offset  (original 2-byte size)
	0x00, 0x1c,  // Prime() offset
	0x00, 0x20,  // Control() offset
	0x00, 0x26,  // Status() offset
	0x00, 0x2c,  // Close() offset

	// Driver code
	// Open() routine (offset 0x18)
	0x4e, 0x75,	  // RTS

	// Prime() routine (offset 0x1c)
	0xae, 0x0d,   // EmulOp 0xAE0D (SONY_PRIME = 0x710d)
	0x4e, 0x75,	  // RTS

	// Control() routine (offset 0x20)
	0x0c, 0x68, 0x00, 0x01, 0x00, 0x1a,  // CMP.W #1,$1a(A0)
	0xae, 0x0e,   // EmulOp 0xAE0E (SONY_CONTROL = 0x710e)
	0x4e, 0x75,	  // RTS

	// Status() routine (offset 0x26)
	0x3e, 0x38, 0x02, 0x06,  // MOVE.W $0206,D7
	0xae, 0x0f,   // EmulOp 0xAE0F (SONY_STATUS = 0x710f)
	0x4e, 0x75,	  // RTS

	// Close() routine (offset 0x2c)
	0xae, 0x0c,   // EmulOp 0xAE0C (SONY_OPEN = 0x710c)
	0x4e, 0x75,	  // RTS
};

static const uint8 disk_driver_aline[] = {	// Generic disk driver
	// Driver header
	DiskDriverFlags >> 8, DiskDriverFlags & 0xff, 0, 0, 0, 0, 0, 0,
	0x00, 0x18,  // Open() offset
	0x00, 0x1c,  // Prime() offset
	0x00, 0x20,  // Control() offset
	0x00, 0x26,  // Status() offset
	0x00, 0x2c,  // Close() offset

	// Driver code
	// Open() routine
	0x4e, 0x75,	  // RTS

	// Prime() routine
	0xae, 0x11,   // EmulOp 0xAE11 (DISK_PRIME = 0x7111)
	0x4e, 0x75,	  // RTS

	// Control() routine
	0x0c, 0x68, 0x00, 0x01, 0x00, 0x1a,  // CMP.W #1,$1a(A0)
	0xae, 0x12,   // EmulOp 0xAE12 (DISK_CONTROL = 0x7112)
	0x4e, 0x75,	  // RTS

	// Status() routine
	0x3e, 0x38, 0x02, 0x06,  // MOVE.W $0206,D7
	0xae, 0x13,   // EmulOp 0xAE13 (DISK_STATUS = 0x7113)
	0x4e, 0x75,	  // RTS

	// Close() routine
	0xae, 0x10,   // EmulOp 0xAE10 (DISK_OPEN = 0x7110)
	0x4e, 0x75,	  // RTS
};

static const uint8 cdrom_driver_aline[] = {	// CD-ROM driver
	// Driver header
	CDROMDriverFlags >> 8, CDROMDriverFlags & 0xff, 0, 0, 0, 0, 0, 0,
	0x00, 0x1c,  // Open() offset
	0x00, 0x20,  // Prime() offset
	0x00, 0x24,  // Control() offset
	0x00, 0x30,  // Status() offset
	0x00, 0x36,  // Close() offset

	// Driver code
	// Open() routine
	0xae, 0x14,   // EmulOp 0xAE14 (CDROM_OPEN = 0x7114)
	0x4e, 0x75,	  // RTS

	// Prime() routine
	0xae, 0x15,   // EmulOp 0xAE15 (CDROM_PRIME = 0x7115)
	0x4e, 0x75,	  // RTS

	// Control() routine
	0x0c, 0x68, 0x00, 0x01, 0x00, 0x1a,    // CMP.W #1,$1a(A0)
	0x66, 0x04,								// BNE +4
	0x4e, 0x75,								// RTS
	0xae, 0x16,   // EmulOp 0xAE16 (CDROM_CONTROL = 0x7116)
	0x4e, 0x75,	  // RTS

	// Status() routine
	0x3e, 0x38, 0x02, 0x06,  // MOVE.W $0206,D7
	0xae, 0x17,   // EmulOp 0xAE17 (CDROM_STATUS = 0x7117)
	0x4e, 0x75,	  // RTS

	// Close() routine
	0x4e, 0x75,	  // RTS
};

// Check ROM checksum
static bool CheckROMChecksum(void)
{
	uint32 checksum = 0;
	uint32 *p = (uint32 *)ROMBaseHost;
	for (int i = 0; i < ROMSize / 4; i++)
		checksum += ntohl(*p++);
	D(bug("ROM checksum: %08x\n", checksum));
	return checksum == 0;
}

// Patch ROM for A-line EmulOps
bool PatchROM_ALine(void)
{
	D(bug("PatchROM_ALine\n"));

	// ROM is already writable in emulation
	// No need for vm_protect calls

	uint16 *wp;
	uint32 base;

	// ========================================
	// Step 1: Patch boot code
	// ========================================

	// PATCH_BOOT_GLOBS EmulOp at ROM+0x10
	D(bug("Patching boot globals\n"));
	wp = (uint16 *)(ROMBaseHost + 0x10);
	emit_emulop(&wp, M68K_EMUL_OP_PATCH_BOOT_GLOBS);

	// FIX_BOOTSTACK EmulOp at ROM+0x9c
	D(bug("Patching boot stack\n"));
	wp = (uint16 *)(ROMBaseHost + 0x9c);
	emit_emulop(&wp, M68K_EMUL_OP_FIX_BOOTSTACK);

	// ========================================
	// Step 2: Install disk drivers
	// ========================================

	// Replace .Sony driver
	D(bug("Installing Sony driver\n"));
	static const uint8 sony_driver_header[] = {0x4f, 0x00, 0x00, 0x00};
	base = find_rom_data(0, ROMSize, sony_driver_header, 4);
	if (base) {
		memcpy(ROMBaseHost + base, sony_driver_aline, sizeof(sony_driver_aline));
		D(bug(" installed at %08x\n", base));
	}

	// Install disk driver
	D(bug("Installing disk driver\n"));
	// Note: The actual installation location depends on ROM specifics
	// This is a simplified version

	// ========================================
	// Step 3: Patch system calls
	// ========================================

	// Find and patch InitResources
	static const uint8 init_resources_pattern[] = {0x4e, 0x56, 0xff, 0xf8};  // LINK A6,#-8
	base = find_rom_data(0, ROMSize, init_resources_pattern, 4);
	if (base) {
		D(bug("Patching InitResources at %08x\n", base));
		wp = (uint16 *)(ROMBaseHost + base + 6);  // Skip LINK instruction
		emit_emulop(&wp, M68K_EMUL_OP_INSTALL_DRIVERS);
	}

	// ========================================
	// Step 4: Patch memory size detection
	// ========================================

	// Find and patch memory size code
	static const uint8 memsize_pattern[] = {0x22, 0x38, 0x01, 0x08};  // MOVE.L $0108,D1
	base = find_rom_data(0, ROMSize, memsize_pattern, 4);
	if (base) {
		D(bug("Patching memory size at %08x\n", base));
		wp = (uint16 *)(ROMBaseHost + base);
		emit_emulop(&wp, M68K_EMUL_OP_FIX_MEMSIZE);
		*wp++ = htons(0x4e71);  // NOP
	}

	// ========================================
	// Step 5: Patch XPRAM routines
	// ========================================

	// Find and patch ClkNoMem
	static const uint8 clknomem_pattern[] = {0x40, 0xe7, 0x00, 0x7c};  // MOVE SR,-(SP); ORI #$700,SR
	base = find_rom_data(0, ROMSize, clknomem_pattern, 4);
	if (base) {
		D(bug("Patching ClkNoMem at %08x\n", base));
		wp = (uint16 *)(ROMBaseHost + base + 6);  // After ORI
		emit_emulop(&wp, M68K_EMUL_OP_CLKNOMEM);
		// Replace the rest with RTS
		*wp++ = htons(0x4e75);  // RTS
	}

	// Find and patch XPRAM read/write
	static const uint8 xpram_read_pattern[] = {0x08, 0x38, 0x00, 0x04};  // BTST #4,$xxxx
	base = find_rom_data(0, ROMSize, xpram_read_pattern, 4);
	if (base) {
		D(bug("Patching XPRAM read at %08x\n", base));
		wp = (uint16 *)(ROMBaseHost + base);
		emit_emulop(&wp, M68K_EMUL_OP_READ_XPRAM);
		*wp++ = htons(0x4e75);  // RTS
	}

	// ========================================
	// Step 6: Final patches
	// ========================================

	// Patch reset handler
	wp = (uint16 *)(ROMBaseHost + 0x4);  // Reset vector
	uint32 reset_pc = ntohl(*(uint32 *)(ROMBaseHost + 0x4));
	if (reset_pc > 0 && reset_pc < ROMSize) {
		wp = (uint16 *)(ROMBaseHost + reset_pc);
		emit_emulop(&wp, M68K_EMUL_OP_RESET);
	}

	// Check ROM checksum
	D(bug("Checking ROM checksum\n"));
	CheckROMChecksum();

	// ROM protection is handled by the emulator
	// No need for vm_protect calls

	D(bug("ROM patching complete (A-Line)\n"));
	return true;
}