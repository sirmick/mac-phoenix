/*
 *  main.cpp - macemu-next main entry point
 *
 *  Mac emulator with integrated WebRTC streaming (in-process architecture).
 *  Launches 4 threads: CPU/Main, Video Encoder, Audio Encoder, WebRTC/HTTP
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "newcpu.h"
#include "readcpu.h"
#include "memory.h"
#include "main.h"
#include "prefs.h"
#include "video.h"
#include "xpram.h"
#include "timer.h"
#include "sony.h"
#include "disk.h"
#include "cdrom.h"
#include "scsi.h"
#include "serial.h"
#include "ether.h"
#include "clip.h"
#include "adb.h"
#include "audio.h"
#include "rom_patches.h"
#include "user_strings.h"
#include "platform.h"
#include "extfs.h"
#include "drivers/platform/timer_interrupt.h"
#include "core/emulator_init.h"  // For deferred initialization
#include "crash_handler_init.h"  // Crash handler with stack traces

// WebRTC streaming
#include "config/config_manager.h"
#include "config/emulator_config.h"  // Unified configuration
#include "core/cpu_context.h"  // Phase 2: Self-contained CPU context
#include "drivers/video/video_webrtc.h"
#include "drivers/audio/audio_webrtc.h"
#include "webserver/webserver_main.h"
#include "webserver/api_handlers.h"
#include "webrtc/webrtc_server.h"
#include "drivers/video/encoders/codec.h"

// WebRTC globals
namespace webrtc {
	std::atomic<bool> g_running(true);
	std::atomic<bool> g_request_keyframe(false);
	config::MacemuConfig* g_config = nullptr;  // Global config for driver initialization
	WebRTCServer* g_server = nullptr;  // Global WebRTC server for encoder threads to send frames
}

// Video encoder globals
namespace video {
	std::atomic<bool> g_running(true);
	std::atomic<bool> g_request_keyframe(false);
}

// Audio encoder globals
namespace audio {
	std::atomic<bool> g_running(true);
}

// WebServer globals
namespace webserver {
	std::atomic<bool> g_running(true);
}

// WebRTC signaling server globals
namespace webrtc_signaling {
	std::atomic<bool> g_running(true);
}

// Global CPU context (Phase 2: Replaces global memory/CPU state)
static CPUContext g_cpu_ctx;

// CPU emulation state (kept for compatibility with WebUI thread)
namespace cpu_state {
	std::atomic<bool> g_running(false);  // CPU starts stopped, user must click "Start"
	std::mutex g_mutex;
	std::condition_variable g_cv;  // Notifies CPU thread when state changes
}

// Debug flags
bool g_debug_png = false;
bool g_debug_mode_switch = false;

#define DEBUG 1
#include "debug.h"

// Global variables (defined in basilisk_glue.cpp)
extern uint8 *RAMBaseHost;
extern uint8 *ROMBaseHost;
extern uint32 RAMSize;
extern uint32 ROMSize;

#if DIRECT_ADDRESSING
extern uintptr MEMBaseDiff;
extern uint32 RAMBaseMac;
extern uint32 ROMBaseMac;
#endif

// CPU and FPU type
extern int CPUType;
bool CPUIs68060;
extern int FPUType;
bool TwentyFourBitAddressing;

// Error handling
void ErrorAlert(const char *text)
{
	fprintf(stderr, "ERROR: %s\n", text);
}

void WarningAlert(const char *text)
{
	fprintf(stderr, "WARNING: %s\n", text);
}

// Quit emulator
void QuitEmulator(void)
{
	printf("QuitEmulator() called\n");
	ExitAll();
	exit(1);
}

// Interrupt control (minimal stubs - not in platform yet)
void DisableInterrupt(void)
{
}

void EnableInterrupt(void)
{
}

int main(int argc, char **argv)
{
	printf("=== macemu-next ===\n\n");

	// Install crash handlers (SIGSEGV, SIGBUS, etc.) with stack traces
	install_crash_handlers();

	// Initialize platform with null drivers
	platform_init();

	// Initialize random number generator
	srand(time(NULL));

	// ========================================
	// Load Unified Configuration
	// ========================================
	// This replaces the old prefs system with a clean, single source of truth.
	// Priority: CLI args > JSON config > Defaults
	const char* home = getenv("HOME");
	std::string default_config_path = std::string(home) + "/.config/macemu-next/config.json";

	config::EmulatorConfig emu_config = config::load_emulator_config(
		default_config_path.c_str(), argc, argv);

	// Print configuration for debugging
	config::print_config(emu_config);

	// Set RAM size from config (replaces hardcoded 32MB)
	RAMSize = emu_config.ram_mb * 1024 * 1024;

	// Store old JSON config for legacy code (will be removed later)
	static config::MacemuConfig webrtc_config;
	webrtc_config = config::load_config(default_config_path);
	webrtc::g_config = &webrtc_config;

	// Initialize prefs system with values from config (for legacy code)
	int dummy_argc = 0;
	char** dummy_argv = nullptr;
	PrefsInit(NULL, dummy_argc, dummy_argv);  // Don't process CLI args (already done)
	PrefsAddInt32("ramsize", RAMSize);
	PrefsAddInt32("cpu", emu_config.cpu_type_int());
	PrefsAddBool("fpu", emu_config.fpu);

	// Install platform drivers (video/audio)
	if (emu_config.enable_webrtc) {
		// WebRTC mode: Install WebRTC drivers (will launch encoder threads)
		printf("Installing WebRTC video/audio drivers...\n");
		g_platform.video_init = [](bool classic) -> bool {
			return video_webrtc_init(classic, webrtc::g_config);
		};
		g_platform.video_exit = video_webrtc_exit;
		g_platform.video_refresh = video_webrtc_refresh;
		g_platform.audio_init = audio_webrtc_init;
		g_platform.audio_exit = audio_webrtc_exit;
	} else {
		// Headless mode: Keep null drivers (no threads)
		printf("Using null video/audio drivers (headless mode)...\n");
		// null drivers already installed by platform_init()
	}

	// ========================================
	// Initialize CPU Context (Phase 2)
	// ========================================
	// This replaces 230 lines of manual initialization with a single function call!

	// Check if we have a ROM to load
	const char *rom_path = emu_config.rom_path.empty() ? nullptr : emu_config.rom_path.c_str();

	if (rom_path) {
		// CLI mode or WebUI with ROM: Initialize immediately
		printf("\n=== Initializing CPU Context ===\n");

		// Copy null drivers from g_platform into CPUContext's platform
		Platform* platform = g_cpu_ctx.get_platform();
		*platform = g_platform;

		// Install CPU backend into CPUContext's platform
		switch (emu_config.cpu_backend) {
			case config::CPUBackend::Unicorn:
				cpu_unicorn_install(platform);
				break;
			case config::CPUBackend::DualCPU:
				cpu_dualcpu_install(platform);
				break;
			case config::CPUBackend::UAE:
			default:
				cpu_uae_install(platform);
				break;
		}

		// IMPORTANT: Copy platform to global BEFORE init_m68k()
		// because ROM patching and CPU init trigger EmulOps that use g_platform
		g_platform = *platform;

		// Initialize M68K - does everything: allocate memory, load ROM,
		// check ROM, init subsystems, patch ROM, init CPU, set up timer
		if (!g_cpu_ctx.init_m68k(emu_config)) {
			fprintf(stderr, "Failed to initialize M68K CPU context\n");
			return 1;
		}

		// Mark emulator as initialized (for deferred init check)
		g_emulator_initialized = true;

		// Optional auto-exit timer (set EMULATOR_TIMEOUT=2 for 2 seconds)
		const char *timeout_env = getenv("EMULATOR_TIMEOUT");
		if (timeout_env) {
			int timeout_sec = atoi(timeout_env);
			if (timeout_sec > 0) {
				printf("Auto-exit timer set: %d seconds\n", timeout_sec);
				std::thread timeout_thread([timeout_sec]() {
					std::this_thread::sleep_for(std::chrono::seconds(timeout_sec));
					fprintf(stderr, "\n[Timeout: %d seconds elapsed, exiting]\n", timeout_sec);
					exit(0);
				});
				timeout_thread.detach();
			}
		}
	} else {
		// WebUI mode without ROM: Skip init, will be done via API
		printf("\nNo ROM file specified. Starting in webserver mode.\n");
		printf("You can configure the ROM path in the web UI.\n");
	}

	// ============================================================
	// HTTP Server (WebRTC Mode Only)
	// ============================================================
	if (emu_config.enable_webrtc) {
		// Set up API context for HTTP handlers
		CodecType server_codec = CodecType::PNG;  // Default
		http::APIContext api_context;
		api_context.debug_connection = emu_config.debug_connection;
		api_context.debug_mode_switch = emu_config.debug_mode_switch;
		api_context.debug_perf = emu_config.debug_perf;
		api_context.prefs_path = default_config_path;
		api_context.roms_path = webrtc_config.web.storage_dir + "/roms";
		api_context.images_path = webrtc_config.web.storage_dir + "/images";
		api_context.server_codec = &server_codec;
		api_context.cpu_running = &cpu_state::g_running;  // CPU state control
		api_context.cpu_mutex = &cpu_state::g_mutex;
		api_context.cpu_cv = &cpu_state::g_cv;
		printf("Storage paths:\n");
		printf("  ROMs:   %s\n", api_context.roms_path.c_str());
		printf("  Images: %s\n", api_context.images_path.c_str());
		api_context.notify_codec_change_fn = [](CodecType codec) {
			fprintf(stderr, "[API] Codec changed to: %d\n", static_cast<int>(codec));
			// TODO: Notify video encoder to change codec
		};

		// Initialize WebRTC signaling server
		printf("\n=== WebRTC Mode ===\n");
		printf("Launching HTTP server on port %d...\n", webrtc_config.web.http_port);

		webrtc::WebRTCServer webrtc_server;
		if (!webrtc_server.init(8090)) {  // WebSocket signaling on port 8090
			fprintf(stderr, "Failed to start WebRTC signaling server\n");
			return 1;
		}

		// Set global pointer so encoder threads can send frames
		webrtc::g_server = &webrtc_server;

		// Launch HTTP server thread
		std::thread http_server_thread(webserver::http_server_main,
		                                &webrtc_config, &api_context);

		// Launch WebRTC signaling server thread
		std::thread webrtc_server_thread(webrtc::webrtc_server_main,
		                                  &webrtc_server, &webrtc_signaling::g_running);

		// Launch CPU execution thread (always, even if no ROM loaded yet)
		printf("Launching CPU execution thread (CPU starts in stopped state)...\n");
		std::thread cpu_thread([]() {
			printf("[CPU Thread] Waiting for start signal...\n");

			// Wait for CPU to be started via API
			while (webserver::g_running.load(std::memory_order_acquire)) {
				// Wait for CPU to be started (no busy polling)
				{
					std::unique_lock<std::mutex> lock(cpu_state::g_mutex);
					cpu_state::g_cv.wait(lock, []() {
						return cpu_state::g_running.load() || !webserver::g_running.load();
					});
				}

				// Check if we should exit
				if (!webserver::g_running.load(std::memory_order_acquire)) {
					break;
				}

				// Check if emulator is initialized
				if (!g_cpu_ctx.is_initialized()) {
					printf("[CPU Thread] CPU start requested but emulator not initialized yet\n");
					cpu_state::g_running.store(false);  // Reset flag
					continue;  // Go back to waiting
				}

				// CPU is running - execute until stopped
				printf("[CPU Thread] CPU started, executing...\n");

				// Simple execution loop using CPUContext
				Platform* platform = g_cpu_ctx.get_platform();
				if (platform->cpu_execute_fast) {
					// Fast path (Unicorn, DualCPU)
					while (cpu_state::g_running.load(std::memory_order_acquire) &&
					       webserver::g_running.load(std::memory_order_acquire)) {
						platform->cpu_execute_one();
					}
				} else {
					// Slow path (UAE) - execute one instruction at a time
					while (cpu_state::g_running.load(std::memory_order_acquire) &&
					       webserver::g_running.load(std::memory_order_acquire)) {
						platform->cpu_execute_one();
					}
				}

				if (!cpu_state::g_running.load(std::memory_order_acquire)) {
					printf("[CPU Thread] CPU stopped by user\n");
				}
			}
			printf("[CPU Thread] CPU execution thread exiting\n");
		});

		printf("Emulator ready. Open http://localhost:%d in your browser.\n", webrtc_config.web.http_port);
		if (g_cpu_ctx.is_initialized()) {
			printf("CPU loaded. Click 'Start' in the web UI to begin emulation.\n");
		} else {
			printf("No ROM loaded - configure ROM path in web UI.\n");
		}
		printf("Press Ctrl+C to exit.\n\n");

		// Wait for threads to complete (run until shutdown signal)
		http_server_thread.join();
		webrtc_server_thread.join();
		if (cpu_thread.joinable()) {
			cpu_thread.join();
		}
	} else {
		// Headless mode: Run CPU if initialized
		if (g_cpu_ctx.is_initialized()) {
			printf("\n=== CPU Execution Mode (Headless) ===\n");
			printf("Starting CPU execution...\n");
			printf("Press Ctrl+C to exit.\n\n");

			// Run CPU execution loop using CPUContext
			Platform* platform = g_cpu_ctx.get_platform();
			if (platform->cpu_execute_fast) {
				// Fast path (Unicorn, DualCPU)
				platform->cpu_execute_fast();
			} else {
				// Slow path (UAE) - execute one instruction at a time
				while (webserver::g_running.load(std::memory_order_acquire)) {
					platform->cpu_execute_one();
				}
			}
		} else {
			printf("\n=== Idle Mode ===\n");
			printf("No ROM loaded.\n");
			printf("Press Ctrl+C to exit.\n\n");

			// Wait for shutdown signal
			while (webserver::g_running.load(std::memory_order_acquire)) {
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}
	}

	printf("\n=== Shutting Down ===\n");
	stop_timer_interrupt();

	// Should never reach here
	ExitAll();
	return 0;
}
