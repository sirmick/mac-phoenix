/*
 *  main.cpp - mac-phoenix main entry point
 *
 *  Two modes:
 *  - Webserver: Subprocess-based CPU process. Parent runs HTTP/WebRTC/encoders,
 *    child runs CPU via --ipc. Stop = kill(SIGTERM), Reset = Stop+Start.
 *  - Headless: CPU runs directly in main process (no subprocess).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>
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
#include "machine_profile.h"
#include "user_strings.h"
#include "platform.h"
#include "extfs.h"
#include "drivers/platform/timer_interrupt.h"
#include "core/emulator_init.h"
#include "crash_handler_init.h"
#include "sigsegv.h"
#include "drivers/browser/browser_spike.h"
#include "drivers/browser/shm.h"
#include "drivers/browser/module.h"

// ============================================================================
// Early Mac address space reservation — runs BEFORE main(), before any threads.
//
// The Gossamer ROM nanokernel probes the 32-bit address space during init.
// pthread thread stacks (video encoder, tick timer) get placed by ASLR at
// ~0x40xxxxxx — within Mac address space. The nanokernel reads this data
// and takes a different init path than legacy SheepShaver (which has nothing
// mapped there). We must reserve these gaps BEFORE any threads are created.
//
// Priority 101 = earliest user-allowed priority (< 100 reserved for libc).
// All emulator allocations use MAP_FIXED which punches through these guards.
// ============================================================================
static int g_mac_guard_count = 0;

__attribute__((constructor(101)))
static void reserve_mac_address_space_early()
{
	// Guard Mac address space gaps: 0x08000000 - 0x70000000
	// EXCEPT 0x10000000-0x11500000 which is needed for SheepMem + JIT cache.
	// The JIT dyngen ops use rel32 calls that must be within ±2GB of the
	// binary text (~0x78048000). MAP_BASE is 0x10C00000 — if the JIT cache
	// can't allocate there, it goes to a high address and rel32 calls fail.
	//
	// Use 4MB chunks with MAP_FIXED_NOREPLACE to skip existing mappings.
	// All emulator allocations use MAP_FIXED which punches through guards.
	// EXPERIMENT: Disable guard pages to match legacy SheepShaver memory layout.
	// Legacy has NO guard pages. The guards might affect nanokernel memory probing.
	const struct { uintptr_t start; uintptr_t end; } ranges[] = {
		// All ranges disabled for testing
	};
	const size_t chunk = 0x400000; // 4MB
	int count = 0;
	for (const auto &r : ranges) {
		for (uintptr_t addr = r.start; addr < r.end; addr += chunk) {
			size_t sz = chunk;
			if (addr + sz > r.end) sz = r.end - addr;
			void *p = mmap((void *)addr, sz, PROT_NONE,
			               MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
			if (p != MAP_FAILED) count++;
		}
	}
	g_mac_guard_count = count;
	(void)count;
}

// WebRTC streaming
#include "config/emulator_config.h"
#include "core/command_bridge.h"
#include "core/network_info.h"
#include "drivers/ether/ether_socket.h"
#include "core/cpu_context.h"
#include "core/emulator_subprocess.h"
#include "core/boot_progress.h"
#include "ipc/ipc_protocol.h"
#include "drivers/video/video_webrtc.h"
#include "drivers/video/video_screenshot.h"
#include "drivers/video/video_output.h"
#include "drivers/video/video_encoder_thread.h"
#include "drivers/audio/audio_webrtc.h"
#include "drivers/audio/audio_direct.h"
#include "drivers/audio/audio_ipc_reader.h"
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
	std::atomic<IPCBuffer*> g_ipc_shm{nullptr};  // Set when subprocess connects; encoder reads directly
	std::atomic<int> g_ipc_eventfd{-1};          // Parent's copy of the frame-ready eventfd
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

// Global subprocess pointer for SIGTERM cleanup
static EmulatorSubprocess* g_subprocess = nullptr;

// Global IPC client pointer (for webrtc_server.cpp input forwarding)
IPCClient* g_ipc_client = nullptr;

static void sigterm_handler(int sig)
{
	(void)sig;
	if (g_subprocess) {
		g_subprocess->stop();
		g_subprocess = nullptr;
	}
	_exit(0);
}

// Debug flags
bool g_debug_png = false;
bool g_debug_mode_switch = false;

// IPC child-side functions (from src/ipc/)
extern "C" {
bool video_ipc_init(int width, int height);
void video_ipc_exit(void);
void video_ipc_set_framebuffer(const uint8_t* fb);
void video_ipc_set_resolution(int width, int height);
void video_ipc_refresh(void);
IPCBuffer* video_ipc_get_buffer(void);
void video_ipc_set_depth(int depth_bits, int bytes_per_row);
void video_ipc_set_palette(const uint8_t *pal, int num);
bool control_ipc_init(IPCBuffer* shm);
void control_ipc_start(void);
void control_ipc_exit(void);
void video_ipc_unlink(void);
void control_ipc_unlink(void);
}

// CPU backend install functions
extern "C" {
void cpu_uae_install(Platform* platform);
void cpu_unicorn_install(Platform* platform);
void cpu_dualcpu_install(Platform* platform);
}

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
	stop_timer_interrupt();
	ExitAll();
	exit(1);
}

// Interrupt control — weak stubs, overridden by KPX for PPC mode
__attribute__((weak)) void DisableInterrupt(void) {}
__attribute__((weak)) void EnableInterrupt(void) {}

/*
 *  SIGSEGV handler - skip illegal memory accesses
 */
