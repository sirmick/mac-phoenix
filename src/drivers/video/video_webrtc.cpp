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
#include "../../common/include/video_modes.h"
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

// Classic Mac CLUT defaults. Index 0 is white and index 255 is black on
// the Mac (inverted from the usual GDI convention). Intermediate indices
// start black — Mac OS will populate them via SetEntries during boot.
static void init_default_palette(uint8 *pal)
{
	memset(pal, 0, 256 * 3);
	pal[0] = pal[1] = pal[2] = 255;  // white
	// 255 entries remain black (0,0,0)
}

// WebRTC monitor descriptor — supports runtime resolution + depth switching.
class webrtc_monitor_desc : public monitor_desc {
public:
	webrtc_monitor_desc(const vector<video_mode> &available_modes, video_depth default_depth, uint32 default_id)
		: monitor_desc(available_modes, default_depth, default_id)
	{
		init_default_palette(pal);
	}
	~webrtc_monitor_desc() {}

	void switch_to_current_mode(void) {
		const video_mode &mode = get_current_mode();
		uint32_t new_size = (uint32_t)mode.bytes_per_row * mode.y;

		if (the_buffer && new_size <= 0x800000) {
			memset(the_buffer, 0, new_size);
			the_buffer_size = new_size;
			fprintf(stderr,
				"[Video] Mode switch to %dx%dx%d (bpr=%u, fb=%u bytes)\n",
				mode.x, mode.y, mode_depth_bits(mode.depth),
				(unsigned)mode.bytes_per_row, new_size);

			// Request keyframe so encoder picks up the new shape cleanly.
			video::g_request_keyframe.store(true, std::memory_order_release);
		}
	}

	void set_palette(uint8 *src_pal, int num) {
		if (num > 256) num = 256;
		memcpy(pal, src_pal, num * 3);
	}

	void set_gamma(uint8 *gamma, int num) { (void)gamma; (void)num; }

