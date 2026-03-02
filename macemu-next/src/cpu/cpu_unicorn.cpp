/**
 * Unicorn CPU Backend for Platform API
 *
 * Wraps Unicorn engine to conform to platform CPU interface.
 * Always available, no compile-time dependencies.
 */

#include "platform.h"
#include "unicorn_wrapper.h"
#include "unicorn_exception.h"
#include "cpu_trace.h"
#include "memory_access.h"  // For direct memory access (UAE-independent)
#include <unicorn/unicorn.h>
#include <unicorn/m68k.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>  // For memset
#include <atomic>
#include <climits>  // For INT_MAX

// Forward declare the webserver running flag from main.cpp
namespace webserver {
	extern std::atomic<bool> g_running;
}

// M68kRegisters structure (from main.h, duplicated to avoid type conflicts)
struct M68kRegisters {
	uint32_t d[8];
	uint32_t a[8];
	uint16_t sr;
};

// Forward declarations (from macemu globals)
extern uint32_t RAMBaseMac;  // RAM base in Mac address space
extern uint8_t *RAMBaseHost; // RAM base in host address space
extern uint32_t RAMSize;     // RAM size
extern uint32_t ROMBaseMac;  // ROM base in Mac address space
extern uint8_t *ROMBaseHost; // ROM base in host address space
extern uint32_t ROMSize;     // ROM size
extern void EmulOp(uint16_t opcode, struct M68kRegisters *r);
extern int CPUType;          // CPU type from config (2=68020, 3=68030, 4=68040)

static UnicornCPU *unicorn_cpu = NULL;
static uint8_t *unicorn_dummy_buffer = NULL;  // Dummy region for UAE out-of-bounds compatibility
static uint8_t *unicorn_high_mem_buffer = NULL;  // High memory region (hardware registers, etc.)
int unicorn_cpu_type = 2;   // Default to 68020 (extern for DualCPU)
int unicorn_fpu_type = 0;   // Default to no FPU (extern for DualCPU)

// ===== Hardware Register (MMIO) Emulation =====
// Quadra 650 hardware registers at 0x50f00000-0x50f3ffff
// Without emulation, reads return 0 (zeroed on-demand mapping), causing
// the ROM to loop forever waiting for hardware responses (e.g., VIA1 RTC).
#define MMIO_HW_BASE  0x50f00000
#define MMIO_HW_SIZE  0x00040000  // 256 KB covers all Quadra 650 hardware

static uint8_t mmio_via1_port_b = 0xFF;  // VIA1 Port B: bits 0=RTC enable, 1=clock, 2=data

// CPU Configuration
static void unicorn_backend_set_type(int cpu_type, int fpu_type) {
	unicorn_cpu_type = cpu_type;
	unicorn_fpu_type = fpu_type;
	fprintf(stderr, "[Unicorn] CPU type set to %d, FPU=%d\n", cpu_type, fpu_type);
}

// For DualCPU backend - simple wrapper function
void unicorn_set_cpu_type(int cpu_type, int fpu_type) {
	unicorn_backend_set_type(cpu_type, fpu_type);
}

// Forward declare the deferred register update mechanisms
extern "C" void unicorn_defer_sr_update(void *unicorn_cpu, uint16_t new_sr);
extern "C" void unicorn_defer_dreg_update(void *unicorn_cpu, int reg, uint32_t value);
extern "C" void unicorn_defer_areg_update(void *unicorn_cpu, int reg, uint32_t value);

// Platform EmulOp handler for Unicorn-only mode
// This is called from within Unicorn's hook context where register writes don't persist
// We need to defer ALL register updates until after uc_emu_start() returns
static bool unicorn_platform_emulop_handler(uint16_t opcode, bool is_primary) {
	(void)is_primary;  // Unicorn is always primary in standalone mode

	// Build M68kRegisters structure from Unicorn state
	struct M68kRegisters regs;
	uint16_t old_sr = unicorn_get_sr(unicorn_cpu);
	uint32_t old_a0 = unicorn_get_areg(unicorn_cpu, 0);
	uint32_t old_a1 = unicorn_get_areg(unicorn_cpu, 1);
	uint32_t old_dregs[8];
	uint32_t old_aregs[8];

	for (int i = 0; i < 8; i++) {
		old_dregs[i] = unicorn_get_dreg(unicorn_cpu, i);
		old_aregs[i] = unicorn_get_areg(unicorn_cpu, i);
		regs.d[i] = old_dregs[i];
		regs.a[i] = old_aregs[i];
	}
	regs.sr = old_sr;

	// Call EmulOp handler
	EmulOp(opcode, &regs);

	// Debug: Log D0 for IRQ EmulOp
	if (opcode == 0x7129) {
		static int irq_d0_log_count = 0;
		if (++irq_d0_log_count <= 50) {
			fprintf(stderr, "[EmulOp IRQ #%d] D0 before=0x%08x, after=0x%08x, changed=%d\n",
			        irq_d0_log_count, old_dregs[0], regs.d[0], (regs.d[0] != old_dregs[0]));
		}
	}

	// CRITICAL: Register writes don't persist when called from within hooks!
	// We need to defer ALL register updates until after uc_emu_start() returns
	for (int i = 0; i < 8; i++) {
		if (regs.d[i] != old_dregs[i]) {
			unicorn_defer_dreg_update(unicorn_cpu, i, regs.d[i]);
		}
		if (regs.a[i] != old_aregs[i]) {
			unicorn_defer_areg_update(unicorn_cpu, i, regs.a[i]);
		}
	}

	if (regs.sr != old_sr) {
		// Defer SR update to be applied after uc_emu_start() returns
		unicorn_defer_sr_update(unicorn_cpu, regs.sr);

		// Debug: Track deferred SR update
		if (opcode == 0x7103) {
			fprintf(stderr, "[EmulOp 0x7103] Deferring SR update: 0x%04X -> 0x%04X\n",
			        old_sr, regs.sr);
		}
	}

	// Check for "rtd" emulation - SCSI_DISPATCH uses A0/A1 to return:
	// A0 = return address, A1 = new stack pointer
	// Only apply this to SCSI_DISPATCH (0x7128) to avoid false positives
	if (opcode == 0x7128 && regs.a[0] != old_a0 && regs.a[1] != old_a1) {
		// This is "rtd" emulation - need to jump to A0 and set SP from A1
		// The ROM patch after SCSI_DISPATCH has:
		//   move.l a1,a7  ; Set stack pointer
		//   jmp (a0)      ; Jump to return address
		// We emulate this directly here

		fprintf(stderr, "[SCSI_DISPATCH] rtd emulation: PC -> 0x%08X, A7 -> 0x%08X\n",
		        regs.a[0], regs.a[1]);

		// Set new PC and stack pointer
		unicorn_set_pc(unicorn_cpu, regs.a[0]);
		unicorn_set_areg(unicorn_cpu, 7, regs.a[1]);

		// Return true to indicate PC was already set (don't advance it)
		return true;
	}

	// Debug: Verify A7 write for RESET EmulOp
	if (opcode == 0x7103) {
		uint32_t a7_readback = g_platform.cpu_get_areg(7);
		fprintf(stderr, "[EmulOp 0x7103] Set A7=0x%08X (readback=0x%08X)\n",
		        regs.a[7], a7_readback);
	}

	// Debug: Log PC and registers after PATCH_BOOT_GLOBS to trace execution flow
	if (opcode == 0x7107) {
		uint32_t current_pc = unicorn_get_pc(unicorn_cpu);
		fprintf(stderr, "[EmulOp 0x7107 (PATCH_BOOT_GLOBS)] Current PC=0x%08X, will return to PC=0x%08X\n",
		        current_pc, current_pc + 2);
		fprintf(stderr, "[PATCH_BOOT_GLOBS] Return state: D0=%08X D1=%08X D2=%08X A0=%08X A1=%08X A2=%08X SR=%04X\n",
		        unicorn_get_dreg(unicorn_cpu, 0), unicorn_get_dreg(unicorn_cpu, 1), unicorn_get_dreg(unicorn_cpu, 2),
		        unicorn_get_areg(unicorn_cpu, 0), unicorn_get_areg(unicorn_cpu, 1), unicorn_get_areg(unicorn_cpu, 2),
		        (uint16_t)(unicorn_get_sr(unicorn_cpu) & 0xFFFF));
	}

	// Return false to indicate PC was not advanced (caller will advance it)
	return false;
}