// Forward declaration for PPC flight recorder dump
extern "C" void ppc_dump_flight_recorder(const char *filename);
__attribute__((weak)) void ppc_dump_flight_recorder(const char *filename) { (void)filename; }

static int sigsegv_count = 0;
static sigsegv_return_t sigsegv_handler(sigsegv_info_t *sip)
{
#ifdef HAVE_SIGSEGV_SKIP_INSTRUCTION
	const uintptr fault_address = reinterpret_cast<uintptr>(sigsegv_get_fault_address(sip));
	const uintptr fault_pc = reinterpret_cast<uintptr>(sigsegv_get_fault_instruction_address(sip));
	if (++sigsegv_count <= 10)
		fprintf(stderr, "[SIGSEGV] #%d fault at 0x%lx  host_pc=0x%lx\n",
				sigsegv_count, (unsigned long)fault_address, (unsigned long)fault_pc);
	if (sigsegv_count == 1) {
		ppc_dump_flight_recorder("/tmp/mp_sigsegv_trace.log");
		fprintf(stderr, "[SIGSEGV] Flight recorder dumped to /tmp/mp_sigsegv_trace.log\n");
	}
	return SIGSEGV_RETURN_SKIP_INSTRUCTION;
#else
	(void)sip;
	return SIGSEGV_RETURN_FAILURE;
#endif
}

// ============================================================
// m68k IPC video init — sets up monitor descriptors and framebuffer
// for the IPC child subprocess
// ============================================================

// m68k framebuffer state for IPC mode
static uint8_t* g_ipc_m68k_fb = nullptr;
static int g_ipc_m68k_width = 1024;
static int g_ipc_m68k_height = 768;

// Map a video_depth enum value to its bit count for logging / IPC.
static int vdepth_to_bits(video_depth d) {
    switch (d) {
        case VDEPTH_1BIT: return 1;
        case VDEPTH_2BIT: return 2;
        case VDEPTH_4BIT: return 4;
        case VDEPTH_8BIT: return 8;
        case VDEPTH_16BIT: return 16;
        case VDEPTH_32BIT: return 32;
        default: return 0;
    }
}

class ipc_monitor_desc : public monitor_desc {
public:
    ipc_monitor_desc(const vector<video_mode> &modes, video_depth depth, uint32 id)
        : monitor_desc(modes, depth, id) {
        // Classic Mac CLUT default: index 0 = white, index 255 = black.
        // Mac OS populates the real palette via SetEntries during boot.
        memset(pal_, 0, sizeof(pal_));
        pal_[0] = pal_[1] = pal_[2] = 255;
    }
    ~ipc_monitor_desc() {}

