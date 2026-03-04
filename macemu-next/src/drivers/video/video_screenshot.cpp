/*
 *  video_screenshot.cpp - Screenshot video driver
 *
 *  Simple video driver that captures PPM screenshots to /tmp/ for debugging.
 *  640x480x8 framebuffer (same as null driver) with periodic screenshot capture.
 *
 *  Enable with: MACEMU_SCREENSHOTS=1
 *  Output: /tmp/macemu_screen_NNN.ppm (every 5 seconds)
 *
 *  For 8-bit mode, applies the Mac CLUT (color lookup table) to produce
 *  correct RGB output. The palette is captured from Mac OS SetEntries calls.
 */

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "video.h"
#include "video_defs.h"
#include "platform.h"

#include <stdio.h>
#include <string.h>

#define DEBUG 0
#include "debug.h"

// External RAM pointers (defined in basilisk_glue.cpp)
extern uint8 *RAMBaseHost;
extern uint32 RAMSize;

// Framebuffer
static uint8 *the_buffer = NULL;
static uint32 the_buffer_size = 0;

// Screenshot state
static int screenshot_counter = 0;
static int refresh_counter = 0;

// Capture interval: every N refresh calls (refresh is at 60Hz)
// 300 = every 5 seconds
#define SCREENSHOT_INTERVAL 300

// Monitor descriptor with palette capture
class screenshot_monitor_desc : public monitor_desc {
public:
	screenshot_monitor_desc(const vector<video_mode> &available_modes, video_depth default_depth, uint32 default_id)
		: monitor_desc(available_modes, default_depth, default_id) {
		// Initialize palette to Mac default (index 0=white, 255=black)
		memset(pal, 0, sizeof(pal));
		pal[0] = pal[1] = pal[2] = 255;  // White
		// All other indices start as black; Mac OS will set the real palette
	}
	~screenshot_monitor_desc() {}

	void switch_to_current_mode(void) {}
	void set_palette(uint8 *src_pal, int num) {
		if (num > 256) num = 256;
		memcpy(pal, src_pal, num * 3);
	}
	void set_gamma(uint8 *gamma, int num) { (void)gamma; (void)num; }

	uint8 pal[256 * 3];  // RGB palette for screenshot conversion
};


/*
 *  Save framebuffer as PPM image
 */
static void save_screenshot(void)
{
	if (!the_buffer || VideoMonitors.empty()) return;

	screenshot_monitor_desc *monitor = static_cast<screenshot_monitor_desc *>(VideoMonitors[0]);
	const video_mode &mode = monitor->get_current_mode();
	int width = mode.x;
	int height = mode.y;
	int bpr = mode.bytes_per_row;

	uint32 fb_mac_addr = monitor->get_mac_frame_base();

	char filename[256];
	snprintf(filename, sizeof(filename), "/tmp/macemu_screen_%03d.ppm", screenshot_counter);

	FILE *f = fopen(filename, "wb");
	if (!f) {
		fprintf(stderr, "Screenshot: Failed to open %s\n", filename);
		return;
	}

	// PPM header
	fprintf(f, "P6\n%d %d\n255\n", width, height);

	// Convert indexed pixels to RGB using palette
	uint8 *pal = monitor->pal;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			uint8 pixel = ReadMacInt8(fb_mac_addr + y * bpr + x);
			fputc(pal[pixel * 3 + 0], f);  // R
			fputc(pal[pixel * 3 + 1], f);  // G
			fputc(pal[pixel * 3 + 2], f);  // B
		}
	}

	fclose(f);

	// Diagnostics for first few screenshots
	if (screenshot_counter < 3) {
		fprintf(stderr, "Screenshot: %s (%dx%d fb=0x%08x)\n", filename, width, height, fb_mac_addr);
		fprintf(stderr, "  Palette: 0=(%d,%d,%d) 1=(%d,%d,%d) 255=(%d,%d,%d)\n",
		        pal[0], pal[1], pal[2], pal[3], pal[4], pal[5],
		        pal[255*3], pal[255*3+1], pal[255*3+2]);

		// Pixel histogram (top 5 values)
		uint32 hist[256] = {0};
		for (int y = 0; y < height; y++)
			for (int x = 0; x < width; x++)
				hist[ReadMacInt8(fb_mac_addr + y * bpr + x)]++;

		fprintf(stderr, "  Top pixels: ");
		for (int pass = 0; pass < 5; pass++) {
			int best = 0;
			for (int i = 1; i < 256; i++)
				if (hist[i] > hist[best]) best = i;
			if (hist[best] == 0) break;
			fprintf(stderr, "idx=%d(n=%u rgb=%d/%d/%d) ",
			        best, hist[best], pal[best*3], pal[best*3+1], pal[best*3+2]);
			hist[best] = 0;
		}
		fprintf(stderr, "\n");
	} else {
		fprintf(stderr, "Screenshot: %s\n", filename);
	}

	screenshot_counter++;
}


/*
 *  Initialization
 */
bool video_screenshot_init(bool classic)
{
	(void)classic;

	const int width = 640;
	const int height = 480;
	const video_depth depth = VDEPTH_8BIT;
	const uint32 resolution_id = 0x80;

	the_buffer_size = width * height;

	if (the_buffer_size > RAMSize / 2) {
		fprintf(stderr, "Screenshot: Framebuffer too large for RAM\n");
		return false;
	}

	the_buffer = RAMBaseHost + RAMSize - the_buffer_size;
	memset(the_buffer, 0, the_buffer_size);

	vector<video_mode> modes;
	video_mode mode;
	mode.x = width;
	mode.y = height;
	mode.resolution_id = resolution_id;
	mode.depth = depth;
	mode.bytes_per_row = width;
	mode.user_data = 0;
	modes.push_back(mode);

	screenshot_monitor_desc *monitor = new screenshot_monitor_desc(modes, depth, resolution_id);
	monitor->set_mac_frame_base(Host2MacAddr(the_buffer));
	VideoMonitors.push_back(monitor);

	screenshot_counter = 0;
	refresh_counter = 0;

	fprintf(stderr, "Screenshot: 640x480x8 driver initialized (output: /tmp/macemu_screen_*.ppm)\n");
	return true;
}


/*
 *  Deinitialization
 */
void video_screenshot_exit(void)
{
	// Take final screenshot
	if (the_buffer && !VideoMonitors.empty()) {
		save_screenshot();
		fprintf(stderr, "Screenshot: Final screenshot saved (%d total)\n", screenshot_counter);
	}

	vector<monitor_desc *>::iterator i, end = VideoMonitors.end();
	for (i = VideoMonitors.begin(); i != end; ++i)
		delete *i;
	VideoMonitors.clear();
	the_buffer = NULL;
}


/*
 *  Video refresh - called at 60Hz
 */
void video_screenshot_refresh(void)
{
	if (++refresh_counter >= SCREENSHOT_INTERVAL) {
		refresh_counter = 0;
		save_screenshot();
	}
}
