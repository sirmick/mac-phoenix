/*
 *  audio_webrtc.h - WebRTC audio driver
 */

#ifndef AUDIO_WEBRTC_H
#define AUDIO_WEBRTC_H

// WebRTC audio driver functions (compatible with platform API)
namespace config { struct EmulatorConfig; }
void audio_webrtc_init(const config::EmulatorConfig* config);
void audio_webrtc_exit(void);

#endif // AUDIO_WEBRTC_H
