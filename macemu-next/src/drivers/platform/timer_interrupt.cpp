/*
 *  timer_interrupt.cpp - 60Hz timer via clock_gettime() polling
 *
 *  Based on BasiliskII's tick_func() (main_unix.cpp:1492-1515)
 *  Uses simple clock_gettime() polling instead of signals or timerfd.
 *
 *  Called from CPU backend execution loops (UAE and Unicorn).
 */

#include "sysdeps.h"
#include "main.h"
#include "platform.h"
#include "timer_interrupt.h"
#include "rom_patches.h"   // For ROMVersion, ROM_VERSION_CLASSIC
#include "uae_wrapper.h"   // For intlev()
#include "macos_util.h"    // For HasMacStarted()

#include <time.h>
#include <stdio.h>

// Forward declaration (avoid including timer.h due to C linkage conflicts)
extern "C" uint32 TimerDateTime(void);

// Timer state
static uint64_t last_timer_ns = 0;
static uint64_t interrupt_count = 0;
static uint64_t tick_counter = 0;
static bool timer_initialized = false;

extern "C" {

/*
 *  Initialize timer system
 *
 *  Uses clock_gettime() polling for reliable 60.15 Hz timing.
 *  This is the master heartbeat for the emulator.
 */
void setup_timer_interrupt(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	last_timer_ns = now.tv_sec * 1000000000ULL + now.tv_nsec;
	interrupt_count = 0;
	tick_counter = 0;
	timer_initialized = true;

	fprintf(stderr, "Timer: Initialized 60.15 Hz timer (clock_gettime polling)\n");
}

/*
 *  one_second() - Called every 60 ticks (~1 second)
 *
 *  Matches BasiliskII main_unix.cpp:1450-1465
 */
static void one_second(void)
{
	// Pseudo Mac 1Hz interrupt, update local time
	WriteMacInt32(0x20c, TimerDateTime());

	SetInterruptFlag(INTFLAG_1HZ);
	// Note: TriggerInterrupt() will be called from poll_timer_interrupt()

	// Minimal 1Hz heartbeat with stall diagnostics
	extern Platform g_platform;
	uint32 val_0b78 = ReadMacInt32(0x0b78);
	uint32 cur_pc = 0;
	if (g_platform.cpu_get_pc) cur_pc = g_platform.cpu_get_pc();
	fprintf(stderr, "[TIMER 1Hz] sec=%llu $0b78=0x%08x PC=0x%08x backend=%s\n",
			(unsigned long long)interrupt_count / 60,
			val_0b78, cur_pc,
			g_platform.cpu_name ? g_platform.cpu_name : "?");

	// One-time dump at sec=5: show instructions near stall PC and key low-memory globals
	static bool dumped = false;
	if (!dumped && interrupt_count / 60 >= 5) {
		dumped = true;
		fprintf(stderr, "[STALL DIAG] PC=0x%08x, dumping 16 words:\n  ", cur_pc);
		for (int i = -4; i < 12; i++) {
			uint16 w = ReadMacInt16(cur_pc + i * 2);
			fprintf(stderr, "%s%04x ", (i == 0) ? ">>>" : "", w);
		}
		fprintf(stderr, "\n");
		// Key low-memory globals
		fprintf(stderr, "[STALL DIAG] MemTop=$0108=%08x BufPtr=$010C=%08x\n",
				ReadMacInt32(0x0108), ReadMacInt32(0x010C));
		fprintf(stderr, "[STALL DIAG] ScrnBase=$0824=%08x MainDev=$0DD0=%08x\n",
				ReadMacInt32(0x0824), ReadMacInt32(0x0DD0));
		fprintf(stderr, "[STALL DIAG] BootGlobs=$0DDC=%08x ROMBase=$02AE=%08x\n",
				ReadMacInt32(0x0DDC), ReadMacInt32(0x02AE));
		// Check HasMacStarted (WLSC at $0CFC) and ioResult at polling address
		fprintf(stderr, "[STALL DIAG] WLSC=$0CFC=%08x HasMacStarted=%d\n",
				ReadMacInt32(0x0CFC), ReadMacInt32(0x0CFC) == 0x574C5343);
		// Dump pending I/O parameter block (FSQ tail)
		uint32 fsq_tail = ReadMacInt32(0x0366);
		if (fsq_tail != 0) {
			fprintf(stderr, "[STALL DIAG] FSQ tail PB @%08x:\n", fsq_tail);
			fprintf(stderr, "  qLink=%08x qType=%04x ioTrap=%04x ioCmdAddr=%08x\n",
					ReadMacInt32(fsq_tail), ReadMacInt16(fsq_tail+4),
					ReadMacInt16(fsq_tail+6), ReadMacInt32(fsq_tail+8));
			fprintf(stderr, "  ioCompletion=%08x ioResult=%04x ioNamePtr=%08x\n",
					ReadMacInt32(fsq_tail+0x0c), ReadMacInt16(fsq_tail+0x10),
					ReadMacInt32(fsq_tail+0x12));
			fprintf(stderr, "  ioVRefNum=%04x ioRefNum=%04x\n",
					ReadMacInt16(fsq_tail+0x16), ReadMacInt16(fsq_tail+0x18));
			// First 64 bytes hex dump
			fprintf(stderr, "  hex: ");
			for (int i = 0; i < 32; i++) {
				fprintf(stderr, "%02x", ReadMacInt8(fsq_tail + i));
				if (i % 4 == 3) fprintf(stderr, " ");
			}
			fprintf(stderr, "\n");
		}
		fprintf(stderr, "[STALL DIAG] Ticks=$016a=%08x\n", ReadMacInt32(0x016a));
		fprintf(stderr, "[STALL DIAG] DrvQHdr: flags=%04x head=%08x tail=%08x\n",
				ReadMacInt16(0x0308), ReadMacInt32(0x030A), ReadMacInt32(0x030E));
		// FSQHdr at 0x0360: flags(2) + head(4) + tail(4)
		fprintf(stderr, "[STALL DIAG] FSQHdr: flags=%04x head=%08x tail=%08x\n",
				ReadMacInt16(0x0360), ReadMacInt32(0x0362), ReadMacInt32(0x0366));
		// VCBQHdr at 0x0356: flags(2) + head(4) + tail(4)
		fprintf(stderr, "[STALL DIAG] VCBQHdr: flags=%04x head=%08x tail=%08x\n",
				ReadMacInt16(0x0356), ReadMacInt32(0x0358), ReadMacInt32(0x035c));
		// Dump CPU registers to understand what resource is being searched
		if (g_platform.cpu_get_dreg && g_platform.cpu_get_areg) {
			fprintf(stderr, "[STALL DIAG] D0=%08x D1=%08x D2=%08x D3=%08x\n",
					g_platform.cpu_get_dreg(0), g_platform.cpu_get_dreg(1),
					g_platform.cpu_get_dreg(2), g_platform.cpu_get_dreg(3));
			fprintf(stderr, "[STALL DIAG] D4=%08x D5=%08x D6=%08x D7=%08x\n",
					g_platform.cpu_get_dreg(4), g_platform.cpu_get_dreg(5),
					g_platform.cpu_get_dreg(6), g_platform.cpu_get_dreg(7));
			fprintf(stderr, "[STALL DIAG] A0=%08x A1=%08x A2=%08x A3=%08x\n",
					g_platform.cpu_get_areg(0), g_platform.cpu_get_areg(1),
					g_platform.cpu_get_areg(2), g_platform.cpu_get_areg(3));
			fprintf(stderr, "[STALL DIAG] A4=%08x A5=%08x A6=%08x A7=%08x\n",
					g_platform.cpu_get_areg(4), g_platform.cpu_get_areg(5),
					g_platform.cpu_get_areg(6), g_platform.cpu_get_areg(7));
		}
		// Dump memory at A4 (current linked list node) and follow chain
		if (g_platform.cpu_get_areg) {
			uint32 a4 = g_platform.cpu_get_areg(4);
			fprintf(stderr, "[STALL DIAG] Memory chain from A4=0x%08x:\n", a4);
			uint32 node = a4;
			for (int step = 0; step < 8 && node != 0; step++) {
				fprintf(stderr, "  [%d] @%08x: link=%08x +$15=%02x +$1c=%04x +$28=%04x\n",
						step, node,
						ReadMacInt32(node),        // linked list pointer
						ReadMacInt8(node + 0x15),  // compared with D6
						ReadMacInt16(node + 0x1c), // compared with A2
						ReadMacInt16(node + 0x28));// compared with D3
				node = ReadMacInt32(node);  // follow chain
			}
		}
		// Check drive status record if head is non-zero
		// DrvQHdr head points to dsQLink (offset +6 into DrvSts)
		// So DrvSts base = head - 6
		uint32 qElem = ReadMacInt32(0x030A);
		while (qElem != 0) {
			uint32 drvBase = qElem - 6;  // dsQLink is at offset 6
			fprintf(stderr, "[STALL DIAG] Drive @%08x: dsDiskInPlace=%02x dsInstalled=%02x "
					"dsWriteProt=%02x qType=%04x dQDrive=%04x dQRefNum=%04x dQFSID=%04x\n",
					drvBase,
					ReadMacInt8(drvBase + 3),   // dsDiskInPlace
					ReadMacInt8(drvBase + 4),   // dsInstalled
					ReadMacInt8(drvBase + 2),   // dsWriteProt
					ReadMacInt16(drvBase + 10), // dsQType
					ReadMacInt16(drvBase + 12), // dsQDrive (drive number)
					ReadMacInt16(drvBase + 14), // dsQRefNum
					ReadMacInt16(drvBase + 16));// dsQFSID
			qElem = ReadMacInt32(qElem);  // Follow qLink to next
		}
	}
}

/*
 *  one_tick() - Called every 16.625ms (60.15 Hz)
 *
 *  Matches BasiliskII main_unix.cpp:1467-1490
 */
static void one_tick(void)
{
	// Every 60 ticks = ~1 second
	if (++tick_counter >= 60) {
		tick_counter = 0;
		one_second();
	}

	// Trigger video refresh (60Hz frame capture)
	// This must happen BEFORE triggering CPU interrupts to ensure smooth video
	extern Platform g_platform;
	if (g_platform.video_refresh) {
		g_platform.video_refresh();
	}

	// Set 60Hz interrupt flag
	SetInterruptFlag(INTFLAG_60HZ);

	// Trigger CPU-level interrupt via platform API
	// IMPORTANT: Only trigger interrupts after Mac has booted!
	// This matches BasiliskII's check (main_unix.cpp:1486)
	//
	// During early boot, Mac ROM initializes interrupt vectors and other critical
	// state. Taking interrupts too early can cause crashes or divergence between
	// different CPU backends. BasiliskII checks HasMacStarted() before triggering
	// 60Hz interrupts.
	//
	// The ROM_VERSION_CLASSIC check allows Classic Mac ROMs (Plus, SE) to bypass
	// this guard as they have different boot sequences.

	// Trigger CPU-level interrupt
	// IMPORTANT: Must deliver interrupts BEFORE Mac starts to allow boot to progress
	// The ROM needs timer interrupts to initialize and set up WLSC signature
	// Once WLSC is set, HasMacStarted() returns true
	if (g_platform.cpu_trigger_interrupt) {
		int level = intlev();
		if (level > 0) {
			g_platform.cpu_trigger_interrupt(level);
		}
	}
}

/*
 *  Poll timer - call from CPU execution loop
 *
 *  Returns number of timer expirations (usually 0 or 1)
 */
uint64_t poll_timer_interrupt(void)
{
	// Suppress timer interrupts during CPU tracing for deterministic execution
	static bool tracing_mode = (getenv("CPU_TRACE") != NULL);
	if (tracing_mode) {
		return 0;  // No interrupts during tracing
	}

	if (!timer_initialized) {
		return 0;
	}

	// Match BasiliskII: suppress ticks during RESET initialization
	// tick_inhibit is set true by RESET handler, cleared by PATCH_BOOT_GLOBS
	extern bool tick_inhibit;
	if (tick_inhibit) {
		return 0;
	}

	// Check current time
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	uint64_t now_ns = now.tv_sec * 1000000000ULL + now.tv_nsec;

	// Check if 16.625ms have passed (60.15 Hz)
	uint64_t elapsed = now_ns - last_timer_ns;
	if (elapsed < 16625000ULL) {
		return 0;  // Not time yet
	}

	// Timer fired! Advance by one tick interval (NOT reset to now).
	// This ensures missed ticks are caught up on subsequent polls.
	// If we're severely behind, cap to prevent a burst of 100s of ticks.
	last_timer_ns += 16625000ULL;

	// If we've fallen more than 10 ticks behind, snap to now
	// to prevent a burst of catch-up ticks that flood the system
	if (now_ns - last_timer_ns > 10 * 16625000ULL) {
		last_timer_ns = now_ns;
	}

	// Process one tick
	one_tick();
	interrupt_count++;

	return 1;  // One expiration
}

/*
 *  Stop timer
 */
void stop_timer_interrupt(void)
{
	if (!timer_initialized) {
		return;
	}

	timer_initialized = false;
	printf("Timer: Stopped after %llu interrupts (%llu seconds)\n",
	       (unsigned long long)interrupt_count,
	       (unsigned long long)interrupt_count / 60);
}

/*
 *  Get statistics
 */
uint64_t get_timer_interrupt_count(void)
{
	return interrupt_count;
}

}  // extern "C"
