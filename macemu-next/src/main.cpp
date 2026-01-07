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

// WebRTC streaming
#include "config/config_manager.h"
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

// CPU emulation state
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

	// Initialize platform with null drivers
	platform_init();

	// Initialize random number generator
	srand(time(NULL));

	// Set RAM size before PrefsInit
	RAMSize = 32 * 1024 * 1024;  // 32MB

	// Check for --config option first (before PrefsInit consumes it)
	const char *config_file_override = NULL;
	for (int i = 1; i < argc; i++) {
		if (argv[i] && strcmp(argv[i], "--config") == 0 && i+1 < argc) {
			config_file_override = argv[i+1];
			break;
		}
	}

	// Read preferences (minimal)
	// This processes --config and other options, modifying argc/argv
	PrefsInit(NULL, argc, argv);

	// Check for --no-webserver flag
	bool enable_webserver = true;
	for (int i = 1; i < argc; i++) {
		if (argv[i] && strcmp(argv[i], "--no-webserver") == 0) {
			enable_webserver = false;
			argv[i] = NULL;  // Remove from argv
			break;
		}
	}

	// Determine config file path
	std::string config_path;
	if (config_file_override) {
		config_path = config_file_override;
	} else {
		const char* home = getenv("HOME");
		config_path = std::string(home) + "/.config/macemu-next/config.json";
	}

	// Load WebRTC configuration if needed
	static config::MacemuConfig webrtc_config;  // Static so lambda can capture
	if (enable_webserver) {
		webrtc_config = config::load_config(config_path);
		webrtc::g_config = &webrtc_config;  // Store pointer for driver init
	}

	// Install platform drivers (video/audio)
	if (enable_webserver) {
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
		// Tracing mode: Keep null drivers (no threads)
		printf("Using null video/audio drivers (headless mode)...\n");
		// null drivers already installed by platform_init()
	}

	// Check for ROM file argument (optional when WebRTC enabled)
	// Skip NULL entries in argv (consumed by PrefsInit)
	const char *rom_path = NULL;
	for (int i = 1; i < argc; i++) {
		if (argv[i] != NULL) {
			rom_path = argv[i];
			break;
		}
	}

	// ROM file is optional (webserver mode)
	if (!rom_path) {
		printf("No ROM file specified. Starting in webserver mode.\n");
		printf("You can configure the ROM path in the web UI.\n");
	}
	PrefsAddInt32("ramsize", RAMSize);
	PrefsAddInt32("cpu", 4);  // 68040
	PrefsAddBool("fpu", true);

	printf("Allocating RAM (%d MB)...\n", RAMSize / (1024 * 1024));

	// Allocate RAM
	RAMBaseHost = (uint8 *)mmap(NULL, RAMSize + 0x100000,
	                             PROT_READ | PROT_WRITE,
	                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (RAMBaseHost == MAP_FAILED) {
		fprintf(stderr, "Failed to allocate RAM\n");
		return 1;
	}

	ROMBaseHost = RAMBaseHost + RAMSize;
	memset(RAMBaseHost, 0, RAMSize);

#if DIRECT_ADDRESSING
	MEMBaseDiff = (uintptr)RAMBaseHost;
	RAMBaseMac = 0;
	ROMBaseMac = Host2MacAddr(ROMBaseHost);
#endif

	printf("RAM at %p (Mac: 0x%08x)\n", RAMBaseHost, RAMBaseMac);
	printf("ROM at %p (Mac: 0x%08x)\n", ROMBaseHost, ROMBaseMac);

	// Load ROM (skip if no ROM path specified - webserver mode)
	if (rom_path) {
		printf("\nLoading ROM from %s...\n", rom_path);
		int rom_fd = open(rom_path, O_RDONLY);
		if (rom_fd < 0) {
			fprintf(stderr, "Failed to open ROM file: %s\n", rom_path);
			return 1;
		}

		ROMSize = lseek(rom_fd, 0, SEEK_END);
		printf("ROM size: %d bytes (%d KB)\n", ROMSize, ROMSize / 1024);

		if (ROMSize != 64*1024 && ROMSize != 128*1024 && ROMSize != 256*1024 &&
		    ROMSize != 512*1024 && ROMSize != 1024*1024) {
			fprintf(stderr, "Invalid ROM size (must be 64/128/256/512/1024 KB)\n");
			close(rom_fd);
			return 1;
		}

		lseek(rom_fd, 0, SEEK_SET);
		if (read(rom_fd, ROMBaseHost, ROMSize) != (ssize_t)ROMSize) {
			fprintf(stderr, "Failed to read ROM file\n");
			close(rom_fd);
			return 1;
		}
		close(rom_fd);

		printf("ROM loaded successfully (kept in big-endian format)\n");
	} else {
		printf("\nNo ROM file specified - skipping ROM load (webserver mode)\n");
		ROMSize = 0;  // No ROM loaded
	}
	// ============================================================
	// Initialize Emulator (inlined from InitAll())
	// ============================================================
	printf("\n=== Initializing Emulator ===\n");

	// Check ROM version (skip if no ROM loaded)
	if (ROMSize > 0 && !CheckROM()) {
		ErrorAlert(STR_UNSUPPORTED_ROM_TYPE_ERR);
		return 1;
	}

#if EMULATED_68K
	// Set CPU and FPU type (UAE emulation)
	if (ROMSize > 0) {
		switch (ROMVersion) {
			case ROM_VERSION_64K:
			case ROM_VERSION_PLUS:
			case ROM_VERSION_CLASSIC:
				CPUType = 0;
				FPUType = 0;
				TwentyFourBitAddressing = true;
				break;
			case ROM_VERSION_II:
				CPUType = PrefsFindInt32("cpu");
				if (CPUType < 2) CPUType = 2;
				if (CPUType > 4) CPUType = 4;
				FPUType = PrefsFindBool("fpu") ? 1 : 0;
				if (CPUType == 4) FPUType = 1;	// 68040 always with FPU
				TwentyFourBitAddressing = true;
				break;
			case ROM_VERSION_32:
				CPUType = PrefsFindInt32("cpu");
				if (CPUType < 2) CPUType = 2;
				if (CPUType > 4) CPUType = 4;
				FPUType = PrefsFindBool("fpu") ? 1 : 0;
				if (CPUType == 4) FPUType = 1;	// 68040 always with FPU
				TwentyFourBitAddressing = false;
				break;
		}
	}
	CPUIs68060 = false;
#endif

	// Load XPRAM
	XPRAMInit(NULL);

	// Load XPRAM default values if signature not found
	if (XPRAM[0x0c] != 0x4e || XPRAM[0x0d] != 0x75
	 || XPRAM[0x0e] != 0x4d || XPRAM[0x0f] != 0x63) {
		D(bug("Loading XPRAM default values\n"));
		memset(XPRAM, 0, 0x100);
		XPRAM[0x0c] = 0x4e;	// "NuMc" signature
		XPRAM[0x0d] = 0x75;
		XPRAM[0x0e] = 0x4d;
		XPRAM[0x0f] = 0x63;
		XPRAM[0x01] = 0x80;	// InternalWaitFlags = DynWait
		XPRAM[0x10] = 0xa8;	// Standard PRAM values
		XPRAM[0x11] = 0x00;
		XPRAM[0x12] = 0x00;
		XPRAM[0x13] = 0x22;
		XPRAM[0x14] = 0xcc;
		XPRAM[0x15] = 0x0a;
		XPRAM[0x16] = 0xcc;
		XPRAM[0x17] = 0x0a;
		XPRAM[0x1c] = 0x00;
		XPRAM[0x1d] = 0x02;
		XPRAM[0x1e] = 0x63;
		XPRAM[0x1f] = 0x00;
		XPRAM[0x08] = 0x13;
		XPRAM[0x09] = 0x88;
		XPRAM[0x0a] = 0x00;
		XPRAM[0x0b] = 0xcc;
		XPRAM[0x76] = 0x00;	// OSDefault = MacOS
		XPRAM[0x77] = 0x01;
	}

	// Set boot volume
	int16 i16 = PrefsFindInt32("bootdrive");
	XPRAM[0x78] = i16 >> 8;
	XPRAM[0x79] = i16 & 0xff;
	i16 = PrefsFindInt32("bootdriver");
	XPRAM[0x7a] = i16 >> 8;
	XPRAM[0x7b] = i16 & 0xff;

	// Init drivers
	SonyInit();
	DiskInit();
	CDROMInit();
	SCSIInit();

#if SUPPORTS_EXTFS
	// Init external file system
	ExtFSInit();
#endif

	// Init serial ports
	SerialInit();

	// Init network
	EtherInit();

	// Init Time Manager
	TimerInit();

	// Init clipboard
	ClipInit();

	// Init ADB
	ADBInit();

	// Init audio
	AudioInit();

	// Init video
	if (!VideoInit(ROMVersion == ROM_VERSION_64K || ROMVersion == ROM_VERSION_PLUS || ROMVersion == ROM_VERSION_CLASSIC)) {
		fprintf(stderr, "Video initialization failed\n");
		return 1;
	}

	// Set default video mode in XPRAM
	XPRAM[0x56] = 0x42;	// 'B'
	XPRAM[0x57] = 0x32;	// '2'
	const monitor_desc &main_monitor = *VideoMonitors[0];
	XPRAM[0x58] = uint8(main_monitor.depth_to_apple_mode(main_monitor.get_current_mode().depth));
	XPRAM[0x59] = 0;

	// ============================================================
	// Select CPU Backend (before initialization)
	// ============================================================
	// CURRENT ARCHITECTURE (as of 2025):
	//   - All backends (UAE/Unicorn/DualCPU) use Init680x0()
	//   - Init680x0() sets up UAE's memory banking system
	//   - PatchROM() uses UAE's WriteMacInt*() to patch ROM
	//   - Unicorn copies the patched ROM in unicorn_backend_init()
	//
	// FUTURE ARCHITECTURE (goal for Unicorn-only):
	//   - Remove Init680x0() call for Unicorn backend
	//   - Use direct memory access (see memory_access.h)
	//   - PatchROM() uses DirectWriteMacInt*() instead of UAE functions
	//   - Completely eliminate UAE dependency for Unicorn-only builds
	//
	// To achieve this:
	//   1. Make Init680x0() conditional (only for UAE backend)
	//   2. Switch PatchROM() to use backend-independent memory functions
	//   3. Move RAM/ROM variable definitions out of basilisk_glue.cpp
	//   4. Create unicorn-specific build configuration
	// ============================================================

	// Select CPU backend via environment variable
	const char *cpu_backend = getenv("CPU_BACKEND");
	if (!cpu_backend) {
		cpu_backend = "uae";  // Default
	}

	printf("\n=== Selected CPU Backend: %s ===\n", cpu_backend);

	// Skip CPU initialization if no ROM loaded (webserver mode)
	if (ROMSize > 0) {
#if EMULATED_68K
		// Init 680x0 emulation (UAE's memory banking system)
		// NOTE: Required for all backends currently, but Unicorn will use direct access in future
		if (!Init680x0()) {
			fprintf(stderr, "CPU initialization failed\n");
			return 1;
		}
#endif

	// ============================================================
	// Install CPU Backend (BEFORE PatchROM)
	// ============================================================
	// We install the backend early so that PatchROM() can use g_platform.mem_*
	// functions for backend-independent memory access
	// ============================================================

	if (strcmp(cpu_backend, "unicorn") == 0) {
		cpu_unicorn_install(&g_platform);
	} else if (strcmp(cpu_backend, "dualcpu") == 0) {
		cpu_dualcpu_install(&g_platform);
	} else {
		cpu_uae_install(&g_platform);  // Default to UAE
	}

	printf("CPU Backend: %s\n", g_platform.cpu_name);

	// Configure CPU type (must be called after backend install, before cpu_init)
	if (g_platform.cpu_set_type) {
		g_platform.cpu_set_type(CPUType, FPUType);
	}

	// ============================================================
	// Install ROM Patches
	// ============================================================
	// NOTE: PatchROM() currently still uses UAE's WriteMacInt*() functions directly.
	// The Platform API provides g_platform.mem_write_*() functions for backend-
	// independent memory access, but PatchROM() hasn't been converted yet.
	//
	// Future: Convert PatchROM() to use g_platform.mem_* functions to enable
	// Unicorn-only builds without UAE dependency.
	//
	// Patches include runtime addresses and may differ between runs due to ASLR.
	// ============================================================
	if (ROMSize > 0) {
		if (!PatchROM()) {
			ErrorAlert(STR_UNSUPPORTED_ROM_TYPE_ERR);
			return 1;
		}
	}

#if ENABLE_MON
	// Initialize mon
	mon_init();
	mon_read_byte = mon_read_byte_b2;
	mon_write_byte = mon_write_byte_b2;
#endif

	printf("\n=== Initialization Complete ===\n");
	if (ROMSize > 0) {
		printf("ROM Version: 0x%08x\n", ROMVersion);
	}
	printf("CPU Type: 680%02d\n", (CPUType == 0) ? 0 : (CPUType * 10 + 20));
	printf("FPU: %s\n", FPUType ? "Yes" : "No");
	printf("24-bit addressing: %s\n", TwentyFourBitAddressing ? "Yes" : "No");

	// ============================================================
	// Initialize CPU Backend
	// ============================================================
	printf("\n=== Starting Emulation ===\n");

	// Backend was already installed before PatchROM() to provide memory access functions
	// Now we call cpu_init() to complete backend initialization
	if (!g_platform.cpu_init()) {
		fprintf(stderr, "Failed to initialize CPU\n");
		return 1;
	}

	// Reset CPU to ROM entry point
	g_platform.cpu_reset();
	printf("CPU reset to PC=0x%08x\n", g_platform.cpu_get_pc());

	// Set up 60Hz timer (polling-based)
	printf("\n=== Setting up Timer Interrupt ===\n");
	setup_timer_interrupt();

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
		printf("Skipping CPU initialization (no ROM loaded - webserver mode)\n");
	}

	// ============================================================
	// HTTP Server (WebRTC Mode Only)
	// ============================================================
	if (enable_webserver) {
		// Set up API context for HTTP handlers
		CodecType server_codec = CodecType::PNG;  // Default
		http::APIContext api_context;
		api_context.debug_connection = false;
		api_context.debug_mode_switch = false;
		api_context.debug_perf = false;
		api_context.prefs_path = config_path;
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

		// Launch HTTP server thread
		std::thread http_server_thread(webserver::http_server_main,
		                                &webrtc_config, &api_context);

		// Launch WebRTC signaling server thread
		std::thread webrtc_server_thread(webrtc::webrtc_server_main,
		                                  &webrtc_server, &webrtc_signaling::g_running);

		// Launch CPU execution thread (if ROM is loaded)
		std::thread cpu_thread;
		if (ROMSize > 0) {
			printf("Launching CPU execution thread (CPU starts in stopped state)...\n");
			cpu_thread = std::thread([]() {
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

					// CPU is running - execute instructions
					printf("[CPU Thread] CPU started, executing...\n");
					if (g_platform.cpu_execute_fast) {
						// Fast path (Unicorn, DualCPU)
						while (cpu_state::g_running.load(std::memory_order_acquire) &&
						       webserver::g_running.load(std::memory_order_acquire)) {
							g_platform.cpu_execute_one();
						}
					} else {
						// Slow path - execute one instruction at a time (UAE)
						while (cpu_state::g_running.load(std::memory_order_acquire) &&
						       webserver::g_running.load(std::memory_order_acquire)) {
							g_platform.cpu_execute_one();
						}
					}

					if (!cpu_state::g_running.load(std::memory_order_acquire)) {
						printf("[CPU Thread] CPU stopped by user\n");
					}
				}
				printf("[CPU Thread] CPU execution thread exiting\n");
			});
		}

		printf("Emulator ready. Open http://localhost:%d in your browser.\n", webrtc_config.web.http_port);
		if (ROMSize > 0) {
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
		// Tracing/Headless mode: Run CPU if ROM loaded
		if (ROMSize > 0) {
			printf("\n=== CPU Execution Mode (Headless) ===\n");
			printf("Starting CPU execution...\n");
			printf("Press Ctrl+C to exit.\n\n");

			// Run CPU execution loop
			if (g_platform.cpu_execute_fast) {
				// Fast path (Unicorn, DualCPU)
				g_platform.cpu_execute_fast();
			} else {
				// Slow path - execute one instruction at a time (UAE)
				while (webserver::g_running.load(std::memory_order_acquire)) {
					g_platform.cpu_execute_one();
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
