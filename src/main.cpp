/*
 *  main.cpp - mac-phoenix main entry point
 *
 *  Two modes:
 *  - Webserver: Fork-based CPU process. Parent runs HTTP/WebRTC/encoders,
 *    child runs CPU. Stop = kill(SIGKILL), Reset = Stop+Start.
 *  - Headless: CPU runs directly in main process (no fork).
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
#include "core/emulator_init.h"
#include "crash_handler_init.h"
#include "sigsegv.h"

// WebRTC streaming
#include "config/emulator_config.h"
#include "core/cpu_context.h"
#include "core/cpu_process.h"
#include "core/shared_state.h"
#include "core/boot_progress.h"
#include "drivers/video/video_webrtc.h"
#include "drivers/video/video_screenshot.h"
#include "drivers/video/video_output.h"
#include "drivers/video/video_encoder_thread.h"
#include "drivers/audio/audio_webrtc.h"
#include "webserver/webserver_main.h"
#include "webserver/api_handlers.h"
#include "webrtc/webrtc_server.h"
#include "drivers/video/encoders/codec.h"

// WebRTC globals
namespace webrtc {
	std::atomic<bool> g_running(true);
	std::atomic<bool> g_request_keyframe(false);
	WebRTCServer* g_server = nullptr;
}

// Video encoder globals
namespace video {
	std::atomic<bool> g_running(true);
	std::atomic<bool> g_request_keyframe(false);
	extern VideoOutput* g_video_output;  // defined in video_webrtc.cpp
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

// Global CPU context (used in headless mode and by child process)
CPUContext g_cpu_ctx;

// CPU emulation state (legacy, for headless mode compatibility)
namespace cpu_state {
	std::atomic<bool> g_running(false);
	std::mutex g_mutex;
	std::condition_variable g_cv;
}

// Global shared state pointer (used by webrtc_server.cpp for input forwarding)
SharedState* g_shared_state = nullptr;

// Global CPU process pointer for SIGTERM cleanup
static CpuProcess* g_cpu_process = nullptr;

static void sigterm_handler(int sig)
{
	(void)sig;
	// Kill child CPU process so ports are freed
	if (g_cpu_process) {
		g_cpu_process->stop();
		g_cpu_process = nullptr;
	}
	_exit(0);
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

// Interrupt control (minimal stubs)
void DisableInterrupt(void) {}
void EnableInterrupt(void) {}

/*
 *  SIGSEGV handler - skip illegal memory accesses
 */
static sigsegv_return_t sigsegv_handler(sigsegv_info_t *sip)
{
	const uintptr fault_address = (uintptr)sigsegv_get_fault_address(sip);

#ifdef HAVE_SIGSEGV_SKIP_INSTRUCTION
	if (ROMBaseHost && ((uintptr)fault_address - (uintptr)ROMBaseHost) < ROMSize)
		return SIGSEGV_RETURN_SKIP_INSTRUCTION;
	return SIGSEGV_RETURN_SKIP_INSTRUCTION;
#endif

	return SIGSEGV_RETURN_FAILURE;
}