// Platform trap handler for Unicorn-only mode
// Handles A-line and F-line traps by simulating M68K exceptions
static bool unicorn_platform_trap_handler(int vector, uint16_t opcode, bool is_primary) {
	(void)is_primary; // Unicorn is always primary in standalone mode

	fprintf(stderr, "[DEBUG] Trap handler called: vector=%d, opcode=0x%04X, PC=0x%08X\n",
	        vector, opcode, unicorn_get_pc(unicorn_cpu));

	// Use Unicorn's exception simulation (defined in unicorn_exception.c)
	extern void unicorn_simulate_exception(UnicornCPU *cpu, int vector_nr, uint16_t opcode);
	unicorn_simulate_exception(unicorn_cpu, vector, opcode);

	fprintf(stderr, "[DEBUG] After trap: new PC=0x%08X\n", unicorn_get_pc(unicorn_cpu));

	// Return true to indicate we handled PC advancement
	return true;
}

// Unmapped memory handlers - mimic UAE's dummy_bank behavior
/*
 * Unmapped memory handlers - UAE dummy_bank compatibility
 *
 * UAE's memory system uses dummy_bank for all unmapped addresses:
 * - Reads return 0 (all sizes)
 * - Writes are silently dropped (no-op)
 *
 * This is CRITICAL for correct Mac boot behavior. The ROM probes NuBus slots
 * by writing a test pattern and reading it back. If the read returns the
 * written value, the ROM thinks a NuBus card is present and tries to
 * initialize it. With dummy_bank (reads=0, writes=dropped), the probe
 * correctly detects "no hardware".
 *
 * We use uc_mmio_map to create dummy regions with MMIO callbacks that
 * return 0 for reads and silently ignore writes. This perfectly matches
 * UAE's dummy_bank behavior.
 */

// MMIO callback for dummy_bank reads - always returns 0
static uint64_t dummy_bank_read(uc_engine *uc, uint64_t offset, unsigned size, void *user_data) {
	(void)uc; (void)offset; (void)size; (void)user_data;
	return 0;  // UAE dummy_bank: all reads return 0
}

// MMIO callback for dummy_bank writes - silently dropped
static void dummy_bank_write(uc_engine *uc, uint64_t offset, unsigned size, uint64_t value, void *user_data) {
	(void)uc; (void)offset; (void)size; (void)value; (void)user_data;
	// UAE dummy_bank: all writes silently dropped
}

// On-demand unmapped handlers - fallback for any gaps not covered by pre-mapped dummy_bank.
// Uses zeroed memory with UC_PROT_ALL (writes are stored). This is safe because the big
// NuBus/slot regions are already pre-mapped with MMIO dummy_bank. Any remaining unmapped
// accesses are to non-NuBus regions where storing writes is acceptable.
static bool unicorn_unmapped_read_handler(uc_engine *uc, uc_mem_type type,
                                          uint64_t address, int size,
                                          int64_t value, void *user_data) {
	(void)type; (void)value; (void)user_data;

	static int read_count = 0;
	if (++read_count <= 10) {
		fprintf(stderr, "[Unicorn] Unmapped read at 0x%08lX (size=%d) - mapping zeroed region\n",
		        address, size);
	}

	const uint32_t map_size = 1024 * 1024;
	uint32_t map_base = (address / map_size) * map_size;

	uint8_t *buffer = (uint8_t *)calloc(1, map_size);
	if (!buffer) return false;

	uc_err err = uc_mem_map_ptr(uc, map_base, map_size, UC_PROT_ALL, buffer);
	if (err != UC_ERR_OK) {
		free(buffer);
		return false;
	}

	return true;
}

static bool unicorn_unmapped_write_handler(uc_engine *uc, uc_mem_type type,
                                           uint64_t address, int size,
                                           int64_t value, void *user_data) {
	(void)type; (void)user_data;

	static int write_count = 0;
	if (++write_count <= 10) {
		fprintf(stderr, "[Unicorn] Unmapped write at 0x%08lX (size=%d, value=0x%lX) - mapping zeroed region\n",
		        address, size, (unsigned long)value);
	}

	const uint32_t map_size = 1024 * 1024;
	uint32_t map_base = (address / map_size) * map_size;

	uint8_t *buffer = (uint8_t *)calloc(1, map_size);
	if (!buffer) return false;

	uc_err err = uc_mem_map_ptr(uc, map_base, map_size, UC_PROT_ALL, buffer);
	if (err != UC_ERR_OK) {
		free(buffer);
		return false;
	}

	return true;
}

// ===== MMIO Callback Functions (for uc_mmio_map) =====

// MMIO read callback - returns register values for hardware reads
// Uses uc_cb_mmio_read_t signature: uint64_t(uc_engine*, uint64_t offset, unsigned size, void*)
static uint64_t mmio_read_cb(uc_engine *uc, uint64_t offset, unsigned size, void *user_data) {
	(void)uc; (void)user_data;

	uint8_t val = 0x00;

	// VIA1 (offset 0x0000 - 0x1fff, registers at 0x200-byte intervals)
	if (offset < 0x2000) {
		uint32_t reg = (uint32_t)(offset & 0x1E00);
		switch (reg) {
		case 0x0000: {  // vBufB - Port B (RTC interface)
			// Return last written value with bit 2 (data) forced high.
			// CLKNOMEM EmulOp handles actual XPRAM/RTC operations;
			// the ROM's post-CLKNOMEM VIA1 polling just needs to see
			// the data bit respond so it exits its verification loop.
			val = mmio_via1_port_b | 0x04;
			static int via1_portb_read_count = 0;
			if (++via1_portb_read_count <= 20) {
				fprintf(stderr, "[MMIO] VIA1 PortB read #%d: returning 0x%02x (stored=0x%02x)\n",
				        via1_portb_read_count, val, mmio_via1_port_b);
			}
			break;
		}
		case 0x0200:  // vBufA - Port A
			val = 0x7F;  // Bit 7 low = no ADB interrupt
			break;
		case 0x0400:  // vDirB - Port B direction
			val = 0x87;  // Bits 0-2 output (RTC), bit 7 output
			break;
		case 0x0600:  // vDirA - Port A direction
			val = 0x00;  // All inputs
			break;
		case 0x1A00:  // vIFR - Interrupt Flag Register
			val = 0x00;  // No interrupts pending
			break;
		case 0x1C00:  // vIER - Interrupt Enable Register
			val = 0x00;  // No interrupts enabled
			break;
		default:
			val = 0x00;
			break;
		}
	}
	// VIA2 (offset 0x2000 - 0x3fff)
	else if (offset < 0x4000) {
		uint32_t reg = (uint32_t)((offset - 0x2000) & 0x1E00);
		switch (reg) {
		case 0x0000:  // vBufB
			val = 0xFF;
			break;
		case 0x0200:  // vBufA - slot interrupts
			val = 0xFF;  // No slot interrupts active (active low)
			break;
		case 0x1A00:  // vIFR
			val = 0x00;
			break;
		case 0x1C00:  // vIER
			val = 0x00;
			break;
		default:
			val = 0x00;
			break;
		}
		static int via2_read_count = 0;
		if (++via2_read_count <= 20) {
			fprintf(stderr, "[MMIO] VIA2 read: offset=0x%05lx reg=0x%04x val=0x%02x\n",
			        (unsigned long)offset, reg, val);
		}
	}
	// Other hardware (SCC, SCSI, ASC, video, etc.)
	else {
		val = 0x00;
		static int other_read_count = 0;
		if (++other_read_count <= 50) {
			const char *name = "unknown";
			if (offset >= 0x4000 && offset < 0x6000) name = "SCC";
			else if (offset >= 0x10000 && offset < 0x12000) name = "SCSI";
			else if (offset >= 0x14000 && offset < 0x16000) name = "ASC";
			else if (offset >= 0x24000 && offset < 0x26000) name = "DAFB";
			fprintf(stderr, "[MMIO] %s read: offset=0x%05lx (size=%u)\n",
			        name, (unsigned long)offset, size);
		}
	}

	return (uint64_t)val;
}