    void switch_to_current_mode(void) {
        const video_mode &mode = get_current_mode();
        g_ipc_m68k_width = mode.x;
        g_ipc_m68k_height = mode.y;
        int bits = vdepth_to_bits(mode.depth);
        uint32_t fb_size = (uint32_t)mode.bytes_per_row * mode.y;
        if (g_ipc_m68k_fb && fb_size <= 0x800000) {
            memset(g_ipc_m68k_fb, 0, fb_size);
        }
        video_ipc_set_framebuffer(g_ipc_m68k_fb);
        video_ipc_set_resolution(g_ipc_m68k_width, g_ipc_m68k_height);
        video_ipc_set_depth(bits, mode.bytes_per_row);
        fprintf(stderr,
            "[IPC Video] Mode switch to %dx%dx%d (bpr=%u, fb=%u bytes)\n",
            mode.x, mode.y, bits, (unsigned)mode.bytes_per_row, fb_size);
    }

    void set_palette(uint8 *pal, int num) {
        if (num > 256) num = 256;
        memcpy(pal_, pal, (size_t)num * 3);
        video_ipc_set_palette(pal_, num);
    }

    void set_gamma(uint8 *gamma, int num) { (void)gamma; (void)num; }

private:
    uint8 pal_[256 * 3];
};

static bool video_ipc_m68k_init(bool classic)
{
    (void)classic;

    struct { int w, h; uint32 res_id; } supported_modes[] = {
        {  512,  342, 0x80 },  // Mac SE (1-bit mono)
        {  640,  480, 0x81 },
        {  800,  600, 0x82 },
        { 1024,  768, 0x83 },
        { 1280, 1024, 0x85 },
        { 1600, 1200, 0x86 },
        { 1920, 1080, 0x87 },
    };

    config::EmulatorConfig& cfg = config::EmulatorConfig::instance();
    const int default_width = cfg.screen_width;
    const int default_height = cfg.screen_height;

    // Use machine profile for screen dimensions (e.g. SE: fixed 512x342 mono)
    if (machine_profile().mono_framebuffer) {
        g_ipc_m68k_width = machine_profile().screen_width;
        g_ipc_m68k_height = machine_profile().screen_height;
    } else {
        g_ipc_m68k_width = default_width;
        g_ipc_m68k_height = default_height;
    }

    // Place framebuffer after ScratchMem (same layout as video_webrtc)
    g_ipc_m68k_fb = ROMBaseHost + ROMSize + 0x10000;
    memset(g_ipc_m68k_fb, 0, 0x800000);

    // Tell IPC driver where the framebuffer is
    video_ipc_set_framebuffer(g_ipc_m68k_fb);
    video_ipc_set_resolution(g_ipc_m68k_width, g_ipc_m68k_height);

    // Publish ALL depths for each resolution. InstallSlotROM() reads the
    // set of depths via monitor.has_depth(VDEPTH_*) and writes matching
    // sResource entries into the video card's slot ROM — Mac OS's
    // Monitors cdev reads THAT to populate the depth picker.
    vector<video_mode> modes;
    uint32 default_res_id = 0x83;
    const video_depth depths_to_publish[] = {
        VDEPTH_1BIT, VDEPTH_2BIT, VDEPTH_4BIT,
        VDEPTH_8BIT, VDEPTH_16BIT, VDEPTH_32BIT,
    };

    for (const auto& sm : supported_modes) {
        // 32-bit framebuffer bounds the worst case; lower depths trivially fit.
        if ((uint32_t)sm.w * sm.h * 4 > 0x800000) continue;
        if (sm.w > default_width || sm.h > default_height) continue;

        for (video_depth d : depths_to_publish) {
            video_mode mode;
            mode.x = sm.w;
            mode.y = sm.h;
            mode.resolution_id = sm.res_id;
            mode.depth = d;
            mode.bytes_per_row = TrivialBytesPerRow(sm.w, d);
            mode.user_data = 0;
            modes.push_back(mode);
        }

        if (sm.w == default_width && sm.h == default_height) {
            default_res_id = sm.res_id;
        }
    }

    ipc_monitor_desc *monitor = new ipc_monitor_desc(modes, VDEPTH_32BIT, default_res_id);
    // Seed the IPC refresh side with the initial (32-bit) depth + stride
    // so the first frames render correctly before any explicit mode switch.
    video_ipc_set_depth(32, default_width * 4);

    uint32 mac_fb_addr = Host2MacAddr(g_ipc_m68k_fb);
    if (mac_fb_addr == 0) {
        fprintf(stderr, "[IPC Video] Host2MacAddr returned 0\n");
        delete monitor;
        return false;
    }
    monitor->set_mac_frame_base(mac_fb_addr);
    VideoMonitors.push_back(monitor);

    fprintf(stderr, "[IPC Video] m68k initialized %dx%dx32 (%zu modes), fb at Mac 0x%08x\n",
            default_width, default_height, modes.size(), mac_fb_addr);
    return true;
}

