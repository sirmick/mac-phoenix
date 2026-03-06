/*
 *  video_webrtc.h - WebRTC video driver
 */

#ifndef VIDEO_WEBRTC_H
#define VIDEO_WEBRTC_H

#include "../../config/emulator_config.h"

// WebRTC video driver functions (compatible with platform API)
bool video_webrtc_init(bool classic, config::EmulatorConfig* config);
void video_webrtc_exit(void);
void video_webrtc_refresh(void);

#endif // VIDEO_WEBRTC_H