// MMIO write callback - tracks CPU writes to hardware registers
// Uses uc_cb_mmio_write_t signature: void(uc_engine*, uint64_t offset, unsigned size, uint64_t value, void*)
static void mmio_write_cb(uc_engine *uc, uint64_t offset, unsigned size, uint64_t value, void *user_data) {
	(void)uc; (void)user_data;

	uint8_t byte_val = (uint8_t)(value & 0xFF);

	// VIA1 (offset 0x0000 - 0x1fff)
	if (offset < 0x2000) {
		uint32_t reg = (uint32_t)(offset & 0x1E00);
		switch (reg) {
		case 0x0000:  // vBufB - Port B (RTC interface)
			mmio_via1_port_b = byte_val;
			break;
		default:
			break;
		}
	}
	// Log writes to other hardware regions
	else {
		static int write_count = 0;
		if (++write_count <= 50) {
			const char *name = "unknown";
			if (offset >= 0x2000 && offset < 0x4000) name = "VIA2";
			else if (offset >= 0x4000 && offset < 0x6000) name = "SCC";
			else if (offset >= 0x10000 && offset < 0x12000) name = "SCSI";
			else if (offset >= 0x14000 && offset < 0x16000) name = "ASC";
			else if (offset >= 0x24000 && offset < 0x26000) name = "DAFB";
			fprintf(stderr, "[MMIO] %s write: offset=0x%05lx = 0x%02x (size=%u)\n",
			        name, (unsigned long)offset, byte_val, size);
		}
	}
}

