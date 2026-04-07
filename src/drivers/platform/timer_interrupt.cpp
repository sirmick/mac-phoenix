/*
 *  timer_interrupt.cpp - 60Hz tick thread
 *
 *  Dedicated thread fires one_tick() at 60.15 Hz, matching legacy BasiliskII's
 *  tick_func() pthread and the PPC tick_thread_func() in cpu_ppc_kpx.cpp.
 *
 *  Video refresh runs from this thread (not the CPU thread), so the
 *  framebuffer memcpy doesn't stall CPU execution.
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
#include <thread>
#include <atomic>

// IPC SHM export (cursor + app name + mac state) for subprocess mode
extern "C" void boot_progress_export_cursor_to_ipc(void);
extern "C" void boot_progress_export_app_to_ipc(void);
extern "C" void boot_progress_export_mac_state(void);

// Input polling stub — no-op in subprocess mode (input arrives via control socket).
extern "C" __attribute__((weak)) void ADBPollSharedInput(void) {}

// Forward declaration (avoid including timer.h due to C linkage conflicts)
extern "C" uint32 TimerDateTime(void);

// Tick thread state
static std::atomic<bool> tick_thread_running{false};
static std::thread tick_thread;
static uint64_t interrupt_count = 0;

// Get current time in microseconds (matches PPC GetTicks_usec pattern)
static uint64_t get_ticks_usec() {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000ULL + now.tv_nsec / 1000;
}

/*
 *  one_tick() - Called every 16.625ms (60.15 Hz) from tick thread
 */
static void one_tick(void)
{
	static uint64_t tick_counter = 0;

	// Every 60 ticks = ~1 second
	if (++tick_counter >= 60) {
		tick_counter = 0;
		// Pseudo Mac 1Hz interrupt, update local time
		WriteMacInt32(0x20c, TimerDateTime());
		SetInterruptFlag(INTFLAG_1HZ);
	}

	// Poll shared input queue (no-op in subprocess mode)
	ADBPollSharedInput();

	// Trigger video refresh (60Hz frame capture)
	// Runs on tick thread, not CPU thread — doesn't stall emulation
	extern Platform g_platform;
	if (g_platform.video_refresh) {
		g_platform.video_refresh();
	}

	// Export cursor, app name, and Mac state to IPC SHM (for parent's web API)
	boot_progress_export_cursor_to_ipc();
	boot_progress_export_app_to_ipc();
	boot_progress_export_mac_state();

	// Set 60Hz interrupt flag
	SetInterruptFlag(INTFLAG_60HZ);

	// Trigger CPU-level interrupt
	if (g_platform.cpu_trigger_interrupt) {
		int level = intlev();
		if (level > 0) {
			g_platform.cpu_trigger_interrupt(level);
		}
	}

	interrupt_count++;
}

/*
 *  Tick thread — matches PPC tick_thread_func() and legacy BasiliskII tick_func()
 *
 *  Runs independently from CPU thread at 60.15 Hz using nanosleep.
 */
static void tick_thread_func()
{
	uint64_t next = get_ticks_usec();
	extern bool tick_inhibit;

	while (tick_thread_running.load(std::memory_order_relaxed)) {
		int period = 16625;  // 60.15 Hz in microseconds
		next += period;
		int64_t delay = next - get_ticks_usec();
		if (delay > 0) {
			struct timespec ts = {0, (long)(delay * 1000)};
			nanosleep(&ts, nullptr);
		} else if (delay < -period) {
			next = get_ticks_usec();  // Resynchronize if too late
		}
		if (tick_inhibit) continue;

		one_tick();
	}
}

extern "C" {

/*
 *  Start tick thread
 */
void setup_timer_interrupt(void)
{
	if (tick_thread_running.load()) return;

	interrupt_count = 0;
	tick_thread_running.store(true, std::memory_order_release);
	tick_thread = std::thread(tick_thread_func);

	fprintf(stderr, "Timer: Started 60.15 Hz tick thread\n");
}

/*
 *  Poll timer — no-op, kept for backward compatibility.
 *  The tick thread handles everything now.
 */
uint64_t poll_timer_interrupt(void)
{
	return 0;
}

/*
 *  Stop tick thread
 */
void stop_timer_interrupt(void)
{
	if (!tick_thread_running.load()) return;

	tick_thread_running.store(false, std::memory_order_release);
	if (tick_thread.joinable()) {
		tick_thread.join();
	}

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
