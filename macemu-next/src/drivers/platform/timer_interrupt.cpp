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
#include <string.h>        // For strstr()
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

	// Debug: Monitor low memory global at 0x0b78 (boot stall diagnostic)
	static uint32 prev_0b78 = 0xDEADBEEF;
	uint32 val_0b78 = ReadMacInt32(0x0b78);
	extern Platform g_platform;
	/* Watch 0x01FFF30C (resource chain sentinel) and resource globals */
	uint32 val_1fff30c = ReadMacInt32(0x01FFF30C);
	uint32 topmap = ReadMacInt32(0x0A50);
	uint32 sysmap = ReadMacInt32(0x0A54);
	uint32 syszone = ReadMacInt32(0x02A6);
	/* Get current PC via platform API if available */
	uint32 cur_pc = 0;
	if (g_platform.cpu_get_pc) cur_pc = g_platform.cpu_get_pc();
	fprintf(stderr, "[TIMER 1Hz] sec=%llu $0b78=0x%08x PC=0x%08x [1FFF30C]=0x%08x TopMap=0x%08x SysMap=0x%08x SysZone=0x%08x backend=%s\n",
			(unsigned long long)interrupt_count / 60,
			val_0b78, cur_pc, val_1fff30c, topmap, sysmap, syszone,
			g_platform.cpu_name ? g_platform.cpu_name : "?");
	if (val_0b78 != prev_0b78) {
		fprintf(stderr, "[TIMER 1Hz] *** $0b78 CHANGED: 0x%08x -> 0x%08x ***\n",
				prev_0b78, val_0b78);
		prev_0b78 = val_0b78;
		// Dump OS trap table entries that point to RAM (not ROM)
		fprintf(stderr, "[TRAP-TABLE] OS trap table (RAM handlers only):\n");
		for (int i = 0; i < 256; i++) {
			uint32 handler = ReadMacInt32(0x0400 + i * 4);
			if (handler > 0 && handler < 0x02000000) {
				fprintf(stderr, "[TRAP-TABLE] OS trap A0%02x → 0x%08x\n", i, handler);
			}
		}
		// Also dump Toolbox trap table base and first few RAM entries
		uint32 toolbox_base = ReadMacInt32(0x0E7C);
		fprintf(stderr, "[TRAP-TABLE] Toolbox table base: 0x%08x\n", toolbox_base);
		if (toolbox_base > 0 && toolbox_base < 0x01800000) {
			for (int i = 0; i < 1024; i++) {
				uint32 handler = ReadMacInt32(toolbox_base + i * 4);
				if (handler >= 0x0001cb00 && handler <= 0x0001cd00) {
					fprintf(stderr, "[TRAP-TABLE] Toolbox trap A8%03x → 0x%08x\n", i, handler);
				}
			}
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