// CPU Lifecycle
static bool unicorn_backend_init(void) {
	if (unicorn_cpu) {
		return true;  // Already initialized
	}

	// Create Unicorn CPU with model from cpu_set_type()
	// Follow same logic as UAE's cpu_level calculation:
	// - If cpu_type==4: use 68040 (with FPU)
	// - Else if fpu_type: use 68030 (68020 with FPU)
	// - Else if cpu_type>=2: use 68020
	// - Else: use 68000
	// NOTE: Unicorn's CPU table uses array indices, not UC_CPU_M68K enum values!
	// Array order: 0=m68000, 1=m68020, 2=m68030, 3=m68040, 4=m68060...
	int uc_model;
	if (unicorn_cpu_type == 4) {
		uc_model = 3;  // 68040 (array index)
	} else {
		if (unicorn_fpu_type)
			uc_model = 2;  // 68030 (array index)
		else if (unicorn_cpu_type >= 2)
			uc_model = 1;  // 68020 (array index)
		else
			uc_model = 0;  // 68000 (array index)
	}

	fprintf(stderr, "[Unicorn] Creating CPU with model %d (array index, cpu_type=%d, fpu=%d) - matches UAE cpu_level\n",
		uc_model, unicorn_cpu_type, unicorn_fpu_type);
	unicorn_cpu = unicorn_create_with_model(UCPU_ARCH_M68K, uc_model);
	if (!unicorn_cpu) {
		fprintf(stderr, "Failed to create Unicorn CPU\n");
		return false;
	}

	// Map RAM to Unicorn
	fprintf(stderr, "[DEBUG] Mapping RAM to unicorn_cpu=%p: Mac=0x%08X Host=%p Size=0x%08X (%u MB)\n",
		(void*)unicorn_cpu, RAMBaseMac, RAMBaseHost, RAMSize, RAMSize / (1024*1024));
	if (!unicorn_map_ram(unicorn_cpu, RAMBaseMac, RAMBaseHost, RAMSize)) {
		fprintf(stderr, "Failed to map RAM to Unicorn\n");
		unicorn_destroy(unicorn_cpu);
		unicorn_cpu = NULL;
		return false;
	}

	// Map ROM as writable (BasiliskII patches ROM during boot)
	fprintf(stderr, "[DEBUG] Mapping ROM to unicorn_cpu=%p: Mac=0x%08X Host=%p Size=0x%08X (%u KB)\n",
		(void*)unicorn_cpu, ROMBaseMac, ROMBaseHost, ROMSize, ROMSize / 1024);
	if (!unicorn_map_rom_writable(unicorn_cpu, ROMBaseMac, ROMBaseHost, ROMSize)) {
		fprintf(stderr, "Failed to map ROM to Unicorn\n");
		unicorn_destroy(unicorn_cpu);
		unicorn_cpu = NULL;
		return false;
	}

	// Map dummy region after ROM to handle UAE's out-of-bounds reads
	// UAE has a bug where it reads past ROM end without bounds checking
	// We need to provide the same memory layout that UAE sees for compatibility
	uint32_t dummy_region_base = ROMBaseMac + ROMSize;
	uint32_t dummy_region_size = 16 * 1024 * 1024;  // 16 MB
	unicorn_dummy_buffer = (uint8_t *)malloc(dummy_region_size);
	if (!unicorn_dummy_buffer) {
		fprintf(stderr, "Failed to allocate dummy region buffer\n");
		unicorn_destroy(unicorn_cpu);
		unicorn_cpu = NULL;
		return false;
	}
	// Fill with 0xFF00FF00 pattern (same as UAE reads from uninitialized memory)
	// Write in big-endian format for M68K
	for (uint32_t i = 0; i < dummy_region_size; i += 4) {
		unicorn_dummy_buffer[i + 0] = 0xFF;  // MSB
		unicorn_dummy_buffer[i + 1] = 0x00;
		unicorn_dummy_buffer[i + 2] = 0xFF;
		unicorn_dummy_buffer[i + 3] = 0x00;  // LSB
	}
	if (!unicorn_map_ram(unicorn_cpu, dummy_region_base, unicorn_dummy_buffer, dummy_region_size)) {
		fprintf(stderr, "Failed to map dummy region to Unicorn\n");
		free(unicorn_dummy_buffer);
		unicorn_dummy_buffer = NULL;
		unicorn_destroy(unicorn_cpu);
		unicorn_cpu = NULL;
		return false;
	}
	fprintf(stderr, "[DEBUG] Dummy region mapped: 0x%08X - 0x%08X (%u MB) with 0xFF00FF00 pattern\n",
		dummy_region_base, dummy_region_base + dummy_region_size, dummy_region_size / (1024*1024));

	// Map high memory region (0xF0000000-0xFFFFFFFF) for hardware registers
	// BUT SKIP THE TRAP REGION (0xFF000000-0xFF000FFF) for MMIO trap handling
	// This matches UAE's behavior where the entire 4GB address space is backed by dummy_bank
	// Addresses like 0xFFFFFFFE/0xFFFFFFFC are common hardware register placeholders

	// First part: 0xF0000000 - 0xFEFFFFFF (255 MB)
	uint32_t high_mem_base1 = 0xF0000000;
	uint32_t high_mem_size1 = 0x0F000000;  // 240 MB
	unicorn_high_mem_buffer = (uint8_t *)malloc(high_mem_size1);
	if (!unicorn_high_mem_buffer) {
		fprintf(stderr, "Failed to allocate high memory region buffer\n");
		free(unicorn_dummy_buffer);
		unicorn_dummy_buffer = NULL;
		unicorn_destroy(unicorn_cpu);
		unicorn_cpu = NULL;
		return false;
	}
	// Fill with zeros (UAE's dummy_bank returns 0 for reads)
	memset(unicorn_high_mem_buffer, 0, high_mem_size1);
	if (!unicorn_map_ram(unicorn_cpu, high_mem_base1, unicorn_high_mem_buffer, high_mem_size1)) {
		fprintf(stderr, "Failed to map high memory region part 1 to Unicorn\n");
		free(unicorn_high_mem_buffer);
		unicorn_high_mem_buffer = NULL;
		free(unicorn_dummy_buffer);
		unicorn_dummy_buffer = NULL;
		unicorn_destroy(unicorn_cpu);
		unicorn_cpu = NULL;
		return false;
	}
	fprintf(stderr, "[DEBUG] High memory region part 1 mapped: 0x%08X - 0x%08X (%u MB)\n",
		high_mem_base1, high_mem_base1 + high_mem_size1 - 1, high_mem_size1 / (1024*1024));

	// Skip trap region 0xFF000000-0xFF000FFF
	// Second part: 0xFF001000 - 0xFFFFFFFF (15 MB + 1020 KB)
	uint32_t high_mem_base2 = 0xFF001000;
	uint32_t high_mem_size2 = 0x00FFF000;  // ~16 MB - 4KB
	uint8_t *high_mem_buffer2 = (uint8_t *)malloc(high_mem_size2);
	if (!high_mem_buffer2) {
		fprintf(stderr, "Failed to allocate high memory region buffer 2\n");
		free(unicorn_high_mem_buffer);
		unicorn_high_mem_buffer = NULL;
		free(unicorn_dummy_buffer);
		unicorn_dummy_buffer = NULL;
		unicorn_destroy(unicorn_cpu);
		unicorn_cpu = NULL;
		return false;
	}
	// Fill with zeros
	memset(high_mem_buffer2, 0, high_mem_size2);
	if (!unicorn_map_ram(unicorn_cpu, high_mem_base2, high_mem_buffer2, high_mem_size2)) {
		fprintf(stderr, "Failed to map high memory region part 2 to Unicorn\n");
		free(high_mem_buffer2);
		free(unicorn_high_mem_buffer);
		unicorn_high_mem_buffer = NULL;
		free(unicorn_dummy_buffer);
		unicorn_dummy_buffer = NULL;
		unicorn_destroy(unicorn_cpu);
		unicorn_cpu = NULL;
		return false;
	}
	fprintf(stderr, "[DEBUG] High memory region part 2 mapped: 0x%08X - 0x%08X (%u MB) - TRAP REGION 0xFF000000-0xFF000FFF LEFT UNMAPPED\n",
		high_mem_base2, high_mem_base2 + high_mem_size2 - 1, high_mem_size2 / (1024*1024));

	// Map hardware register region using uc_mmio_map for proper MMIO emulation
	// uc_mmio_map provides read/write callbacks that are always invoked by the JIT,
	// unlike UC_HOOK_MEM_READ which is bypassed for uc_mem_map_ptr regions.
	{
		uc_engine *mmio_uc = (uc_engine *)unicorn_get_uc(unicorn_cpu);
		uc_err mmio_err = uc_mmio_map(mmio_uc, MMIO_HW_BASE, MMIO_HW_SIZE,
		                              mmio_read_cb, NULL,
		                              mmio_write_cb, NULL);
		if (mmio_err != UC_ERR_OK) {
			fprintf(stderr, "[MMIO] Warning: Failed to map MMIO region: %s\n", uc_strerror(mmio_err));
		} else {
			fprintf(stderr, "[MMIO] Hardware region mapped via uc_mmio_map: 0x%08X - 0x%08X (%u KB)\n",
				MMIO_HW_BASE, MMIO_HW_BASE + MMIO_HW_SIZE - 1, MMIO_HW_SIZE / 1024);
		}
	}

	// Pre-map NuBus/slot address space gaps with MMIO dummy_bank
	// This is CRITICAL for boot: ROM probes for NuBus cards using write-then-read tests.
	// UAE's dummy_bank silently drops writes and returns 0 for reads, so ROM sees no cards.
	// Without this, Unicorn's on-demand handler maps with UC_PROT_ALL which STORES writes,
	// causing ROM to detect phantom NuBus cards → phantom drivers → I/O stall.
	{
		uc_engine *gap_uc = (uc_engine *)unicorn_get_uc(unicorn_cpu);

		// Gap 1: After post-ROM dummy region to before MMIO hardware
		// 0x03100000 - 0x50EFFFFF
		uint32_t gap1_base = dummy_region_base + dummy_region_size;  // 0x03100000
		uint32_t gap1_size = MMIO_HW_BASE - gap1_base;              // 0x4DE00000
		uc_err gap_err = uc_mmio_map(gap_uc, gap1_base, gap1_size,
		                             dummy_bank_read, NULL,
		                             dummy_bank_write, NULL);
		if (gap_err != UC_ERR_OK) {
			fprintf(stderr, "[MMIO] Warning: Failed to map NuBus gap 1 (0x%08X-0x%08X): %s\n",
				gap1_base, gap1_base + gap1_size - 1, uc_strerror(gap_err));
		} else {
			fprintf(stderr, "[MMIO] NuBus gap 1 mapped as dummy_bank: 0x%08X - 0x%08X (%.0f MB)\n",
				gap1_base, gap1_base + gap1_size - 1, gap1_size / (1024.0*1024.0));
		}

		// Gap 2: After MMIO hardware to before high memory region
		// 0x50F40000 - 0xEFFFFFFF
		uint32_t gap2_base = MMIO_HW_BASE + MMIO_HW_SIZE;           // 0x50F40000
		uint32_t gap2_size = high_mem_base1 - gap2_base;             // 0x9F0C0000
		gap_err = uc_mmio_map(gap_uc, gap2_base, gap2_size,
		                      dummy_bank_read, NULL,
		                      dummy_bank_write, NULL);
		if (gap_err != UC_ERR_OK) {
			fprintf(stderr, "[MMIO] Warning: Failed to map NuBus gap 2 (0x%08X-0x%08X): %s\n",
				gap2_base, gap2_base + gap2_size - 1, uc_strerror(gap_err));
		} else {
			fprintf(stderr, "[MMIO] NuBus gap 2 mapped as dummy_bank: 0x%08X - 0x%08X (%.0f MB)\n",
				gap2_base, gap2_base + gap2_size - 1, gap2_size / (1024.0*1024.0));
		}
	}

	// Register unmapped memory hooks as fallback for any remaining unmapped regions
	// This matches UAE's behavior where ALL unmapped memory returns 0 / ignores writes
	uc_engine *uc = (uc_engine *)unicorn_get_uc(unicorn_cpu);
	uc_hook unmapped_read_hook, unmapped_write_hook;
	uc_err err = uc_hook_add(uc, &unmapped_read_hook,
	                         UC_HOOK_MEM_READ_UNMAPPED,
	                         (void*)unicorn_unmapped_read_handler,
	                         NULL, 1, 0);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "Warning: Failed to register unmapped read hook: %s\n", uc_strerror(err));
		// Not fatal - high memory region should cover most cases
	}
	err = uc_hook_add(uc, &unmapped_write_hook,
	                  UC_HOOK_MEM_WRITE_UNMAPPED,
	                  (void*)unicorn_unmapped_write_handler,
	                  NULL, 1, 0);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "Warning: Failed to register unmapped write hook: %s\n", uc_strerror(err));
	}

	fprintf(stderr, "[DEBUG] Unmapped memory hooks registered - MMIO dummy_bank for UAE compatibility\n");

	fprintf(stderr, "[DEBUG] unicorn_cpu instance at init: %p\n", (void*)unicorn_cpu);

	// Initialize CPU tracing from environment variable
	cpu_trace_init();

	// Register atexit handler to print block statistics on exit
	static bool atexit_registered = false;
	if (!atexit_registered) {
		atexit([]() {
			if (unicorn_cpu) {
				unicorn_print_block_stats(unicorn_cpu);
			}
		});
		atexit_registered = true;
	}

	// Register EmulOp handler via platform API
	// EmulOps are handled by UC_HOOK_INSN_INVALID which checks g_platform handlers
	g_platform.emulop_handler = unicorn_platform_emulop_handler;

	// Register trap handler for A-line/F-line traps
	// Traps are handled by UC_HOOK_INSN_INVALID which checks g_platform handlers
	g_platform.trap_handler = unicorn_platform_trap_handler;

	// NOTE: Legacy per-CPU exception handler API removed - UC_HOOK_INSN_INVALID
	// automatically checks g_platform.trap_handler for A-line/F-line exceptions

	return true;
}

