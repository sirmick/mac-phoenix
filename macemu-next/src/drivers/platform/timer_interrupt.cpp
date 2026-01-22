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

	printf("Timer: Initialized 60.15 Hz timer (clock_gettime polling)\n");
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

	// Debug: Check what HasMacStarted returns
	static bool debug_logged = false;
	bool mac_started = HasMacStarted();
	if (!debug_logged && interrupt_count < 5) {
		uint32_t cfc_value = ReadMacInt32(0xcfc);
		fprintf(stderr, "[DEBUG Timer] tick=%llu ROM=0x%04x mac_started=%d cfc=0x%08x (expected 0x574C5343='WLSC')\n",
		        (unsigned long long)interrupt_count, ROMVersion, mac_started, cfc_value);
		if (interrupt_count >= 4) debug_logged = true;
	}

	// EXPERIMENTAL: For Unicorn, always deliver interrupts during boot
	// to work around the WLSC chicken-and-egg problem
	extern Platform g_platform;
	bool is_unicorn = g_platform.cpu_name && strstr(g_platform.cpu_name, "Unicorn") != NULL;

	if (ROMVersion == ROM_VERSION_CLASSIC || mac_started ||
	    (is_unicorn && interrupt_count < 300)) {  // First 5 seconds for Unicorn
		if (g_platform.cpu_trigger_interrupt) {
			int level = intlev();
			if (level > 0) {
				g_platform.cpu_trigger_interrupt(level);
			}
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

	// Timer fired! Update last fire time
	last_timer_ns = now_ns;

	// Process one tick
	one_tick();
	interrupt_count++;

	// Debug logging for first few timer firings
	static int fire_count = 0;
	if (++fire_count <= 10) {
		fprintf(stderr, "[poll_timer_interrupt] Timer fired #%d, interrupt_count=%llu, elapsed_ns=%llu\n",
		        fire_count, (unsigned long long)interrupt_count, (unsigned long long)elapsed);
	}

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
