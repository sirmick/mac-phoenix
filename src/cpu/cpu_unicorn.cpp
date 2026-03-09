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

// M68k register structure (shared C/C++ definition)
#include "m68k_registers.h"

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
static std::atomic<bool> unicorn_stop_requested(false);
// Dummy and high-memory regions now use uc_mmio_map (no host buffer needed)
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
		unicorn_defer_sr_update(unicorn_cpu, regs.sr);
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

		// Set new PC and stack pointer
		unicorn_set_pc(unicorn_cpu, regs.a[0]);
		unicorn_set_areg(unicorn_cpu, 7, regs.a[1]);

		// Return true to indicate PC was already set (don't advance it)
		return true;
	}

	// Return false to indicate PC was not advanced (caller will advance it)
	return false;
}

// Platform trap handler for Unicorn-only mode
// Handles A-line and F-line traps by simulating M68K exceptions
static bool unicorn_platform_trap_handler(int vector, uint16_t opcode, bool is_primary) {
	(void)is_primary; // Unicorn is always primary in standalone mode

	extern void unicorn_simulate_exception(UnicornCPU *cpu, int vector_nr, uint16_t opcode);
	unicorn_simulate_exception(unicorn_cpu, vector, opcode);

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
	(void)type; (void)size; (void)value; (void)user_data;

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
	(void)type; (void)size; (void)value; (void)user_data;

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
	(void)uc; (void)size; (void)user_data;

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
	}
	// Other hardware (SCC, SCSI, ASC, video, etc.)
	else {
		val = 0x00;
	}

	return (uint64_t)val;
}

