/*
 *  main.cpp - mac-phoenix main entry point
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
#include "sigsegv.h"  // SIGSEGV handler for skipping illegal memory accesses

// WebRTC streaming
#include "config/emulator_config.h"  // Unified configuration
#include "core/cpu_context.h"  // Phase 2: Self-contained CPU context
#include "core/boot_progress.h"  // For set_log_level
#include "drivers/video/video_webrtc.h"
#include "drivers/video/video_screenshot.h"
#include "drivers/audio/audio_webrtc.h"
#include "webserver/webserver_main.h"
#include "webserver/api_handlers.h"
#include "webrtc/webrtc_server.h"
#include "drivers/video/encoders/codec.h"

// WebRTC globals
namespace webrtc {
	std::atomic<bool> g_running(true);
	std::atomic<bool> g_request_keyframe(false);
	WebRTCServer* g_server = nullptr;  // Global WebRTC server for encoder threads to send frames
}

// Video encoder globals
namespace video {
	std::atomic<bool> g_running(true);
	std::atomic<bool> g_request_keyframe(false);
	extern class VideoOutput* g_video_output;  // Defined in video_webrtc.cpp
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

/*
 *  SIGSEGV handler - skip illegal memory accesses (like BasiliskII ignoresegv)
 */
static sigsegv_return_t sigsegv_handler(sigsegv_info_t *sip)
{
	const uintptr fault_address = (uintptr)sigsegv_get_fault_address(sip);

#ifdef HAVE_SIGSEGV_SKIP_INSTRUCTION
	// Ignore writes to ROM
	if (ROMBaseHost && ((uintptr)fault_address - (uintptr)ROMBaseHost) < ROMSize)
		return SIGSEGV_RETURN_SKIP_INSTRUCTION;

	// Ignore all other faults (equivalent to BasiliskII ignoresegv=true)
	return SIGSEGV_RETURN_SKIP_INSTRUCTION;
#endif

	return SIGSEGV_RETURN_FAILURE;
}