int main(int argc, char **argv)
{
	printf("=== mac-phoenix ===\n\n");

	// Install crash handlers
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

	config::EmulatorConfig& emu_config = config::EmulatorConfig::instance();
	{
		config::EmulatorConfig loaded = config::load_emulator_config(
			default_config_path.c_str(), argc, argv);
		emu_config = std::move(loaded);
	}

	config::print_config(emu_config);

	// Set global debug/log state from config
	g_debug_mode_switch = emu_config.debug_mode_switch;
	set_log_level(emu_config.log_level);
	RAMSize = emu_config.ram_mb * 1024 * 1024;

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

	// ============================================================
	// Webserver Mode (Fork-Based CPU Process)
	// ============================================================
	if (emu_config.enable_webserver) {
		printf("\n=== WebRTC Mode (Fork-Based CPU) ===\n");

		// Create shared memory for parent-child communication
		SharedState* shm = create_shared_state();
		if (!shm) {
			fprintf(stderr, "FATAL: Failed to create shared memory (%zu bytes)\n", sizeof(SharedState));
			return 1;
		}
		g_shared_state = shm;
		printf("Shared memory created (%zu bytes)\n", sizeof(SharedState));

		// Create VideoOutput for parent-side encoder
		video::g_video_output = new VideoOutput(1920, 1080);

		// Create CPU process manager
		CpuProcess cpu_proc(shm, &emu_config);
		cpu_proc.set_video_output(video::g_video_output);
		g_cpu_process = &cpu_proc;

		// Install signal handlers to kill child process on exit
		signal(SIGTERM, sigterm_handler);
		signal(SIGINT, sigterm_handler);
		signal(SIGHUP, sigterm_handler);

		// Set up codec
		CodecType server_codec = CodecType::PNG;
		if (emu_config.codec == "h264") server_codec = CodecType::H264;
		else if (emu_config.codec == "vp9") server_codec = CodecType::VP9;
		else if (emu_config.codec == "webp") server_codec = CodecType::WEBP;

		// Set up API context
		http::APIContext api_context;
		api_context.config = &emu_config;
		api_context.server_codec = &server_codec;
		api_context.video_output = video::g_video_output;
		api_context.cpu_process = &cpu_proc;
		api_context.shared_state = shm;

		// Initialize WebRTC signaling server
		printf("Launching HTTP server on port %d...\n", emu_config.http_port);

		webrtc::WebRTCServer webrtc_server;
		if (!webrtc_server.init(emu_config.signaling_port)) {
			fprintf(stderr, "Failed to start WebRTC signaling server\n");
			return 1;
		}
		webrtc::g_server = &webrtc_server;

		// Wire up codec change notification
		api_context.notify_codec_change_fn = [&webrtc_server](CodecType codec) {
			fprintf(stderr, "[API] Codec changed to: %d, notifying WebRTC peers\n", static_cast<int>(codec));
			webrtc_server.notify_codec_change(codec);
		};

		// Launch HTTP server thread
		std::thread http_server_thread(webserver::http_server_main,
		                                &emu_config, &api_context);

		// Launch WebRTC signaling server thread
		std::thread webrtc_server_thread(webrtc::webrtc_server_main,
		                                  &webrtc_server, &webrtc_signaling::g_running);

		// Launch video encoder thread (reads from VideoOutput, which relay feeds)
		video::g_running.store(true, std::memory_order_release);
		std::thread encoder_thread(video::video_encoder_main,
		                            video::g_video_output, &emu_config);

		printf("Emulator ready. Open http://localhost:%d in your browser.\n", emu_config.http_port);
		printf("Click 'Start' in the web UI to begin emulation.\n");
		printf("Press Ctrl+C to exit.\n\n");

		// Wait for threads
		http_server_thread.join();
		webrtc_server_thread.join();

		// Shutdown
		cpu_proc.stop();
		video::g_running.store(false, std::memory_order_release);
		video::g_video_output->shutdown();
		encoder_thread.join();

		delete video::g_video_output;
		video::g_video_output = nullptr;
		destroy_shared_state(shm);
		g_shared_state = nullptr;

	} else {
		// ============================================================
		// Headless Mode (Direct CPU Execution, No Fork)
		// ============================================================
		const char *rom_path = emu_config.rom_path.empty() ? nullptr : emu_config.rom_path.c_str();

		if (rom_path) {
			printf("\n=== Initializing CPU Context (Headless) ===\n");

			// Install screenshot driver if requested
			if (emu_config.screenshots) {
				g_platform.video_init = video_screenshot_init;
				g_platform.video_exit = video_screenshot_exit;
				g_platform.video_refresh = video_screenshot_refresh;
			}

			// Copy platform into CPUContext
			Platform* platform = g_cpu_ctx.get_platform();
			*platform = g_platform;

			// Install CPU backend
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

			g_platform = *platform;

			// Initialize M68K
			if (!g_cpu_ctx.init_m68k(emu_config)) {
				fprintf(stderr, "Failed to initialize M68K CPU context\n");
				return 1;
			}

			// Install SIGSEGV handler
			if (!sigsegv_install_handler(sigsegv_handler)) {
				fprintf(stderr, "WARNING: Could not install SIGSEGV handler\n");
			}

			g_emulator_initialized = true;

			printf("\n=== CPU Execution Mode (Headless) ===\n");
			printf("Starting CPU execution...\n");
			printf("Press Ctrl+C to exit.\n\n");

			if (platform->cpu_execute_fast) {
				platform->cpu_execute_fast();
			} else {
				while (true) {
					platform->cpu_execute_one();
				}
			}
		} else {
			printf("\nNo ROM file specified.\n");
			return 1;
		}
	}

	printf("\n=== Shutting Down ===\n");
	stop_timer_interrupt();
	ExitAll();
	return 0;
}