// MMIO write callback - tracks CPU writes to hardware registers
// Uses uc_cb_mmio_write_t signature: void(uc_engine*, uint64_t offset, unsigned size, uint64_t value, void*)
static void mmio_write_cb(uc_engine *uc, uint64_t offset, unsigned size, uint64_t value, void *user_data) {
	(void)uc; (void)size; (void)value; (void)user_data;

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
	// Other hardware regions - writes silently accepted
	else {
		// No action needed for VIA2/SCC/SCSI/ASC/DAFB writes
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
	if (!unicorn_map_ram(unicorn_cpu, RAMBaseMac, RAMBaseHost, RAMSize)) {
		fprintf(stderr, "Failed to map RAM to Unicorn\n");
		unicorn_destroy(unicorn_cpu);
		unicorn_cpu = NULL;
		return false;
	}

	// Map ROM as writable (BasiliskII patches ROM during boot)
	if (!unicorn_map_rom_writable(unicorn_cpu, ROMBaseMac, ROMBaseHost, ROMSize)) {
		fprintf(stderr, "Failed to map ROM to Unicorn\n");
		unicorn_destroy(unicorn_cpu);
		unicorn_cpu = NULL;
		return false;
	}

	// Map ScratchMem region (64KB after ROM) - used by ROM patches for fake hardware bases
	// CRITICAL: This MUST be mapped separately from the dummy region below.
	// ROM patches redirect hardware register accesses to ScratchMem addresses.
	// Without this mapping, those accesses hit the dummy region (0xFF00FF00 pattern),
	// causing ROM code to read garbage values and corrupt data structures (e.g., WDCB).
	{
		uint32_t scratch_base = ROMBaseMac + ROMSize;  // 0x02100000
		uint32_t scratch_size = 0x10000;               // 64KB (SCRATCH_MEM_SIZE)
		uint8_t *scratch_host = ROMBaseHost + ROMSize;  // Contiguous after ROM in host memory
		memset(scratch_host, 0, scratch_size);  // Initialize to zeros (safe for hardware register reads)
		if (!unicorn_map_ram(unicorn_cpu, scratch_base, scratch_host, scratch_size)) {
			fprintf(stderr, "Failed to map ScratchMem to Unicorn\n");
			unicorn_destroy(unicorn_cpu);
			unicorn_cpu = NULL;
			return false;
		}
	}

	// Map frame buffer area (4MB after ScratchMem) — video drivers place framebuffer here
	// Memory layout: [RAM 32MB][ROM 1MB][ScratchMem 64KB][FrameBuffer 4MB][Dummy ...]
	{
		uint32_t fb_base = ROMBaseMac + ROMSize + 0x10000;  // 0x02110000
		uint32_t fb_size = 0x400000;  // 4MB (FRAMEBUFFER_AREA_SIZE)
		uint8_t *fb_host = ROMBaseHost + ROMSize + 0x10000;  // Contiguous after ScratchMem
		if (!unicorn_map_ram(unicorn_cpu, fb_base, fb_host, fb_size)) {
			fprintf(stderr, "Failed to map frame buffer to Unicorn\n");
			unicorn_destroy(unicorn_cpu);
			unicorn_cpu = NULL;
			return false;
		}
	}

	// Map dummy region after FrameBuffer using MMIO callbacks (not uc_mem_map_ptr!)
	// CRITICAL: Must use uc_mmio_map so reads ALWAYS return 0 and writes are silently dropped.
	// With uc_mem_map_ptr (UC_PROT_ALL), ROM init code writes ff00ff00 to these addresses,
	// and later reads get ff00ff00 back — cascading through the Memory Manager to corrupt
	// the WDCB (Working Directory Control Block) and stall the file system.
	// UAE's dummy_bank uses callbacks that always return 0, so we must match that behavior.
	uint32_t dummy_region_base = ROMBaseMac + ROMSize + 0x10000 + 0x400000;  // 0x02510000
	uint32_t dummy_region_size = 16 * 1024 * 1024 - 0x10000 - 0x400000;     // 16MB - 64KB - 4MB
	{
		uc_engine *dummy_uc = (uc_engine *)unicorn_get_uc(unicorn_cpu);
		uc_err dummy_err = uc_mmio_map(dummy_uc, dummy_region_base, dummy_region_size,
		                               dummy_bank_read, NULL,
		                               dummy_bank_write, NULL);
		if (dummy_err != UC_ERR_OK) {
			fprintf(stderr, "Failed to map dummy region as MMIO: %s\n", uc_strerror(dummy_err));
			unicorn_destroy(unicorn_cpu);
			unicorn_cpu = NULL;
			return false;
		}
	}
	// Map high memory region using MMIO callbacks (same reasoning as dummy region)
	uint32_t high_mem_base = 0xF0000000;
	uint32_t high_mem_size = 0x10000000;  // 256 MB
	{
		uc_engine *high_uc = (uc_engine *)unicorn_get_uc(unicorn_cpu);
		uc_err high_err = uc_mmio_map(high_uc, high_mem_base, high_mem_size,
		                              dummy_bank_read, NULL,
		                              dummy_bank_write, NULL);
		if (high_err != UC_ERR_OK) {
			fprintf(stderr, "Failed to map high memory region as MMIO: %s\n", uc_strerror(high_err));
			unicorn_destroy(unicorn_cpu);
			unicorn_cpu = NULL;
			return false;
		}
	}
	// Map hardware register region using uc_mmio_map for proper MMIO emulation
	// uc_mmio_map provides read/write callbacks that are always invoked by the JIT,
	// unlike UC_HOOK_MEM_READ which is bypassed for uc_mem_map_ptr regions.
	{
		uc_engine *mmio_uc = (uc_engine *)unicorn_get_uc(unicorn_cpu);
		uc_err mmio_err = uc_mmio_map(mmio_uc, MMIO_HW_BASE, MMIO_HW_SIZE,
		                              mmio_read_cb, NULL,
		                              mmio_write_cb, NULL);
		if (mmio_err != UC_ERR_OK) {
			fprintf(stderr, "[Unicorn] Failed to map MMIO region: %s\n", uc_strerror(mmio_err));
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
			fprintf(stderr, "[Unicorn] Failed to map NuBus gap 1: %s\n", uc_strerror(gap_err));
		}

		// Gap 2: After MMIO hardware to before high memory region
		// 0x50F40000 - 0xEFFFFFFF
		uint32_t gap2_base = MMIO_HW_BASE + MMIO_HW_SIZE;           // 0x50F40000
		uint32_t gap2_size = high_mem_base - gap2_base;              // 0x9F0C0000
		gap_err = uc_mmio_map(gap_uc, gap2_base, gap2_size,
		                      dummy_bank_read, NULL,
		                      dummy_bank_write, NULL);
		if (gap_err != UC_ERR_OK) {
			fprintf(stderr, "[Unicorn] Failed to map NuBus gap 2: %s\n", uc_strerror(gap_err));
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

	// Initialize CPU tracing from environment variable
	cpu_trace_init();

	// Register atexit handler to print block statistics on exit
	static bool atexit_registered = false;
	if (!atexit_registered) {
		atexit([]() {
			if (unicorn_cpu) {
				unicorn_print_perf_counters(unicorn_cpu);
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

// Execution
static int unicorn_backend_execute_one(void) {
	if (!unicorn_cpu) {
		return 3;  // CPU_EXEC_EXCEPTION
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
	 * exit cleanly when --timeout is set.
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
	return 0;  // CPU_EXEC_OK
}

static void unicorn_backend_execute_fast(void) {
	if (!unicorn_cpu) return;

	unicorn_stop_requested.store(false, std::memory_order_release);

	while (webserver::g_running.load(std::memory_order_acquire) &&
	       !unicorn_stop_requested.load(std::memory_order_acquire)) {
		int result = unicorn_execute_with_interrupts(unicorn_cpu, 100000000);
		if (result < 0) {
			uint32_t pc = unicorn_get_pc(unicorn_cpu);
			const char *err = unicorn_get_error(unicorn_cpu);

			if (!err || strcmp(err, "OK") == 0) {
				continue;  // Normal uc_emu_stop() — restart
			}

			fprintf(stderr, "[Unicorn] Fatal error at PC=0x%08X: %s\n", pc, err);
			break;
		}
	}
}

static void unicorn_backend_request_stop(void) {
	unicorn_stop_requested.store(true, std::memory_order_release);
	if (unicorn_cpu) {
		uc_engine *uc = (uc_engine *)unicorn_get_uc(unicorn_cpu);
		if (uc) uc_emu_stop(uc);
	}
}

// State Query
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

	// Write caller's register values to CPU for parameter passing.
	// TimerInterrupt sets A0=completion_addr, A1=TMTask_addr before calling.
	for (int i = 0; i < 8; i++) {
		unicorn_set_dreg(unicorn_cpu, i, r->d[i]);
	}
	for (int i = 0; i < 7; i++) {
		unicorn_set_areg(unicorn_cpu, i, r->a[i]);
	}
	// DON'T set A7 or SR from r - use saved state for stack pointer

	// Push M68K_EXEC_RETURN sentinel on stack, then push a fake return address
	// that points to the sentinel. When the subroutine does RTS, it pops
	// the return address and jumps to the sentinel opcode.
	uint32_t sp = saved_aregs[7];

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
			// Errors during Execute68k are often transient (uc_emu_stop)
		}

		if (g_exec68k_return_flag) {
			returned = true;
			g_exec68k_return_flag = false;
		}

		iterations++;
	}

	if (!returned) {
		fprintf(stderr, "[Unicorn] Execute68k did not return after %d iterations (addr=0x%08x)\n",
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

// Flush Unicorn's JIT translation block cache
// Called when code is patched at runtime (system patches, CheckLoad, etc.)
// Without this, Unicorn continues executing stale TBs after code is modified.
static void unicorn_backend_flush_code_cache(void) {
	if (!unicorn_cpu) return;
	uc_engine *uc = (uc_engine *)unicorn_get_uc(unicorn_cpu);
	uc_ctl_flush_tb(uc);
}

/**
 * Install Unicorn CPU backend into platform
 */
void cpu_unicorn_install(Platform *p) {
	p->cpu_name = "Unicorn Engine";
	p->use_aline_emulops = true;

	// Configuration
	p->cpu_set_type = unicorn_backend_set_type;

	// Lifecycle
	p->cpu_init = unicorn_backend_init;
	p->cpu_reset = unicorn_backend_reset;

	// Execution
	p->cpu_execute_one = unicorn_backend_execute_one;
	// Enable fast path unless CPU_TRACE is set (tracing needs single-step)
	if (getenv("CPU_TRACE")) {
		p->cpu_execute_fast = NULL;  // Force slow path for accurate tracing
	} else {
		p->cpu_execute_fast = unicorn_backend_execute_fast;  // JIT fast path
	}

	// Stop
	p->cpu_request_stop = unicorn_backend_request_stop;

	// State query
	p->cpu_get_pc = unicorn_backend_get_pc;
	p->cpu_get_sr = unicorn_backend_get_sr;
	p->cpu_get_dreg = unicorn_backend_get_dreg;
	p->cpu_get_areg = unicorn_backend_get_areg;

	// Interrupts
	p->cpu_trigger_interrupt = unicorn_backend_trigger_interrupt;

	// 68k Trap execution
	p->cpu_execute_68k_trap = unicorn_backend_execute_68k_trap;

	// 68k Subroutine execution (for timer callbacks, ADB handlers, etc.)
	p->cpu_execute_68k = unicorn_backend_execute_68k;

	// Code cache flush (invalidate JIT TBs when code is patched at runtime)
	p->flush_code_cache = unicorn_backend_flush_code_cache;

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