int main(int argc, char **argv)
{
	printf("=== mac-phoenix ===\n\n");

	// Install crash handlers (SIGSEGV, SIGBUS, etc.) with stack traces
	install_crash_handlers();

	// Initialize platform with null drivers
	platform_init();

	// Initialize random number generator
	srand(time(NULL));

	// ========================================
	// Load Unified Configuration
	// ========================================
	const char* home = getenv("HOME");
	std::string default_config_path = std::string(home) + "/.config/mac-phoenix/config.json";

	static config::EmulatorConfig emu_config = config::load_emulator_config(
		default_config_path.c_str(), argc, argv);

	// Print configuration for debugging
	config::print_config(emu_config);

	// Set global debug/log state from config
	g_debug_mode_switch = emu_config.debug_mode_switch;
	set_log_level(emu_config.log_level);

	// Set RAM size from config
	RAMSize = emu_config.ram_mb * 1024 * 1024;

	// Initialize prefs system with values from config (for legacy code)
	int dummy_argc = 0;
	char** dummy_argv = nullptr;
	PrefsInit(NULL, dummy_argc, dummy_argv);
	PrefsAddInt32("ramsize", RAMSize);
	PrefsAddInt32("cpu", emu_config.cpu_type_int());
	PrefsAddBool("fpu", emu_config.fpu());

	// Install platform drivers (video/audio)
	if (emu_config.enable_webserver) {
		printf("Installing WebRTC video/audio drivers...\n");
		g_platform.video_init = [](bool classic) -> bool {
			return video_webrtc_init(classic, &emu_config);
		};
		g_platform.video_exit = video_webrtc_exit;
		g_platform.video_refresh = video_webrtc_refresh;
		g_platform.audio_init = audio_webrtc_init;
		g_platform.audio_exit = audio_webrtc_exit;
	} else if (emu_config.screenshots) {
		printf("Installing screenshot video driver...\n");
		g_platform.video_init = video_screenshot_init;
		g_platform.video_exit = video_screenshot_exit;
		g_platform.video_refresh = video_screenshot_refresh;
	} else {
		printf("Using null video/audio drivers (headless mode)...\n");
	}

	// ========================================
	// Initialize CPU Context (Phase 2)
	// ========================================
	const char *rom_path = emu_config.rom_path.empty() ? nullptr : emu_config.rom_path.c_str();

	if (rom_path) {
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
		g_platform = *platform;

		// Initialize M68K
		if (!g_cpu_ctx.init_m68k(emu_config)) {
			fprintf(stderr, "Failed to initialize M68K CPU context\n");
			return 1;
		}

		// Install SIGSEGV handler
		if (!sigsegv_install_handler(sigsegv_handler)) {
			fprintf(stderr, "WARNING: Could not install SIGSEGV handler\n");
		} else {
			printf("[Init] SIGSEGV handler installed (ignoresegv mode)\n");
		}

		// Mark emulator as initialized
		g_emulator_initialized = true;

		// Auto-start CPU only in headless mode
		if (!emu_config.enable_webserver) {
			printf("[CPU] Auto-starting CPU (headless mode)\n");
			{
				std::lock_guard<std::mutex> lock(cpu_state::g_mutex);
				cpu_state::g_running.store(true);
			}
			cpu_state::g_cv.notify_one();
		} else {
			printf("[CPU] WebRTC mode - CPU will start when user clicks 'Start' in web UI\n");
		}

		// Auto-exit timer
		if (emu_config.timeout_seconds > 0) {
			int timeout_sec = emu_config.timeout_seconds;
			printf("Auto-exit timer set: %d seconds\n", timeout_sec);
			std::thread timeout_thread([timeout_sec]() {
				std::this_thread::sleep_for(std::chrono::seconds(timeout_sec));
				fprintf(stderr, "\n[Timeout: %d seconds elapsed, exiting]\n", timeout_sec);
				exit(0);
			});
			timeout_thread.detach();
		}
	} else {
		printf("\nNo ROM file specified. Starting in webserver mode.\n");
		printf("You can configure the ROM path in the web UI.\n");
	}

	// ============================================================
	// HTTP Server (WebRTC Mode Only)
	// ============================================================
	if (emu_config.enable_webserver) {
		// Set up API context for HTTP handlers
		// Initialize server_codec from config
		CodecType server_codec = CodecType::PNG;
		if (emu_config.codec == "h264") server_codec = CodecType::H264;
		// AV1 not implemented — no encoder exists
		else if (emu_config.codec == "vp9") server_codec = CodecType::VP9;
		else if (emu_config.codec == "webp") server_codec = CodecType::WEBP;
		http::APIContext api_context;
		api_context.config = &emu_config;
		api_context.server_codec = &server_codec;
		api_context.video_output = video::g_video_output;
		api_context.cpu_running = &cpu_state::g_running;
		api_context.cpu_mutex = &cpu_state::g_mutex;
		api_context.cpu_cv = &cpu_state::g_cv;
		api_context.notify_codec_change_fn = [](CodecType codec) {
			fprintf(stderr, "[API] Codec changed to: %d\n", static_cast<int>(codec));
		};

		// Initialize WebRTC signaling server
		printf("\n=== WebRTC Mode ===\n");
		printf("Launching HTTP server on port %d...\n", emu_config.http_port);

		webrtc::WebRTCServer webrtc_server;
		if (!webrtc_server.init(emu_config.signaling_port)) {
			fprintf(stderr, "Failed to start WebRTC signaling server\n");
			return 1;
		}

		// Set global pointer so encoder threads can send frames
		webrtc::g_server = &webrtc_server;

		// Launch HTTP server thread
		std::thread http_server_thread(webserver::http_server_main,
		                                &emu_config, &api_context);

		// Launch WebRTC signaling server thread
		std::thread webrtc_server_thread(webrtc::webrtc_server_main,
		                                  &webrtc_server, &webrtc_signaling::g_running);

		// Launch CPU execution thread
		printf("Launching CPU execution thread (CPU starts in stopped state)...\n");
		std::thread cpu_thread([]() {
			printf("[CPU Thread] Waiting for start signal...\n");

			while (webserver::g_running.load(std::memory_order_acquire)) {
				{
					std::unique_lock<std::mutex> lock(cpu_state::g_mutex);
					cpu_state::g_cv.wait(lock, []() {
						return cpu_state::g_running.load() || !webserver::g_running.load();
					});
				}

				if (!webserver::g_running.load(std::memory_order_acquire)) {
					break;
				}

				if (!g_cpu_ctx.is_initialized()) {
					printf("[CPU Thread] CPU start requested but emulator not initialized yet\n");
					cpu_state::g_running.store(false);
					continue;
				}

				Platform* platform = g_cpu_ctx.get_platform();
				printf("[CPU Thread] CPU started, executing...\n");

				if (platform->cpu_execute_fast) {
					std::thread watchdog([platform]() {
						while (cpu_state::g_running.load(std::memory_order_acquire) &&
						       webserver::g_running.load(std::memory_order_acquire)) {
							std::this_thread::sleep_for(std::chrono::milliseconds(50));
						}
						if (platform->cpu_request_stop) {
							platform->cpu_request_stop();
						}
					});
					platform->cpu_execute_fast();
					watchdog.join();
				} else {
					while (cpu_state::g_running.load(std::memory_order_acquire) &&
					       webserver::g_running.load(std::memory_order_acquire)) {
						platform->cpu_execute_one();
					}
				}

				printf("[CPU Thread] CPU stopped\n");
			}
			printf("[CPU Thread] CPU execution thread exiting\n");
		});

		printf("Emulator ready. Open http://localhost:%d in your browser.\n", emu_config.http_port);
		if (g_cpu_ctx.is_initialized()) {
			printf("CPU loaded. Click 'Start' in the web UI to begin emulation.\n");
		} else {
			printf("No ROM loaded - configure ROM path in web UI.\n");
		}
		printf("Press Ctrl+C to exit.\n\n");

		// Wait for threads to complete
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

			Platform* platform = g_cpu_ctx.get_platform();
			if (platform->cpu_execute_fast) {
				fprintf(stderr, "[CPU] Using FAST execution path (cpu_execute_fast)\n");
				platform->cpu_execute_fast();
			} else {
				fprintf(stderr, "[CPU] Using SLOW execution path (cpu_execute_one loop)\n");
				while (webserver::g_running.load(std::memory_order_acquire)) {
					platform->cpu_execute_one();
				}
			}
		} else {
			printf("\n=== Idle Mode ===\n");
			printf("No ROM loaded.\n");
			printf("Press Ctrl+C to exit.\n\n");

			while (webserver::g_running.load(std::memory_order_acquire)) {
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}
	}

	printf("\n=== Shutting Down ===\n");
	stop_timer_interrupt();

	ExitAll();
	return 0;
}
