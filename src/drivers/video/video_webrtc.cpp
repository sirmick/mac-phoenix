/*
 *  video_webrtc.cpp - WebRTC video driver
 *
 *  Manages WebRTC video streaming:
 *  - Creates VideoOutput triple buffer
 *  - Launches video encoder thread
 *  - Provides video_refresh() to capture frames
 */

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "video.h"
#include "video_defs.h"
#include "platform.h"
#include "video_output.h"
#include "video_encoder_thread.h"
#include "../../config/emulator_config.h"
#include <thread>
#include <atomic>

#define DEBUG 0
#include "debug.h"

// Video encoder globals (defined in main.cpp)
namespace video {
	extern std::atomic<bool> g_running;
	extern std::atomic<bool> g_request_keyframe;
}

// VideoOutput accessible externally for screenshot API
namespace video {
	VideoOutput* g_video_output = nullptr;
}

// WebRTC video state (internal)
namespace {
	std::thread* g_encoder_thread = nullptr;
	config::EmulatorConfig* g_config = nullptr;

	// Dummy framebuffer for Mac
	uint8_t* the_buffer = nullptr;
	uint32_t the_buffer_size = 0;
}

// WebRTC monitor descriptor — supports runtime resolution switching
class webrtc_monitor_desc : public monitor_desc {
public:
	webrtc_monitor_desc(const vector<video_mode> &available_modes, video_depth default_depth, uint32 default_id)
		: monitor_desc(available_modes, default_depth, default_id) {}
	~webrtc_monitor_desc() {}

	void switch_to_current_mode(void) {
		const video_mode &mode = get_current_mode();
		uint32_t new_size = mode.x * mode.y * 4;

		// Clear framebuffer for new resolution
		if (the_buffer && new_size <= 0x800000) {
			memset(the_buffer, 0, new_size);
			the_buffer_size = new_size;
			fprintf(stderr, "[Video] Mode switch to %dx%dx32\n", mode.x, mode.y);

			// Request keyframe so encoder picks up the new resolution cleanly
			video::g_request_keyframe.store(true, std::memory_order_release);
		}
	}
	void set_palette(uint8 *pal, int num) { (void)pal; (void)num; }
	void set_gamma(uint8 *gamma, int num) { (void)gamma; (void)num; }
};

/*
 *  Initialization
 */
bool video_webrtc_init(bool classic, config::EmulatorConfig* config)
{
	D(bug("Video: WebRTC driver initializing\n"));

	// Store config for encoder thread
	g_config = config;

	// Create VideoOutput triple buffer (1080p max)
	video::g_video_output = new VideoOutput(1920, 1080);

	// Supported resolutions (resolution_id follows legacy BasiliskII convention)
	struct { int w, h; uint32 res_id; } supported_modes[] = {
		{  640,  480, 0x81 },
		{  800,  600, 0x82 },
		{ 1024,  768, 0x83 },
		{ 1280, 1024, 0x85 },
		{ 1600, 1200, 0x86 },
		{ 1920, 1080, 0x87 },
	};

	// Default resolution from config — also used as max mode limit
	const int default_width = config ? config->screen_width : 1024;
	const int default_height = config ? config->screen_height : 768;
	const video_depth depth = VDEPTH_32BIT;
	fprintf(stderr, "[Video] Max resolution from config: %dx%d\n", default_width, default_height);

	// Allocate framebuffer at max supported size (8MB area in cpu_context.cpp)
	// All modes share the same buffer — only the used portion changes
	the_buffer_size = 0x800000;  // 8MB max

	// Get memory layout info from globals
	extern uint8 *ROMBaseHost;
	extern uint32 ROMSize;

	// Place framebuffer AFTER ScratchMem (outside RAM) to avoid overlapping Mac heap
	// Memory layout: [RAM][ROM 1MB][ScratchMem 64KB][FrameBuffer 8MB]
	the_buffer = ROMBaseHost + ROMSize + 0x10000;  // After ScratchMem
	memset(the_buffer, 0, the_buffer_size);

	D(bug("Video: Framebuffer at host addr %p, Mac addr 0x%08x\n",
	      the_buffer, Host2MacAddr(the_buffer)));

	// Build list of supported video modes (32-bit only)
	vector<video_mode> modes;
	uint32 default_res_id = 0x83;  // Default to 1024x768 if config resolution not in list

	for (const auto& sm : supported_modes) {
		// Only include modes that fit in framebuffer area
		if ((uint32_t)sm.w * sm.h * 4 > 0x800000) continue;

		// --screen limits maximum resolution (prevents Mac from mode-switching up)
		if (sm.w > (int)default_width || sm.h > (int)default_height) continue;

		video_mode mode;
		mode.x = sm.w;
		mode.y = sm.h;
		mode.resolution_id = sm.res_id;
		mode.depth = depth;
		mode.bytes_per_row = sm.w * 4;
		mode.user_data = 0;
		modes.push_back(mode);

		if (sm.w == default_width && sm.h == default_height) {
			default_res_id = sm.res_id;
		}
	}

	// Create monitor descriptor with default set to config resolution
	webrtc_monitor_desc *monitor = new webrtc_monitor_desc(modes, depth, default_res_id);

	// Set Mac frame buffer address (now it's in Mac RAM!)
	uint32 mac_fb_addr = Host2MacAddr(the_buffer);
	if (mac_fb_addr == 0) {
		fprintf(stderr, "Video: FATAL - Host2MacAddr returned 0 for buffer at %p\n", the_buffer);
		fprintf(stderr, "       RAMBaseHost=%p, RAMSize=0x%08x\n", RAMBaseHost, RAMSize);
		delete video::g_video_output;
		delete monitor;
		video::g_video_output = nullptr;
		return false;
	}
	monitor->set_mac_frame_base(mac_fb_addr);
	D(bug("Video: Mac framebuffer address set to 0x%08x\n", mac_fb_addr));

	// Add to global monitor list
	VideoMonitors.push_back(monitor);

	// Launch video encoder thread
	video::g_running.store(true, std::memory_order_release);
	g_encoder_thread = new std::thread(video::video_encoder_main, video::g_video_output, g_config);

	D(bug("Video: WebRTC driver initialized (%dx%dx32, %zu modes, encoder thread started)\n",
	      default_width, default_height, modes.size()));
	return true;
}

