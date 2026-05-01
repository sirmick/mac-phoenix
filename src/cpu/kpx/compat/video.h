/*
 *  video.h - PPC video/graphics emulation (SheepShaver native driver)
 *
 *  SheepShaver (C) 1997-2008 Marc Hellwig and Christian Bauer
 *  Licensed under GPL v2+
 */

#ifndef KPX_VIDEO_H
#define KPX_VIDEO_H
#ifdef _COMMON_VIDEO_H
#error "KPX compat/video.h conflicts with common/include/video.h — both included in same TU"
#endif
#define _KPX_VIDEO_H

// Block core's video.h from being included after us
#define VIDEO_H

// PPC video mode info (native driver protocol)
struct VideoInfo {
	int	viType;				// Screen/Window
	uint32 viRowBytes;		// width of each row in memory
	uint16 viXsize,viYsize;	// Window
	uint32 viAppleMode;		// Screen Color Depth
	uint32 viAppleID;		// Screen DisplayID
};

enum {	// viAppleMode
	APPLE_1_BIT = 0x80,
	APPLE_2_BIT,
	APPLE_4_BIT,
	APPLE_8_BIT,
	APPLE_16_BIT,
	APPLE_32_BIT
};

inline bool IsDirectMode(int mode)
{
	return mode == APPLE_16_BIT || mode == APPLE_32_BIT;
}

inline int DepthModeForPixelDepth(int depth)
{
	switch (depth) {
	case 1: return APPLE_1_BIT;
	case 2: return APPLE_2_BIT;
	case 4: return APPLE_4_BIT;
	case 8: return APPLE_8_BIT;
	case 15: case 16: return APPLE_16_BIT;
	case 24: case 32: return APPLE_32_BIT;
	default: return APPLE_1_BIT;
	}
}

inline uint32 TrivialBytesPerRow(uint32 width, int mode)
{
	switch (mode) {
	case APPLE_1_BIT: return (width + 7) / 8;
	case APPLE_2_BIT: return (width + 3) / 4;
	case APPLE_4_BIT: return (width + 1) / 2;
	case APPLE_8_BIT: return width;
	case APPLE_16_BIT: return width * 2;
	case APPLE_32_BIT: return width * 4;
	default: return width;
	}
}

enum {	// viAppleID
	APPLE_640x480 = 0x81,
	APPLE_W_640x480,
	APPLE_800x600,
	APPLE_W_800x600,
	APPLE_1024x768,
	APPLE_1152x768,
	APPLE_1152x900,
	APPLE_1280x1024,
	APPLE_1600x1200,
	APPLE_CUSTOM,            // 0x8A — pinned, semantic preserved

	// mac-phoenix extensions — Apple never shipped IDs for these.
	// See src/common/include/video_modes.h for the canonical table.
	APPLE_832x624,           // 0x8B  Mac 16" RGB
	APPLE_1152x870,          // 0x8C  Mac 21" two-page
	APPLE_1440x900,          // 0x8D  16:10
	APPLE_1920x1080,         // 0x8E  FHD
	APPLE_1920x1200,         // 0x8F  WUXGA
	APPLE_2560x1080,         // 0x90  UWHD 21:9
	APPLE_2560x1440,         // 0x91  QHD
	APPLE_3440x1440,         // 0x92  UWQHD 21:9
	APPLE_3840x1080,         // 0x93  dual-4K side-by-side 32:9
	APPLE_3840x1600,         // 0x94  UWQHD+ 21:9
	APPLE_3840x2160,         // 0x95  4K UHD

	APPLE_ID_MIN = APPLE_640x480,
	APPLE_ID_MAX = APPLE_3840x2160
};

enum {	// Display type
	DIS_INVALID,
	DIS_SCREEN,
	DIS_WINDOW,
	DIS_CHROMAKEY
};

struct VidLocals{
	uint16	saveMode;
	uint32	saveData;
	uint16	savePage;
	uint32	saveBaseAddr;
	uint32	gammaTable;
	uint32	maxGammaTableSize;
	uint32	saveVidParms;
	bool	luminanceMapping;
	bool	cursorHardware;
	int32	cursorX;
	int32	cursorY;
	uint32	cursorVisible;
	uint32	cursorSet;
	bool	cursorHotFlag;
	uint8	cursorHotX;
	uint8	cursorHotY;
	uint32	vslServiceID;
	bool	interruptsEnabled;
	uint32	regEntryID;
};

// PPC video functions and globals
namespace ppc {

extern struct VideoInfo VModes[];
extern bool video_activated;
extern uint32 screen_base;
extern int cur_mode;
extern int display_type;
extern rgb_color mac_pal[256];
extern rgb_color mac_gamma[256];
extern uint8 remap_mac_be[256];
extern uint8 MacCursor[68];
extern VidLocals *private_data;
extern bool keyfile_valid;

extern bool VideoActivated(void);
extern bool VideoSnapshot(int xsize, int ysize, uint8 *p);
extern int16 VideoDoDriverIO(uint32 spaceID, uint32 commandID, uint32 commandContents, uint32 commandCode, uint32 commandKind);
extern bool VideoInit(void);
extern void VideoExit(void);
extern void VideoVBL(void);
extern void VideoInstallAccel(void);
extern void VideoQuitFullScreen(void);

extern void video_set_palette(void);
extern void video_set_gamma(int n_colors);
extern void video_set_cursor(void);
extern bool video_can_change_cursor(void);
extern int16 video_mode_change(VidLocals *csSave, uint32 ParamPtr);
extern void video_set_dirty_area(int x, int y, int w, int h);

extern int16 VSLDoInterruptService(uint32 arg1);
extern void NQDMisc(uint32 arg1, uintptr arg2);

extern bool NQD_sync_hook(uint32);
extern bool NQD_bitblt_hook(uint32);
extern bool NQD_fillrect_hook(uint32);
extern bool NQD_unknown_hook(uint32);
extern void NQD_bitblt(uint32);
extern void NQD_invrect(uint32);
extern void NQD_fillrect(uint32);

} // namespace ppc

#endif /* KPX_VIDEO_H */
