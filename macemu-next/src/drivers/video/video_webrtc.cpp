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
#include "../../config/config_manager.h"
#include <thread>
#include <atomic>

#define DEBUG 0
#include "debug.h"

// Video encoder globals (defined in main.cpp)
namespace video {
	extern std::atomic<bool> g_running;
	extern std::atomic<bool> g_request_keyframe;
}

// WebRTC video state
namespace {
	VideoOutput* g_video_output = nullptr;
	std::thread* g_encoder_thread = nullptr;
	config::MacemuConfig* g_config = nullptr;

	// Dummy framebuffer for Mac
	uint8_t* the_buffer = nullptr;
	uint32_t the_buffer_size = 0;
}

// Dummy monitor descriptor (similar to video_null)
class webrtc_monitor_desc : public monitor_desc {
public:
	webrtc_monitor_desc(const vector<video_mode> &available_modes, video_depth default_depth, uint32 default_id)
		: monitor_desc(available_modes, default_depth, default_id) {}
	~webrtc_monitor_desc() {}

	void switch_to_current_mode(void) { /* Streaming - no mode switch needed */ }
	void set_palette(uint8 *pal, int num) { /* Streaming - palette changes handled in refresh */ }
	void set_gamma(uint8 *gamma, int num) { /* Streaming - gamma ignored */ }
};

/*
 *  Initialization
 */
bool video_webrtc_init(bool classic, config::MacemuConfig* config)
{
	D(bug("Video: WebRTC driver initializing\n"));

	// Store config for encoder thread
	g_config = config;

	// Create VideoOutput triple buffer (1080p max)
	g_video_output = new VideoOutput(1920, 1080);

	// Create a dummy framebuffer for Mac (1024x768x32 ARGB)
	const int width = 1024;
	const int height = 768;
	const video_depth depth = VDEPTH_32BIT;
	const uint32 resolution_id = 0x80;

	// Allocate framebuffer from Mac RAM (at the end of RAM)
	// Need to allocate BEFORE initializing to get correct address
	the_buffer_size = width * height * 4;  // 32-bit = 4 bytes per pixel

	// Get RAM info from globals
	extern uint8 *RAMBaseHost;
	extern uint32 RAMSize;

	// Allocate framebuffer at end of Mac RAM minus buffer size
	// IMPORTANT: Framebuffer MUST be in Mac RAM for Host2MacAddr to work!
	if (the_buffer_size > RAMSize / 2) {
		fprintf(stderr, "Video: Framebuffer too large (%u bytes) for RAM size (%u bytes)\n",
		        the_buffer_size, RAMSize);
		delete g_video_output;
		g_video_output = nullptr;
		return false;
	}

	// Place framebuffer at top of RAM
	the_buffer = RAMBaseHost + RAMSize - the_buffer_size;
	memset(the_buffer, 0, the_buffer_size);

	D(bug("Video: Framebuffer at host addr %p, Mac addr 0x%08x\n",
	      the_buffer, Host2MacAddr(the_buffer)));

	// Build list of supported video modes
	vector<video_mode> modes;
	video_mode mode;
	mode.x = width;
	mode.y = height;
	mode.resolution_id = resolution_id;
	mode.depth = depth;
	mode.bytes_per_row = width * 4;  // 32-bit depth = 4 bytes per pixel
	mode.user_data = 0;
	modes.push_back(mode);

	// Create monitor descriptor
	webrtc_monitor_desc *monitor = new webrtc_monitor_desc(modes, depth, resolution_id);

	// Set Mac frame buffer address (now it's in Mac RAM!)
	uint32 mac_fb_addr = Host2MacAddr(the_buffer);
	if (mac_fb_addr == 0) {
		fprintf(stderr, "Video: FATAL - Host2MacAddr returned 0 for buffer at %p\n", the_buffer);
		fprintf(stderr, "       RAMBaseHost=%p, RAMSize=0x%08x\n", RAMBaseHost, RAMSize);
		delete g_video_output;
		delete monitor;
		g_video_output = nullptr;
		return false;
	}
	monitor->set_mac_frame_base(mac_fb_addr);
	D(bug("Video: Mac framebuffer address set to 0x%08x\n", mac_fb_addr));

	// Add to global monitor list
	VideoMonitors.push_back(monitor);

	// Launch video encoder thread
	video::g_running.store(true, std::memory_order_release);
	g_encoder_thread = new std::thread(video::video_encoder_main, g_video_output, g_config);

	D(bug("Video: WebRTC driver initialized (1024x768x32, encoder thread started)\n"));
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
	if (g_video_output) {
		delete g_video_output;
		g_video_output = nullptr;
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

	if (!g_video_output || !the_buffer) {
		if (debug_frames && refresh_count == 0) {
			fprintf(stderr, "[VideoRefresh] ERROR: g_video_output=%p the_buffer=%p\n",
			        (void*)g_video_output, (void*)the_buffer);
		}
		return;
	}

	refresh_count++;

	// Debug: Log first 5 calls and every 60 calls thereafter
	if (refresh_count <= 5 || (debug_frames && (refresh_count % 60 == 0))) {
		fprintf(stderr, "[VideoRefresh] Called %d times, submitting frame\n", refresh_count);
	}

	// Get current video mode dimensions
	// Note: For now we're hardcoded to 1024x768x32
	const int width = 1024;
	const int height = 768;

	// Mac framebuffer is already in ARGB format (32-bit)
	// VideoOutput wants ARGB or BGRA - let's submit as ARGB since that's what Mac uses
	const uint32_t* pixels = reinterpret_cast<const uint32_t*>(the_buffer);

	// Submit frame to encoder (non-blocking, lock-free)
	g_video_output->submit_frame(pixels, width, height, PIXFMT_ARGB);
}