static void unicorn_backend_reset(void) {
	if (!unicorn_cpu) return;

	// M68K reset: Initialize registers to power-on state
	// IMPORTANT: Set PC and SR first, THEN registers
	// Setting PC may clear A7 in Unicorn, so A7 must be set after PC
	unicorn_set_pc(unicorn_cpu, ROMBaseMac + 0x2a);
	unicorn_set_sr(unicorn_cpu, 0x2700);  // S=1, I=111

	for (int i = 0; i < 8; i++) {
		unicorn_set_dreg(unicorn_cpu, i, 0);
		unicorn_set_areg(unicorn_cpu, i, 0);
	}

	// Set A7 (SSP) after PC to avoid it being cleared
	unicorn_set_areg(unicorn_cpu, 7, 0x2000);

	// Initialize control registers (68040)
	uc_engine *uc = (uc_engine *)unicorn_get_uc(unicorn_cpu);
	uint32_t zero = 0;
	uc_reg_write(uc, UC_M68K_REG_CR_VBR, &zero);   // Vector Base Register = 0
	uc_reg_write(uc, UC_M68K_REG_CR_CACR, &zero);  // Cache Control Register = 0

	// Verify VBR was actually set to 0
	uint32_t vbr_readback = 0;
	uc_reg_read(uc, UC_M68K_REG_CR_VBR, &vbr_readback);
	fprintf(stderr, "[Unicorn] Reset: VBR=0 (readback=0x%08X), CACR=0\n", vbr_readback);
}

static void unicorn_backend_destroy(void) {
	if (unicorn_cpu) {
		// Print block statistics before destroying
		unicorn_print_block_stats(unicorn_cpu);

		unicorn_destroy(unicorn_cpu);
		unicorn_cpu = NULL;
	}
	if (unicorn_dummy_buffer) {
		free(unicorn_dummy_buffer);
		unicorn_dummy_buffer = NULL;
	}
	if (unicorn_high_mem_buffer) {
		free(unicorn_high_mem_buffer);
		unicorn_high_mem_buffer = NULL;
	}
}

// Execution
static int unicorn_backend_execute_one(void) {
	if (!unicorn_cpu) {
		return 3;  // CPU_EXEC_EXCEPTION
	}

	// Debug: Check PC at entry
	static int exec_one_count = 0;
	exec_one_count++;
	if (exec_one_count <= 5) {
		uint32_t pc_entry = unicorn_get_pc(unicorn_cpu);
		fprintf(stderr, "[execute_one #%d] Entry PC=0x%08X\n", exec_one_count, pc_entry);
	}

	/* CPU tracing (controlled by CPU_TRACE env var) */
	if (cpu_trace_should_log()) {
		// Safety check
		if (!unicorn_cpu) {
			fprintf(stderr, "[CPU TRACE] ERROR: unicorn_cpu is NULL!\n");
			cpu_trace_increment();
			return 0;
		}

		uint32_t pc = unicorn_get_pc(unicorn_cpu);

		// Debug: Check for wrong PC
		static int trace_count = 0;
		if (++trace_count <= 5) {
			fprintf(stderr, "[CPU TRACE #%d] About to trace PC=0x%08X, unicorn_cpu=%p\n",
			        trace_count, pc, (void*)unicorn_cpu);
		}

		uint16_t opcode = 0;
		uc_mem_read((uc_engine*)unicorn_get_uc(unicorn_cpu), pc, &opcode, sizeof(opcode));
		#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
		opcode = __builtin_bswap16(opcode);
		#endif

		cpu_trace_log_detailed(
			"Unicorn",
			pc, opcode,
			unicorn_get_dreg(unicorn_cpu, 0),
			unicorn_get_dreg(unicorn_cpu, 1),
			unicorn_get_dreg(unicorn_cpu, 2),
			unicorn_get_dreg(unicorn_cpu, 3),
			unicorn_get_dreg(unicorn_cpu, 4),
			unicorn_get_dreg(unicorn_cpu, 5),
			unicorn_get_dreg(unicorn_cpu, 6),
			unicorn_get_dreg(unicorn_cpu, 7),
			unicorn_get_areg(unicorn_cpu, 0),
			unicorn_get_areg(unicorn_cpu, 1),
			unicorn_get_areg(unicorn_cpu, 2),
			unicorn_get_areg(unicorn_cpu, 3),
			unicorn_get_areg(unicorn_cpu, 4),
			unicorn_get_areg(unicorn_cpu, 5),
			unicorn_get_areg(unicorn_cpu, 6),
			unicorn_get_areg(unicorn_cpu, 7),
			unicorn_get_sr(unicorn_cpu)
		);
	}

	/* NOTE: Batch execution with timeout support
	 *
	 * The RTE (Return from Exception) bug has been fixed by modifying Unicorn's
	 * cpu-exec.c to handle EXCP_RTE (0x100) specially. When RTE is encountered,
	 * m68k_interrupt_all() is called directly BEFORE clearing exception_index,
	 * which updates the PC from the stack correctly.
	 *
	 * We use a smaller batch size (1000) to match UAE's tick checking frequency.
	 * This ensures the timeout mechanism works correctly and the emulator can
	 * exit cleanly when EMULATOR_TIMEOUT is set.
	 *
	 * UAE checks every 1000 instructions via cpu_do_check_ticks(), so we match
	 * that behavior here for consistency across backends.
	 *
	 * IMPORTANT: When CPU tracing is enabled, execute only 1 instruction at a time
	 * so that the trace log accurately captures every instruction.
	 *
	 * See: external/unicorn/qemu/accel/tcg/cpu-exec.c (TARGET_M68K section)
	 * See: docs/deepdive/UnicornBatchExecutionRTEBug.md
	 */
	int count = cpu_trace_is_enabled() ? 1 : INT_MAX;  // Let the inner loop handle all batching

	// Phase 2: Use QEMU-style execution loop with interrupt checking
	int result = unicorn_execute_with_interrupts(unicorn_cpu, count);
	if (result < 0) {
		uint32_t pc = unicorn_get_pc(unicorn_cpu);
		uint32_t a7 = unicorn_get_areg(unicorn_cpu, 7);
		const char *err_str = unicorn_get_error(unicorn_cpu);

		/* Try to read the opcode at PC */
		uint16_t opcode_be = 0;
		uc_engine *uc = (uc_engine*)unicorn_get_uc(unicorn_cpu);
		uc_mem_read(uc, pc, &opcode_be, 2);
		uint16_t opcode = __builtin_bswap16(opcode_be);

		fprintf(stderr, "Unicorn execution failed: %s (unicorn_cpu=%p)\n",
			err_str, (void*)unicorn_cpu);
		fprintf(stderr, "PC=0x%08X opcode=0x%04X A7=0x%08X\n", pc, opcode, a7);

		/* If it's RTE, dump the stack frame for debugging */
		if (opcode == 0x4E73) {
			uint16_t sr_be, fv_be;
			uint32_t ret_pc_be;
			uc_mem_read(uc, a7, &sr_be, 2);
			uc_mem_read(uc, a7+2, &ret_pc_be, 4);
			uc_mem_read(uc, a7+6, &fv_be, 2);

			fprintf(stderr, "[RTE FAIL] Stack: SR=0x%04X PC=0x%08X FV=0x%04X\n",
				__builtin_bswap16(sr_be), __builtin_bswap32(ret_pc_be), __builtin_bswap16(fv_be));
		}

		return 3;  // CPU_EXEC_EXCEPTION
	}

	cpu_trace_increment();

	// Debug: Check PC at exit
	if (exec_one_count <= 5) {
		uint32_t pc_exit = unicorn_get_pc(unicorn_cpu);
		fprintf(stderr, "[execute_one #%d] Exit PC=0x%08X\n", exec_one_count, pc_exit);
	}

	// Unicorn doesn't track STOP state separately
	return 0;  // CPU_EXEC_OK
}