int main(int argc, char **argv)
{
	printf("=== mac-phoenix ===\n\n");

	// Mac address space guards: constructor(101) already reserved 0x08000000-0x70000000
	// in 4MB chunks before any threads existed.

	// Install crash handlers
	install_crash_handlers();

	// Initialize platform with null drivers
	platform_init();

	// Initialize random number generator
	srand(time(nullptr));

	// ========================================
	// Load Unified Configuration
	// ========================================
	const char* home = getenv("HOME");
	std::string default_config_path = home
		? std::string(home) + "/.config/mac-phoenix/config.json"
		: "/tmp/mac-phoenix/config.json";

	config::EmulatorConfig& emu_config = config::EmulatorConfig::instance();
	{
		config::EmulatorConfig loaded = config::load_emulator_config(
			default_config_path.c_str(), argc, argv);
		emu_config = std::move(loaded);
	}

	config::print_config(emu_config);

	command_bridge_init();

	// Select network driver based on config
	if (emu_config.network == config::NetworkMode::Socket) {
		const char *path = emu_config.network_if.empty()
			? "/tmp/mac-ether.sock"
			: emu_config.network_if.c_str();
		ether_socket_set_mitm(
			emu_config.mitm_tls,
			emu_config.mitm_ports.empty() ? nullptr : emu_config.mitm_ports.c_str(),
			emu_config.mitm_ca_dir.empty()  ? nullptr : emu_config.mitm_ca_dir.c_str()
		);
		ether_socket_register(path);
	}

	// Drop a consolidated NetworkInfo.txt (plus MITM CA copies) into
	// <extfs>/MacPhoenix/ so the guest can discover DHCP addresses,
	// bridge/mitm status, build dates, and install the MITM CA without
	// digging through the host filesystem. No-op when ExtFS isn't used.
	core::write_network_info(emu_config);

	// Set global debug/log state from config
	g_debug_mode_switch = emu_config.debug_mode_switch;
	// g_debug_network was in lwip_nat.cpp, now unused (Rust bridge has its own logging)
	set_log_level(emu_config.log_level);
	RAMSize = emu_config.ram_mb * 1024 * 1024;

	// Auto-exit timer
	if (emu_config.timeout_seconds > 0) {
		int timeout_sec = emu_config.timeout_seconds;
		bool ipc_mode = emu_config.ipc_mode;
		printf("Auto-exit timer set: %d seconds\n", timeout_sec);
		std::thread timeout_thread([timeout_sec, ipc_mode]() {
			std::this_thread::sleep_for(std::chrono::seconds(timeout_sec));
			fprintf(stderr, "\n[Timeout: %d seconds elapsed, exiting]\n", timeout_sec);
			if (ipc_mode) {
				exit(0);  // Run atexit handlers to clean up SHM/sockets
			} else {
				_exit(0);
			}
		});
		timeout_thread.detach();
	}

	// ============================================================
	// IPC Child Mode (subprocess, --ipc flag)
	// ============================================================
	if (emu_config.ipc_mode) {
		bool is_ppc = emu_config.is_ppc();
		printf("\n=== IPC Child Mode (%s) ===\n", is_ppc ? "PPC" : "m68k");

		// Ask kernel to send us SIGTERM when parent dies (orphan prevention)
#ifdef __linux__
		prctl(PR_SET_PDEATHSIG, SIGTERM);
		// Check if parent already died between fork() and prctl()
		if (getppid() == 1) {
			fprintf(stderr, "IPC: Parent already dead, exiting\n");
			_exit(0);
		}
#endif
		// Handle SIGTERM/SIGINT: unlink SHM+socket (signal-safe) then _exit.
		// We can't call exit() from a signal handler (deadlock risk if the
		// signal arrives on the control thread which exit() tries to join).
		signal(SIGTERM, [](int) {
			video_ipc_unlink();
			control_ipc_unlink();
			_exit(0);
		});
		signal(SIGINT, [](int) {
			video_ipc_unlink();
			control_ipc_unlink();
			_exit(0);
		});

		const char *rom_path = emu_config.rom_path.empty() ? nullptr : emu_config.rom_path.c_str();
		if (!rom_path) {
			fprintf(stderr, "No ROM file specified for IPC mode\n");
			return 1;
		}

		// Initialize IPC video driver (creates SHM)
		if (!video_ipc_init(emu_config.screen_width, emu_config.screen_height)) {
			fprintf(stderr, "Failed to initialize IPC video\n");
			return 1;
		}

		// Register cleanup so SHM and sockets are unlinked on any exit path
		// (_exit from IPC command handler, timeout thread, or signal)
		atexit([]() {
			control_ipc_exit();
			video_ipc_exit();
		});

		IPCBuffer* ipc_buf = video_ipc_get_buffer();
		if (!ipc_buf) {
			fprintf(stderr, "Failed to get IPC buffer\n");
			return 1;
		}

		// Initialize IPC control socket
		if (!control_ipc_init(ipc_buf)) {
			fprintf(stderr, "Failed to initialize IPC control socket\n");
			video_ipc_exit();
			return 1;
		}
		control_ipc_start();

		// Set boot progress to write to IPC buffer
		boot_progress_set_ipc_buffer(ipc_buf);

		// Install platform drivers
		g_platform.video_exit = []() {};
		if (emu_config.audio_enabled) {
			audio_direct_set_ipc_buffer(ipc_buf);
			g_platform.audio_init = []() {
				audio_direct_init();
			};
			g_platform.audio_exit = []() {
				audio_direct_exit();
			};
		}  // else: platform defaults (audio_null_init/exit) stay in place

		Platform* platform = g_cpu_ctx.get_platform();
		*platform = g_platform;

		if (is_ppc) {
			// PPC: use no-op video_init, refresh copies from screen_base
			g_platform.video_init = [](bool) -> bool { return true; };
			g_platform.video_refresh = video_ipc_refresh;
			*platform = g_platform;  // Push IPC hooks into platform_ before init

			if (!g_cpu_ctx.init_ppc(emu_config)) {
				fprintf(stderr, "Failed to initialize PPC CPU context\n");
				control_ipc_exit();
				video_ipc_exit();
				return 1;
			}
			// Copy KPX CPU hooks back, then restore IPC video/audio hooks
			g_platform = *platform;
			g_platform.video_refresh = video_ipc_refresh;
			if (emu_config.audio_enabled) {
				g_platform.audio_init = []() { audio_direct_init(); };
				g_platform.audio_exit = []() { audio_direct_exit(); };
			}

			// screen_base is 0 here — VideoInit runs during Mac boot.
			// Set the framebuffer pointer from VideoInit instead.
			// (see video_ppc.cpp VideoInit → video_ipc_set_framebuffer)
		} else {
			// m68k: video_init sets up monitor descriptors and framebuffer
			g_platform.video_init = video_ipc_m68k_init;
			g_platform.video_refresh = video_ipc_refresh;
			*platform = g_platform;

			// Install CPU backend
			switch (emu_config.backend) {
				case config::Backend::UnicornM68K:
					cpu_unicorn_install(platform);
					break;
				case config::Backend::DualCPU:
					cpu_dualcpu_install(platform);
					break;
				case config::Backend::UAE:
				default:
					cpu_uae_install(platform);
					break;
			}
			g_platform = *platform;

			RAMSize = emu_config.ram_mb * 1024 * 1024;

			if (!g_cpu_ctx.init_m68k(emu_config)) {
				fprintf(stderr, "Failed to initialize m68k CPU context\n");
				control_ipc_exit();
				video_ipc_exit();
				return 1;
			}
			if (emu_config.browser_enabled) {
				browser::shm_init();
				browser_spike_start(emu_config.browser_initial_url.empty() ? 1 : 0);
				browser::browser_module_start(emu_config.browser_initial_url);
			}
		}

		// Set up signal stack for SIGSEGV handler (SheepShaver legacy pattern)
		// PPC emulation triggers SIGSEGV on ROM write-protect; handler must
		// run on alternate stack to avoid faulting on emulated stack.
		{
			static uint8_t sig_stack_mem[0x10000];  // 64KB signal stack
			stack_t sig_stack;
			sig_stack.ss_sp = sig_stack_mem;
			sig_stack.ss_flags = 0;
			sig_stack.ss_size = sizeof(sig_stack_mem);
			if (sigaltstack(&sig_stack, nullptr) < 0) {
				fprintf(stderr, "WARNING: Could not set signal stack\n");
			}
		}

		// Install SIGSEGV handler
		if (!sigsegv_install_handler(sigsegv_handler)) {
			fprintf(stderr, "WARNING: Could not install SIGSEGV handler\n");
		}

		g_emulator_initialized = true;
		ipc_buf->state = IPC_STATE_RUNNING;

		printf("IPC child ready (PID %d, %s), starting CPU execution...\n",
		       getpid(), is_ppc ? "PPC" : "m68k");

		if (platform->cpu_execute_fast) {
			platform->cpu_execute_fast();
		} else {
			while (true) {
				platform->cpu_execute_one();
			}
		}

		// Cleanup (normally reached via _exit or signal)
		control_ipc_exit();
		video_ipc_exit();
		return 0;
	}

	// ============================================================
	// Webserver Mode (always subprocess+IPC)
	// ============================================================
	if (emu_config.enable_webserver) {
		printf("\n=== WebRTC Mode (Subprocess+IPC) ===\n");

		// Create VideoOutput for parent-side encoder
		auto video_output_owner = std::make_unique<VideoOutput>(1920, 1080);
		video::g_video_output = video_output_owner.get();

		// Create subprocess manager (works for both m68k and PPC)
		auto subprocess_owner = std::make_unique<EmulatorSubprocess>(&emu_config);
		subprocess_owner->set_ipc_shm_atoms(&video::g_ipc_shm, &video::g_ipc_eventfd);
		g_subprocess = subprocess_owner.get();
		g_ipc_client = subprocess_owner->ipc_client();

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
		api_context.subprocess = subprocess_owner.get();

		// Initialize WebRTC signaling server
		printf("Launching HTTP server on port %d...\n", emu_config.http_port);

		webrtc::WebRTCServer webrtc_server;
		if (!webrtc_server.init()) {
			fprintf(stderr, "Failed to initialize WebRTC server\n");
			return 1;
		}
		webrtc::g_server = &webrtc_server;

		// Wire up codec change notification
		api_context.notify_codec_change_fn = [&webrtc_server](CodecType codec) {
			fprintf(stderr, "[API] Codec changed to: %d, notifying WebRTC peers\n", static_cast<int>(codec));
			webrtc_server.notify_codec_change(codec);
		};

		// Bridge watchdog — once Finder is reached (read from child's IPC SHM),
		// warn if BridgeAgent never writes its heartbeat.
		{
			auto* ipc = subprocess_owner->ipc_client();
			command_bridge_start_watchdog([ipc]() {
				const IPCBuffer* buf = ipc ? ipc->shm() : nullptr;
				if (!buf) return false;
				return boot_progress_phase_reached_by_name(buf->boot_phase, "Finder") != 0;
			});
		}

		// Launch HTTP server thread (also hosts /ws signaling)
		std::thread http_server_thread(webserver::http_server_main,
		                                &emu_config, &api_context, &webrtc_server);

		// Launch WebRTC signaling server thread
		std::thread webrtc_server_thread(webrtc::webrtc_server_main,
		                                  &webrtc_server, &webrtc_signaling::g_running);

		// Launch video encoder thread (passes atomic IPC SHM pointer for zero-copy reads)
		video::g_running.store(true, std::memory_order_release);
		std::thread encoder_thread(video::video_encoder_main,
		                            video::g_video_output, &emu_config,
		                            &video::g_ipc_shm, &video::g_ipc_eventfd);

		// Launch audio pipeline (parent side) — only if --audio was passed
		if (emu_config.audio_enabled) {
			audio_webrtc_init();
			if (g_ipc_client) {
				extern AudioOutput* audio_direct_get_output(void);
				AudioOutput* audio_out = audio_direct_get_output();
				if (audio_out) {
					audio_ipc_reader_start(g_ipc_client, audio_out);
				}
			}
		}

		printf("Emulator ready. Open http://localhost:%d in your browser.\n", emu_config.http_port);
		printf("Click 'Start' in the web UI to begin emulation.\n");
		printf("Press Ctrl+C to exit.\n\n");

		// Wait for threads
		http_server_thread.join();
		webrtc_server_thread.join();

		// Shutdown
		if (subprocess_owner) subprocess_owner->stop();
		if (emu_config.audio_enabled) {
			audio_ipc_reader_stop();
			audio::g_running.store(false, std::memory_order_release);
			audio_webrtc_exit();
		}
		video::g_running.store(false, std::memory_order_release);
		video::g_video_output->shutdown();
		encoder_thread.join();

		g_subprocess = nullptr;
		g_ipc_client = nullptr;
		video_output_owner.reset();
		video::g_video_output = nullptr;

	} else {
		// ============================================================
		// Headless Mode (Direct CPU Execution, No Subprocess)
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

			if (emu_config.audio_enabled) {
				g_platform.audio_init = []() { audio_direct_init(); };
				g_platform.audio_exit = []() { audio_direct_exit(); };
			}  // else: platform defaults (audio_null_init/exit) stay in place

			// Copy platform into CPUContext
			Platform* platform = g_cpu_ctx.get_platform();
			*platform = g_platform;

			// Initialize based on architecture
			if (emu_config.is_ppc()) {
				if (!g_cpu_ctx.init_ppc(emu_config)) {
					fprintf(stderr, "Failed to initialize PPC CPU context\n");
					return 1;
				}
				g_platform = *platform;
			} else {
				switch (emu_config.backend) {
					case config::Backend::UnicornM68K:
						cpu_unicorn_install(platform);
						break;
					case config::Backend::DualCPU:
						cpu_dualcpu_install(platform);
						break;
					case config::Backend::UAE:
					default:
						cpu_uae_install(platform);
						break;
				}

				g_platform = *platform;

				if (!g_cpu_ctx.init_m68k(emu_config)) {
					fprintf(stderr, "Failed to initialize M68K CPU context\n");
					return 1;
				}
				if (emu_config.browser_enabled) {
					browser::shm_init();
					browser_spike_start(emu_config.browser_initial_url.empty() ? 1 : 0);
					browser::browser_module_start(emu_config.browser_initial_url);
				}
			}

			// Set up signal stack for SIGSEGV handler (SheepShaver legacy pattern)
			if (emu_config.is_ppc()) {
				static uint8_t sig_stack_mem[0x10000];
				stack_t sig_stack;
				sig_stack.ss_sp = sig_stack_mem;
				sig_stack.ss_flags = 0;
				sig_stack.ss_size = sizeof(sig_stack_mem);
				if (sigaltstack(&sig_stack, nullptr) < 0) {
					fprintf(stderr, "WARNING: Could not set signal stack\n");
				}
			}

			// Install SIGSEGV handler
			if (!sigsegv_install_handler(sigsegv_handler)) {
				fprintf(stderr, "WARNING: Could not install SIGSEGV handler\n");
			}

			g_emulator_initialized = true;

			printf("\n=== CPU Execution Mode (Headless) ===\n");

			// Optional: start HTTP API server in headless mode.
			// Enabled when a port is explicitly requested alongside --no-webserver.
			// In this mode, command bridge runs in the same process so launch/quit work.
			std::thread headless_http_thread;
			std::unique_ptr<http::APIContext> headless_api_context;
			if (emu_config.http_port > 0 && emu_config.headless_http) {
				printf("Starting HTTP API server on port %d (headless mode)...\n", emu_config.http_port);
				headless_api_context = std::make_unique<http::APIContext>();
				headless_api_context->config = &emu_config;
				headless_api_context->subprocess = nullptr;  // in-process command bridge
				headless_http_thread = std::thread(webserver::http_server_main,
				                                    &emu_config, headless_api_context.get(),
				                                    /*webrtc_server=*/nullptr);
			}

			printf("Starting CPU execution...\n");
			printf("Press Ctrl+C to exit.\n\n");

			// Bridge watchdog — same role as in webserver mode, but reads
			// boot phase from in-process boot_progress (no IPC SHM).
			command_bridge_start_watchdog([]() {
				return boot_progress_phase_reached("Finder") != 0;
			});

			if (platform->cpu_execute_fast) {
				platform->cpu_execute_fast();
			} else {
				while (true) {
					platform->cpu_execute_one();
				}
			}

			if (headless_http_thread.joinable()) {
				webserver::g_running.store(false, std::memory_order_release);
				headless_http_thread.join();
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
