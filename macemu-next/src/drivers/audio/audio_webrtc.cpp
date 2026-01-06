/*
 *  audio_webrtc.cpp - WebRTC audio driver
 *
 *  Manages WebRTC audio streaming:
 *  - Creates AudioOutput ring buffer
 *  - Launches audio encoder thread
 */

#include "sysdeps.h"
#include "platform.h"
#include "audio_output.h"
#include "audio_encoder_thread.h"
#include <thread>
#include <atomic>

// Audio encoder globals (defined in main.cpp)
namespace audio {
	extern std::atomic<bool> g_running;
}

// WebRTC audio state
namespace {
	AudioOutput* g_audio_output = nullptr;
	std::thread* g_encoder_thread = nullptr;
}

/*
 *  Initialization
 */
void audio_webrtc_init(void)
{
	// Create AudioOutput ring buffer (48kHz stereo)
	g_audio_output = new AudioOutput(48000, 2);

	// Launch audio encoder thread
	audio::g_running.store(true, std::memory_order_release);
	g_encoder_thread = new std::thread(audio::audio_encoder_main, g_audio_output);
}

/*
 *  Deinitialization
 */
void audio_webrtc_exit(void)
{
	// Stop encoder thread
	if (g_encoder_thread) {
		audio::g_running.store(false, std::memory_order_release);
		g_encoder_thread->join();
		delete g_encoder_thread;
		g_encoder_thread = nullptr;
	}

	// Delete AudioOutput
	if (g_audio_output) {
		delete g_audio_output;
		g_audio_output = nullptr;
	}
}