static void unicorn_backend_execute_fast(void) {
	if (!unicorn_cpu) return;

	// Fast execution path using JIT
	// Execute until stopped (runs continuously)
	// EmulOps are handled by checking at block boundaries via UC_HOOK_BLOCK
	// This is much faster than checking every instruction

	// Use global running flag from webserver namespace (same as main.cpp)
	int exec_count = 0;
	while (webserver::g_running.load(std::memory_order_acquire)) {
		// Phase 2: Use QEMU-style execution loop which handles interrupt checking
		// Execute a large batch - the inner loop will handle proper batching and interrupts
		int result = unicorn_execute_with_interrupts(unicorn_cpu, 100000000);  // Very large, let inner loop control
		if (result < 0) {
			// Check if this was a stop request (e.g., from uc_emu_stop) or an error
			uint32_t pc = unicorn_get_pc(unicorn_cpu);
			const char *err = unicorn_get_error(unicorn_cpu);

			// Debug: Log all stops
			static int stop_count = 0;
			if (++stop_count <= 10) {
				fprintf(stderr, "[unicorn_backend_execute] Stop #%d at PC=0x%08X, error: %s\n",
				        stop_count, pc, err ? err : "NULL");
			}

			// If no error, it was probably uc_emu_stop() - continue execution
			if (!err || strcmp(err, "OK") == 0) {
				// This is normal - likely from uc_emu_stop() after exception handling
				// Continue execution from the new PC
				if (stop_count <= 10) {
					fprintf(stderr, "[unicorn_backend_execute] Continuing execution from PC=0x%08X\n", pc);
				}
				continue;
			}

			// Real error - stop execution
			fprintf(stderr, "[unicorn_backend_execute] ERROR: Stopped at PC=0x%08X after %d executions: %s\n",
			        pc, exec_count, err);
			break;
		}
		exec_count++;

		// Debug: Check if we're stuck
		if (exec_count > 0 && exec_count % 100 == 0) {
			uint32_t pc = unicorn_get_pc(unicorn_cpu);
			if (exec_count == 100) {
				fprintf(stderr, "[unicorn_backend_execute] Still running: PC=0x%08X, %d instructions executed\n",
				        pc, exec_count);
			}
		}
	}
}

// State Query
static bool unicorn_backend_is_stopped(void) {
	// Unicorn doesn't track STOP state
	return false;
}

static uint32_t unicorn_backend_get_pc(void) {
	if (!unicorn_cpu) return 0;
	return unicorn_get_pc(unicorn_cpu);
}

static uint16_t unicorn_backend_get_sr(void) {
	if (!unicorn_cpu) return 0;
	return unicorn_get_sr(unicorn_cpu);
}

static uint32_t unicorn_backend_get_dreg(int n) {
	if (!unicorn_cpu) return 0;
	return unicorn_get_dreg(unicorn_cpu, n);
}

static uint32_t unicorn_backend_get_areg(int n) {
	if (!unicorn_cpu) return 0;
	return unicorn_get_areg(unicorn_cpu, n);
}

// State Modification
static void unicorn_backend_set_pc(uint32_t pc) {
	if (!unicorn_cpu) return;
	unicorn_set_pc(unicorn_cpu, pc);
}

static void unicorn_backend_set_sr(uint16_t sr) {
	if (!unicorn_cpu) return;
	unicorn_set_sr(unicorn_cpu, sr);
}

static void unicorn_backend_set_dreg(int n, uint32_t val) {
	if (!unicorn_cpu) return;
	unicorn_set_dreg(unicorn_cpu, n, val);
}

static void unicorn_backend_set_areg(int n, uint32_t val) {
	if (!unicorn_cpu) return;
	unicorn_set_areg(unicorn_cpu, n, val);
}

// Memory Access
static void unicorn_backend_mem_read(uint32_t addr, void *data, uint32_t size) {
	if (!unicorn_cpu) return;
	unicorn_mem_read(unicorn_cpu, addr, data, size);
}

static void unicorn_backend_mem_write(uint32_t addr, const void *data, uint32_t size) {
	if (!unicorn_cpu) return;
	unicorn_mem_write(unicorn_cpu, addr, data, size);
}

// Interrupts
static void unicorn_backend_trigger_interrupt(int level) {
	/* Trigger interrupt via internal wrapper function
	 * This sets g_pending_interrupt_level which will be checked
	 * by hook_block() on the next basic block execution.
	 *
	 * Unicorn uses manual interrupt handling:
	 * - hook_block() checks g_pending_interrupt_level
	 * - Manually builds M68K exception stack frame (SR, PC)
	 * - Sets supervisor mode, updates interrupt mask
	 * - Reads vector table and jumps to handler
	 * - RTE is handled by Unicorn's QEMU M68K implementation
	 */
	unicorn_trigger_interrupt_internal(level);
}