/*
 *  Deinitialization
 */
void video_webrtc_exit(void)
{
	D(bug("Video: WebRTC driver shutting down\n"));

	// Stop encoder thread
	if (g_encoder_thread) {
		video::g_running.store(false, std::memory_order_release);
		g_encoder_thread->join();
		delete g_encoder_thread;
		g_encoder_thread = nullptr;
	}

	// Delete monitor descriptors
	vector<monitor_desc *>::iterator i, end = VideoMonitors.end();
	for (i = VideoMonitors.begin(); i != end; ++i)
		delete *i;
	VideoMonitors.clear();

	// Note: the_buffer is now part of Mac RAM, not malloc'd
	// So we don't free() it - it will be freed when RAM is freed
	the_buffer = nullptr;

	// Delete VideoOutput
	if (video::g_video_output) {
		delete video::g_video_output;
		video::g_video_output = nullptr;
	}

	D(bug("Video: WebRTC driver shutdown complete\n"));
}

/*
 *  Video refresh - called periodically to capture frames
 *
 *  Called from main emulation loop to submit frames to the encoder.
 *  Mac framebuffer is in ARGB format, we convert to BGRA for H.264 encoder.
 */
void video_webrtc_refresh(void)
{
	static bool debug_frames = (getenv("MACEMU_DEBUG_FRAMES") != nullptr);
	static int refresh_count = 0;

	if (!video::g_video_output || !the_buffer) {
		if (debug_frames && refresh_count == 0) {
			fprintf(stderr, "[VideoRefresh] ERROR: g_video_output=%p the_buffer=%p\n",
			        (void*)video::g_video_output, (void*)the_buffer);
		}
		return;
	}

	refresh_count++;

	// Debug: Log first 5 calls and every 60 calls thereafter
	if (refresh_count <= 5 || (debug_frames && (refresh_count % 60 == 0))) {
		// Sample a few pixels to see what's in the framebuffer
		const uint32_t* pixels = reinterpret_cast<const uint32_t*>(the_buffer);
		uint32_t p0 = pixels[0];          // Top-left corner
		int w = VideoMonitors.empty() ? 1024 : VideoMonitors[0]->get_current_mode().x;
		int h = VideoMonitors.empty() ? 768 : VideoMonitors[0]->get_current_mode().y;
		uint32_t p1 = pixels[w - 1];       // Top-right corner
		uint32_t p2 = pixels[w/2 + (h/2) * w]; // Center
		fprintf(stderr, "[VideoRefresh] Frame %d: pixels[0]=0x%08x [1023]=0x%08x [center]=0x%08x\n",
		        refresh_count, p0, p1, p2);
	}

	// Get current video mode dimensions from monitor descriptor
	const int width = VideoMonitors.empty() ? 1024 : VideoMonitors[0]->get_current_mode().x;
	const int height = VideoMonitors.empty() ? 768 : VideoMonitors[0]->get_current_mode().y;

	// Mac framebuffer is already in ARGB format (32-bit)
	// VideoOutput wants ARGB or BGRA - let's submit as ARGB since that's what Mac uses
	const uint32_t* pixels = reinterpret_cast<const uint32_t*>(the_buffer);

	// Submit frame to encoder (non-blocking, lock-free)
	video::g_video_output->submit_frame(pixels, width, height, PIXFMT_ARGB);
}
