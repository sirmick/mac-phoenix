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

// Shared input polling for fork mode (defined in cpu_process.cpp)
extern "C" void ADBPollSharedInput(void);

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

	// Poll shared input queue (fork mode: parent writes, child reads at 60Hz)
	ADBPollSharedInput();

	// Trigger video refresh (60Hz frame capture)
	// This must happen BEFORE triggering CPU interrupts to ensure smooth video
	extern Platform g_platform;
	if (g_platform.video_refresh) {
		g_platform.video_refresh();
	}

	// Set 60Hz interrupt flag
	SetInterruptFlag(INTFLAG_60HZ);

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