// 68k Trap Execution - Unicorn native implementation
// This allows ROM patches to call Mac OS traps without depending on UAE CPU backend
//
// CRITICAL: Must save/restore ALL CPU registers to avoid corrupting main execution state.
// The M68kRegisters struct from callers is often partially initialized (e.g., only d[0] set),
// so we must NOT blindly write uninitialized fields into the CPU. Instead, we only write
// registers from `r` that the caller might use, and we ALWAYS restore all state afterward.
// This matches BasiliskII's UAE backend which saves/restores all registers around trap execution.
static void unicorn_backend_execute_68k_trap(uint16_t trap, struct M68kRegisters *r) {
	if (!unicorn_cpu) {
		fprintf(stderr, "[ERROR] unicorn_backend_execute_68k_trap: Unicorn CPU not initialized\n");
		return;
	}

	// Save ALL CPU state - we MUST restore everything after trap execution
	// to avoid corrupting the main execution's register state.
	uint32_t saved_pc = unicorn_get_pc(unicorn_cpu);
	uint16_t saved_sr = unicorn_get_sr(unicorn_cpu);
	uint32_t saved_dregs[8], saved_aregs[8];
	for (int i = 0; i < 8; i++) {
		saved_dregs[i] = unicorn_get_dreg(unicorn_cpu, i);
		saved_aregs[i] = unicorn_get_areg(unicorn_cpu, i);
	}

	// Write caller's register values to CPU for parameter passing.
	// Some fields may be uninitialized (e.g., r2 in IRQ handler only sets d[0]),
	// but that's OK because the trap handler saves/restores its own working registers,
	// and we RESTORE all saved state after the trap returns.
	for (int i = 0; i < 8; i++) {
		unicorn_set_dreg(unicorn_cpu, i, r->d[i]);
	}
	for (int i = 0; i < 7; i++) {
		unicorn_set_areg(unicorn_cpu, i, r->a[i]);
	}
	// DON'T set A7 or SR from r - use saved state for stack pointer

	static int exec68k_count = 0;
	++exec68k_count;
	if (exec68k_count <= 50) {
		fprintf(stderr, "[Execute68kTrap] #%d trap=0x%04x A7=0x%08x PC=0x%08x SR=0x%04x\n",
		        exec68k_count, trap, saved_aregs[7], saved_pc, saved_sr);
	}

	// Push trap number and M68K_EXEC_RETURN on stack
	// For Unicorn: use 0xAE00 (A-line EmulOp) instead of 0x7100 (which is MOVEQ in QEMU)
	uint32_t sp = saved_aregs[7];

	sp -= 2;
	uint16_t sentinel = 0xAE00;  // M68K_EXEC_RETURN in Unicorn encoding
	g_platform.mem_write_word(sp, sentinel);
	sp -= 2;
	g_platform.mem_write_word(sp, trap);    // Trap number (A-line opcode)
	unicorn_set_areg(unicorn_cpu, 7, sp);

	// Set PC to stack (CPU will fetch trap opcode and execute it)
	unicorn_set_pc(unicorn_cpu, sp);

	// Execute until hook_interrupt sees EXEC_RETURN (0xAE00) and stops
	extern volatile bool g_exec68k_return_flag;
	g_exec68k_return_flag = false;

	uc_engine *uc = (uc_engine *)unicorn_get_uc(unicorn_cpu);
	bool returned = false;
	int max_iterations = 100000;
	int iterations = 0;

	while (!returned && iterations < max_iterations) {
		uint32_t pc = unicorn_get_pc(unicorn_cpu);
		uc_err err = uc_emu_start(uc, pc, 0xFFFFFFFF, 0, 0);

		if (err != UC_ERR_OK) {
			fprintf(stderr, "[ERROR] Execute68kTrap failed at PC=0x%08X: %s\n",
			        pc, uc_strerror(err));
			break;
		}

		if (g_exec68k_return_flag) {
			returned = true;
			g_exec68k_return_flag = false;
		}

		iterations++;
	}

	if (!returned) {
		fprintf(stderr, "[ERROR] Execute68kTrap did not return after %d iterations\n", iterations);
	}

	// Get registers back from Unicorn (results of the trap execution)
	for (int i = 0; i < 8; i++) {
		r->d[i] = unicorn_get_dreg(unicorn_cpu, i);
		r->a[i] = unicorn_get_areg(unicorn_cpu, i);
	}
	r->sr = unicorn_get_sr(unicorn_cpu);

	// RESTORE ALL CPU state - this is the critical fix.
	// Without this, partially-initialized M68kRegisters structs (like the one in
	// the IRQ handler's DoVBLTask call) corrupt D0-D7/A0-A6 with stack garbage.
	for (int i = 0; i < 8; i++) {
		unicorn_set_dreg(unicorn_cpu, i, saved_dregs[i]);
		unicorn_set_areg(unicorn_cpu, i, saved_aregs[i]);
	}
	unicorn_set_sr(unicorn_cpu, saved_sr);
	unicorn_set_pc(unicorn_cpu, saved_pc);
}

// 68k Subroutine Execution - Unicorn native implementation
// Similar to Execute68kTrap but executes code at a given address instead of a trap
// Used by TimerInterrupt(), ADBInterrupt(), etc.
//
// CRITICAL: Must save/restore ALL CPU registers (same as Execute68kTrap).
static void unicorn_backend_execute_68k(uint32_t addr, struct M68kRegisters *r) {
	if (!unicorn_cpu) {
		fprintf(stderr, "[ERROR] unicorn_backend_execute_68k: Unicorn CPU not initialized\n");
		return;
	}

	// Save ALL CPU state
	uint32_t saved_pc = unicorn_get_pc(unicorn_cpu);
	uint16_t saved_sr = unicorn_get_sr(unicorn_cpu);
	uint32_t saved_dregs[8], saved_aregs[8];
	for (int i = 0; i < 8; i++) {
		saved_dregs[i] = unicorn_get_dreg(unicorn_cpu, i);
		saved_aregs[i] = unicorn_get_areg(unicorn_cpu, i);
	}

	// Push M68K_EXEC_RETURN sentinel on stack, then push a fake return address
	// that points to the sentinel. When the subroutine does RTS, it pops
	// the return address and jumps to the sentinel opcode.
	uint32_t sp = saved_aregs[7];

	static int exec68k_sub_count = 0;
	bool do_log = (++exec68k_sub_count <= 20);
	if (do_log) {
		fprintf(stderr, "[Execute68k] #%d addr=0x%08x A7=0x%08x PC=0x%08x\n",
		        exec68k_sub_count, addr, sp, saved_pc);
	}

	// Push sentinel (EXEC_RETURN EmulOp)
	sp -= 2;
	uint16_t sentinel = 0xAE00;  // M68K_EXEC_RETURN in Unicorn encoding
	g_platform.mem_write_word(sp, sentinel);

	// Push return address pointing to the sentinel we just wrote
	sp -= 4;
	g_platform.mem_write_long(sp, sp + 4);  // Return addr = address of sentinel

	unicorn_set_areg(unicorn_cpu, 7, sp);

	// Set PC to the subroutine address
	unicorn_set_pc(unicorn_cpu, addr);

	// Execute until hook_interrupt sees EXEC_RETURN (0xAE00) and stops
	extern volatile bool g_exec68k_return_flag;
	g_exec68k_return_flag = false;

	uc_engine *uc = (uc_engine *)unicorn_get_uc(unicorn_cpu);
	bool returned = false;
	int max_iterations = 100000;
	int iterations = 0;

	while (!returned && iterations < max_iterations) {
		uint32_t pc = unicorn_get_pc(unicorn_cpu);
		uc_err err = uc_emu_start(uc, pc, 0xFFFFFFFF, 0, 0);

		if (err != UC_ERR_OK) {
			if (do_log) {
				fprintf(stderr, "[Execute68k] uc_emu_start returned %d at PC=0x%08X\n", err, pc);
			}
		}

		if (g_exec68k_return_flag) {
			returned = true;
			g_exec68k_return_flag = false;
		}

		iterations++;
	}

	if (!returned && do_log) {
		fprintf(stderr, "[ERROR] Execute68k did not return after %d iterations (addr=0x%08x)\n",
		        iterations, addr);
	}

	// Get registers back from Unicorn (results of the subroutine execution)
	for (int i = 0; i < 8; i++) {
		r->d[i] = unicorn_get_dreg(unicorn_cpu, i);
		r->a[i] = unicorn_get_areg(unicorn_cpu, i);
	}
	r->sr = unicorn_get_sr(unicorn_cpu);

	// RESTORE ALL CPU state
	for (int i = 0; i < 8; i++) {
		unicorn_set_dreg(unicorn_cpu, i, saved_dregs[i]);
		unicorn_set_areg(unicorn_cpu, i, saved_aregs[i]);
	}
	unicorn_set_sr(unicorn_cpu, saved_sr);
	unicorn_set_pc(unicorn_cpu, saved_pc);
}

