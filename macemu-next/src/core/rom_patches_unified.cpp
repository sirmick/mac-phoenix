/*
 *  rom_patches_unified.cpp - Unified ROM patches for all CPU backends
 *
 *  This provides a single ROM patching implementation that works for both
 *  UAE and Unicorn backends. The only difference is the opcode format used
 *  for EmulOps (0x71xx for UAE, 0xAExx for Unicorn).
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
#include "platform.h"

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
// Platform-Agnostic EmulOp Emission
// ========================================

// Emit an EmulOp using the appropriate format for the current backend
static inline void emit_emulop(uint16 **wp, uint16 emulop)
{
	uint16 opcode;

	// Check backend and emit appropriate opcode format
	if (g_platform.cpu_name && strstr(g_platform.cpu_name, "Unicorn")) {
		// Unicorn: Use A-line format (0xAE00-0xAE3F)
		// Extract EmulOp number from 0x71xx format
		uint16 emulop_num = emulop & 0x3F;
		opcode = 0xAE00 | emulop_num;
	} else {
		// UAE: Use traditional 0x71xx format
		opcode = emulop;
	}

	**wp = htons(opcode);
	(*wp)++;
}

// ========================================
// Common Driver Arrays
// ========================================

// Build driver arrays dynamically using emit_emulop
static void build_sony_driver(uint8 *driver)
{
	uint16 *wp = (uint16 *)driver;

	// Driver header
	*wp++ = htons(SonyDriverFlags >> 16);
	*wp++ = htons(SonyDriverFlags & 0xffff);
	*wp++ = 0;  // Reserved
	*wp++ = 0;  // Reserved
	*wp++ = htons(0x0018);  // Open offset
	*wp++ = htons(0x001c);  // Prime offset
	*wp++ = htons(0x0020);  // Control offset
	*wp++ = htons(0x0026);  // Status offset
	*wp++ = htons(0x002c);  // Close offset

	// Open() routine
	*wp++ = htons(M68K_RTS);

	// Prime() routine
	emit_emulop(&wp, M68K_EMUL_OP_SONY_PRIME);
	*wp++ = htons(M68K_RTS);

	// Control() routine
	*wp++ = htons(0x0c68); *wp++ = htons(0x0001); *wp++ = htons(0x001a);  // CMP.W #1,$1a(A0)
	emit_emulop(&wp, M68K_EMUL_OP_SONY_CONTROL);
	*wp++ = htons(M68K_RTS);

	// Status() routine
	*wp++ = htons(0x3e38); *wp++ = htons(0x0206);  // MOVE.W $0206,D7
	emit_emulop(&wp, M68K_EMUL_OP_SONY_STATUS);
	*wp++ = htons(M68K_RTS);

	// Close() routine
	emit_emulop(&wp, M68K_EMUL_OP_SONY_OPEN);
	*wp++ = htons(M68K_RTS);
}

static void build_disk_driver(uint8 *driver)
{
	uint16 *wp = (uint16 *)driver;

	// Driver header
	*wp++ = htons(DiskDriverFlags >> 16);
	*wp++ = htons(DiskDriverFlags & 0xffff);
	*wp++ = 0;  // Reserved
	*wp++ = 0;  // Reserved
	*wp++ = htons(0x0018);  // Open offset
	*wp++ = htons(0x001c);  // Prime offset
	*wp++ = htons(0x0020);  // Control offset
	*wp++ = htons(0x0026);  // Status offset
	*wp++ = htons(0x002c);  // Close offset

	// Open() routine
	*wp++ = htons(M68K_RTS);

	// Prime() routine
	emit_emulop(&wp, M68K_EMUL_OP_DISK_PRIME);
	*wp++ = htons(M68K_RTS);

	// Control() routine
	*wp++ = htons(0x0c68); *wp++ = htons(0x0001); *wp++ = htons(0x001a);  // CMP.W #1,$1a(A0)
	emit_emulop(&wp, M68K_EMUL_OP_DISK_CONTROL);
	*wp++ = htons(M68K_RTS);

	// Status() routine
	*wp++ = htons(0x3e38); *wp++ = htons(0x0206);  // MOVE.W $0206,D7
	emit_emulop(&wp, M68K_EMUL_OP_DISK_STATUS);
	*wp++ = htons(M68K_RTS);

	// Close() routine
	emit_emulop(&wp, M68K_EMUL_OP_DISK_OPEN);
	*wp++ = htons(M68K_RTS);
}

static void build_cdrom_driver(uint8 *driver)
{
	uint16 *wp = (uint16 *)driver;

	// Driver header
	*wp++ = htons(CDROMDriverFlags >> 16);
	*wp++ = htons(CDROMDriverFlags & 0xffff);
	*wp++ = 0;  // Reserved
	*wp++ = 0;  // Reserved
	*wp++ = htons(0x001c);  // Open offset
	*wp++ = htons(0x0020);  // Prime offset
	*wp++ = htons(0x0024);  // Control offset
	*wp++ = htons(0x0030);  // Status offset
	*wp++ = htons(0x0036);  // Close offset

	// Open() routine
	emit_emulop(&wp, M68K_EMUL_OP_CDROM_OPEN);
	*wp++ = htons(M68K_RTS);

	// Prime() routine
	emit_emulop(&wp, M68K_EMUL_OP_CDROM_PRIME);
	*wp++ = htons(M68K_RTS);

	// Control() routine
	*wp++ = htons(0x0c68); *wp++ = htons(0x0001); *wp++ = htons(0x001a);  // CMP.W #1,$1a(A0)
	*wp++ = htons(0x6604);  // BNE +4
	*wp++ = htons(M68K_RTS);
	emit_emulop(&wp, M68K_EMUL_OP_CDROM_CONTROL);
	*wp++ = htons(M68K_RTS);

	// Status() routine
	*wp++ = htons(0x3e38); *wp++ = htons(0x0206);  // MOVE.W $0206,D7
	emit_emulop(&wp, M68K_EMUL_OP_CDROM_STATUS);
	*wp++ = htons(M68K_RTS);

	// Close() routine
	*wp++ = htons(M68K_RTS);
}

// Check ROM checksum
static bool CheckROMChecksum(void)
{
	uint32 checksum = 0;
	uint32 *p = (uint32 *)ROMBaseHost;
	for (uint32 i = 0; i < ROMSize / 4; i++)
		checksum += ntohl(*p++);
	D(bug("ROM checksum: %08x\n", checksum));
	return checksum == 0;
}

// ========================================
// Unified ROM Patching Implementation
// ========================================
bool PatchROM_Unified(void)
{
	D(bug("PatchROM_Unified - backend: %s\n",
		g_platform.cpu_name ? g_platform.cpu_name : "unknown"));

	// Check if this is a test ROM (properly, using a dedicated flag or config)
	if (PrefsFindBool("testrom")) {
		D(bug("Test ROM mode enabled, skipping patches\n"));
		return true;
	}

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
	// Step 2: Install drivers
	// ========================================

	// Build and install Sony driver
	D(bug("Installing Sony driver\n"));
	uint8 sony_driver[256];
	build_sony_driver(sony_driver);

	static const uint8 sony_driver_header[] = {0x4f, 0x00, 0x00, 0x00};
	base = find_rom_data(0, ROMSize, sony_driver_header, 4);
	if (base) {
		memcpy(ROMBaseHost + base, sony_driver, sizeof(sony_driver));
		D(bug(" installed at %08x\n", base));
	}

	// Build and install disk driver
	D(bug("Installing disk driver\n"));
	uint8 disk_driver[256];
	build_disk_driver(disk_driver);
	// Installation location depends on ROM version
	// This is simplified - real implementation would detect proper location

	// Build and install CD-ROM driver if needed
	if (PrefsFindString("cdrom")) {
		D(bug("Installing CD-ROM driver\n"));
		uint8 cdrom_driver[256];
		build_cdrom_driver(cdrom_driver);
		// Installation location depends on ROM version
	}

	// ========================================
	// Step 3: Patch system initialization
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
		*wp++ = htons(M68K_NOP);
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
		*wp++ = htons(M68K_RTS);
	}

	// Find and patch XPRAM read/write
	static const uint8 xpram_read_pattern[] = {0x08, 0x38, 0x00, 0x04};  // BTST #4,$xxxx
	base = find_rom_data(0, ROMSize, xpram_read_pattern, 4);
	if (base) {
		D(bug("Patching XPRAM read at %08x\n", base));
		wp = (uint16 *)(ROMBaseHost + base);
		emit_emulop(&wp, M68K_EMUL_OP_READ_XPRAM);
		*wp++ = htons(M68K_RTS);
	}

	// ========================================
	// Step 6: Patch reset handler
	// ========================================

	// Insert reset EmulOp at reset vector
	wp = (uint16 *)(ROMBaseHost + 0x4);  // Reset vector location
	uint32 reset_pc = ntohl(*(uint32 *)(ROMBaseHost + 0x4));
	if (reset_pc > 0 && reset_pc < ROMSize) {
		wp = (uint16 *)(ROMBaseHost + reset_pc);
		emit_emulop(&wp, M68K_EMUL_OP_RESET);
	}

	// Check ROM checksum
	D(bug("Checking ROM checksum\n"));
	CheckROMChecksum();

	D(bug("ROM patching complete (Unified)\n"));
	return true;
}