	// Report bit depth as a number (1/2/4/8/16/32) for log prefixes etc.
	static int mode_depth_bits(video_depth d) {
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

	uint8 pal[256 * 3];  // RGB CLUT for indexed modes (1/2/4/8-bit)
};

/*
 *  Initialization
 */
bool video_webrtc_init(bool /*classic*/, config::EmulatorConfig* config)
{
	D(bug("Video: WebRTC driver initializing\n"));

	// Store config for encoder thread
	g_config = config;

	// Create VideoOutput triple buffer (1080p max)
	video::g_video_output = new VideoOutput(mp::video::kMaxWidth_M68k,
	                                        mp::video::kMaxHeight_M68k);

	// Default resolution from config — selects the boot mode only; does
	// NOT cap the rest of the published list (drove a "why isn't 1280x1024
	// in the picker?" surprise pre-refactor).
	const int cfg_width = config ? config->screen_width : 1024;
	const int cfg_height = config ? config->screen_height : 768;
	const video_depth default_depth = VDEPTH_32BIT;
	fprintf(stderr, "[Video] Default mode from config: %dx%d\n", cfg_width, cfg_height);

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

	// Walk the shared video-mode table (filtered to m68k-capable). Every
	// listed resolution gets fanned out to every depth so the Monitors cdev
	// can offer the full 1/2/4/8/16/32-bit picker.
	vector<video_mode> modes;
	uint32 default_res_id = 0;
	const video_depth depths_to_publish[] = {
		VDEPTH_1BIT, VDEPTH_2BIT, VDEPTH_4BIT,
		VDEPTH_8BIT, VDEPTH_16BIT, VDEPTH_32BIT,
	};

	for (std::size_t i = 0; i < mp::video::kModeCount; ++i) {
		const auto& md = mp::video::kModes[i];
		if (!(md.flags & mp::video::kFlagM68k)) continue;
		// Memory budget — belt-and-braces in case the table grows past
		// our 8 MB framebuffer arena.
		if ((uint32_t)md.w * md.h * 4 > 0x800000) continue;

		for (video_depth d : depths_to_publish) {
			video_mode mode;
			mode.x = md.w;
			mode.y = md.h;
			mode.resolution_id = md.apple_id;
			mode.depth = d;
			mode.bytes_per_row = TrivialBytesPerRow(md.w, d);
			mode.user_data = 0;
			modes.push_back(mode);
		}

		if (md.w == cfg_width && md.h == cfg_height) {
			default_res_id = md.apple_id;
		}
	}
	if (default_res_id == 0 && !modes.empty()) {
		default_res_id = modes.front().resolution_id;
	}

	// Create monitor descriptor; default to 32-bit at the configured resolution.
	webrtc_monitor_desc *monitor = new webrtc_monitor_desc(modes, default_depth, default_res_id);

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
	g_encoder_thread = new std::thread(video::video_encoder_main, video::g_video_output, g_config,
	                                   (std::atomic<IPCBuffer*>*)nullptr, (std::atomic<int>*)nullptr);

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

// Intermediate ARGB buffer — used only when the Mac is in a non-32-bit
// mode and we need to unpack/palette-lookup before handing frames to
// the encoder. Sized to the max resolution we'll ever serve.
namespace {
	std::vector<uint32_t> g_convert_buf;

	// Ensure g_convert_buf can hold width*height ARGB pixels.
	uint32_t *ensure_convert_buf(int width, int height) {
		size_t need = (size_t)width * (size_t)height;
		if (g_convert_buf.size() < need) {
			g_convert_buf.resize(need);
		}
		return g_convert_buf.data();
	}

	// Pack R,G,B into a uint32 whose in-memory bytes are A,R,G,B on
	// little-endian hosts. See video_ipc_ppc.cpp's pack_argb for the
	// full explanation of why we DON'T use A<<24 here.
	inline uint32_t pack_argb(uint8_t r, uint8_t g, uint8_t b) {
		return (uint32_t)0xff
		     | ((uint32_t)r << 8)
		     | ((uint32_t)g << 16)
		     | ((uint32_t)b << 24);
	}

	// Expand a 5-bit color component to 8 bits using bit replication —
	// avoids skewing dark colors the way `(c << 3)` alone would.
	inline uint8_t expand5(uint8_t c) { return (c << 3) | (c >> 2); }

	// Convert one row of the Mac framebuffer at `depth` into ARGB.
	// `src` points at the row start (stride = bytes_per_row). `pal` is the
	// active CLUT (256 * 3 bytes, R,G,B).
	void convert_row_to_argb(const uint8_t *src, int width, video_depth depth,
	                         const uint8_t *pal, uint32_t *dst)
	{
		switch (depth) {
			case VDEPTH_1BIT: {
				// 8 pixels per byte, MSB first. bit 1 = index 1, 0 = index 0.
				for (int x = 0; x < width; x++) {
					uint8_t bit = (src[x >> 3] >> (7 - (x & 7))) & 1;
					const uint8_t *p = &pal[bit * 3];
					dst[x] = pack_argb(p[0], p[1], p[2]);
				}
				break;
			}
			case VDEPTH_2BIT: {
				// 4 pixels per byte, MSB pair first.
				for (int x = 0; x < width; x++) {
					uint8_t idx = (src[x >> 2] >> ((3 - (x & 3)) * 2)) & 0x3;
					const uint8_t *p = &pal[idx * 3];
					dst[x] = pack_argb(p[0], p[1], p[2]);
				}
				break;
			}
			case VDEPTH_4BIT: {
				// 2 pixels per byte, high nibble first.
				for (int x = 0; x < width; x++) {
					uint8_t idx = (src[x >> 1] >> ((1 - (x & 1)) * 4)) & 0xf;
					const uint8_t *p = &pal[idx * 3];
					dst[x] = pack_argb(p[0], p[1], p[2]);
				}
				break;
			}
			case VDEPTH_8BIT: {
				for (int x = 0; x < width; x++) {
					const uint8_t *p = &pal[src[x] * 3];
					dst[x] = pack_argb(p[0], p[1], p[2]);
				}
				break;
			}
			case VDEPTH_16BIT: {
				// Mac 16-bit is 0RRRRRGG GGGBBBBB (big-endian uint16).
				for (int x = 0; x < width; x++) {
					uint16_t pix = ((uint16_t)src[x * 2] << 8) | src[x * 2 + 1];
					uint8_t r = expand5((pix >> 10) & 0x1f);
					uint8_t g = expand5((pix >> 5)  & 0x1f);
					uint8_t b = expand5(pix & 0x1f);
					dst[x] = pack_argb(r, g, b);
				}
				break;
			}
			case VDEPTH_32BIT:
			default:
				// Already ARGB — caller fast-paths this case without us.
				memcpy(dst, src, (size_t)width * 4);
				break;
		}
	}
}

/*
 *  Video refresh - called periodically to capture frames
 *
 *  Reads the Mac framebuffer at whatever depth the guest currently has
 *  selected and submits an ARGB frame to the encoder. For 32-bit, this
 *  is a pass-through; for 1/2/4/8-bit we palette-lookup through the
 *  CLUT captured via set_palette(); for 16-bit we unpack RGB555.
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
	if (VideoMonitors.empty()) return;

	refresh_count++;

	webrtc_monitor_desc *monitor = static_cast<webrtc_monitor_desc *>(VideoMonitors[0]);
	const video_mode &mode = monitor->get_current_mode();
	const int width = mode.x;
	const int height = mode.y;
	const int bpr = mode.bytes_per_row;
	const video_depth depth = mode.depth;

	// Fast path: 32-bit is already ARGB, submit directly.
	if (depth == VDEPTH_32BIT) {
		if (refresh_count <= 5 || (debug_frames && (refresh_count % 60 == 0))) {
			const uint32_t* pixels = reinterpret_cast<const uint32_t*>(the_buffer);
			fprintf(stderr,
				"[VideoRefresh] Frame %d (32bit %dx%d): p[0]=0x%08x center=0x%08x\n",
				refresh_count, width, height,
				pixels[0], pixels[width/2 + (height/2) * width]);
		}
		const uint32_t* pixels = reinterpret_cast<const uint32_t*>(the_buffer);
		video::g_video_output->submit_frame(pixels, width, height, PIXFMT_ARGB);
		return;
	}

	// Indexed / 16-bit: unpack into an ARGB scratch buffer, one row at
	// a time so we walk `src` with the real bytes_per_row stride.
	uint32_t *argb = ensure_convert_buf(width, height);
	const uint8_t *src = reinterpret_cast<const uint8_t *>(the_buffer);
	for (int y = 0; y < height; y++) {
		convert_row_to_argb(src + y * bpr, width, depth, monitor->pal,
		                    argb + y * width);
	}

	if (refresh_count <= 5 || (debug_frames && (refresh_count % 60 == 0))) {
		fprintf(stderr,
			"[VideoRefresh] Frame %d (%dbit %dx%d bpr=%d) converted to ARGB\n",
			refresh_count, webrtc_monitor_desc::mode_depth_bits(depth),
			width, height, bpr);
	}

	video::g_video_output->submit_frame(argb, width, height, PIXFMT_ARGB);
}