// Memory access (Unicorn-specific: uses uc_mem_read/write, NOT host pointers)
// These are called from UAE's get_long/put_long functions via platform API
//
// IMPORTANT: During initialization (before unicorn_backend_init()), unicorn_cpu is NULL.
// In this phase, PatchROM() needs to patch ROM, so we fall back to DirectReadMacInt*()
// which patches ROMBaseHost directly. After unicorn_backend_init(), Unicorn copies the
// patched ROM, and all subsequent access uses uc_mem_read/write on Unicorn's internal memory.
static uint32_t unicorn_mem_read_long(uint32_t addr) {
	if (!unicorn_cpu) {
		// Before Unicorn initialization: read from host memory directly
		return DirectReadMacInt32(addr);
	}
	uint32_t value = 0;
	uc_err err = uc_mem_read((uc_engine*)unicorn_get_uc(unicorn_cpu), addr, &value, 4);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "unicorn_mem_read_long: failed to read from 0x%08X: %s\n",
		        addr, uc_strerror(err));
		return 0;
	}
	// Unicorn stores memory in big-endian (M68K native), convert to host byte order for processing
	#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return __builtin_bswap32(value);
	#else
	return value;
	#endif
}

static uint16_t unicorn_mem_read_word(uint32_t addr) {
	if (!unicorn_cpu) {
		return DirectReadMacInt16(addr);
	}
	uint16_t value = 0;
	uc_err err = uc_mem_read((uc_engine*)unicorn_get_uc(unicorn_cpu), addr, &value, 2);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "unicorn_mem_read_word: failed to read from 0x%08X: %s\n",
		        addr, uc_strerror(err));
		return 0;
	}
	#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return __builtin_bswap16(value);
	#else
	return value;
	#endif
}

static uint8_t unicorn_mem_read_byte(uint32_t addr) {
	if (!unicorn_cpu) {
		return DirectReadMacInt8(addr);
	}
	uint8_t value = 0;
	uc_err err = uc_mem_read((uc_engine*)unicorn_get_uc(unicorn_cpu), addr, &value, 1);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "unicorn_mem_read_byte: failed to read from 0x%08X: %s\n",
		        addr, uc_strerror(err));
		return 0;
	}
	return value;
}

static void unicorn_mem_write_long(uint32_t addr, uint32_t value) {
	// Debug: Track writes to 0xcfc (WLSC marker)
	if (addr == 0xcfc) {
		fprintf(stderr, "[WLSC] Writing 0x%08x to 0xcfc (PC=0x%08x)\n",
		        value, unicorn_cpu ? unicorn_get_pc(unicorn_cpu) : 0);
	}

	if (!unicorn_cpu) {
		// Before Unicorn initialization: write to host memory directly
		DirectWriteMacInt32(addr, value);
		return;
	}
	// Convert from host byte order to big-endian (M68K native) for Unicorn
	#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	value = __builtin_bswap32(value);
	#endif
	uc_err err = uc_mem_write((uc_engine*)unicorn_get_uc(unicorn_cpu), addr, &value, 4);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "unicorn_mem_write_long: failed to write to 0x%08X: %s\n",
		        addr, uc_strerror(err));
	}
}

static void unicorn_mem_write_word(uint32_t addr, uint16_t value) {
	if (!unicorn_cpu) {
		DirectWriteMacInt16(addr, value);
		return;
	}
	#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	value = __builtin_bswap16(value);
	#endif
	uc_err err = uc_mem_write((uc_engine*)unicorn_get_uc(unicorn_cpu), addr, &value, 2);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "unicorn_mem_write_word: failed to write to 0x%08X: %s\n",
		        addr, uc_strerror(err));
	}
}

static void unicorn_mem_write_byte(uint32_t addr, uint8_t value) {
	if (!unicorn_cpu) {
		DirectWriteMacInt8(addr, value);
		return;
	}
	uc_err err = uc_mem_write((uc_engine*)unicorn_get_uc(unicorn_cpu), addr, &value, 1);
	if (err != UC_ERR_OK) {
		fprintf(stderr, "unicorn_mem_write_byte: failed to write to 0x%08X: %s\n",
		        addr, uc_strerror(err));
	}
}

/**
 * Install Unicorn CPU backend into platform
 */
void cpu_unicorn_install(Platform *p) {
	p->cpu_name = "Unicorn Engine";

	// Configuration
	p->cpu_set_type = unicorn_backend_set_type;

	// Lifecycle
	p->cpu_init = unicorn_backend_init;
	p->cpu_reset = unicorn_backend_reset;
	p->cpu_destroy = unicorn_backend_destroy;

	// Execution
	p->cpu_execute_one = unicorn_backend_execute_one;
	// Enable fast path unless CPU_TRACE is set (tracing needs single-step)
	if (getenv("CPU_TRACE")) {
		p->cpu_execute_fast = NULL;  // Force slow path for accurate tracing
	} else {
		p->cpu_execute_fast = unicorn_backend_execute_fast;  // JIT fast path
	}

	// State query
	p->cpu_is_stopped = unicorn_backend_is_stopped;
	p->cpu_get_pc = unicorn_backend_get_pc;
	p->cpu_get_sr = unicorn_backend_get_sr;
	p->cpu_get_dreg = unicorn_backend_get_dreg;
	p->cpu_get_areg = unicorn_backend_get_areg;

	// State modification
	p->cpu_set_pc = unicorn_backend_set_pc;
	p->cpu_set_sr = unicorn_backend_set_sr;
	p->cpu_set_dreg = unicorn_backend_set_dreg;
	p->cpu_set_areg = unicorn_backend_set_areg;

	// Memory access
	p->cpu_mem_read = unicorn_backend_mem_read;
	p->cpu_mem_write = unicorn_backend_mem_write;

	// Interrupts
	p->cpu_trigger_interrupt = unicorn_backend_trigger_interrupt;

	// 68k Trap execution
	p->cpu_execute_68k_trap = unicorn_backend_execute_68k_trap;

	// 68k Subroutine execution (for timer callbacks, ADB handlers, etc.)
	p->cpu_execute_68k = unicorn_backend_execute_68k;

	// Memory system (Unicorn-specific: uses uc_mem_read/write to access Unicorn's internal memory)
	// IMPORTANT: Do NOT use DirectReadMacInt* functions - they read from RAMBaseHost/ROMBaseHost
	// which is UAE's memory space, not Unicorn's internal memory!
	p->mem_read_byte = unicorn_mem_read_byte;
	p->mem_read_word = unicorn_mem_read_word;
	p->mem_read_long = unicorn_mem_read_long;
	p->mem_write_byte = unicorn_mem_write_byte;
	p->mem_write_word = unicorn_mem_write_word;
	p->mem_write_long = unicorn_mem_write_long;

	// Address translation: Unicorn doesn't support direct host pointer access
	// Mac2HostAddr/Host2MacAddr are only valid for UAE's memory space
	// For Unicorn, these should NOT be used - all access must go through uc_mem_read/write
	p->mem_mac_to_host = NULL;  // Not supported for Unicorn
	p->mem_host_to_mac = NULL;  // Not supported for Unicorn
}
