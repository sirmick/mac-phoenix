/*
 *  audio_direct.cpp - Audio driver for IPC child process
 *
 *  Ported from legacy/BasiliskII/src/IPC/audio_ipc.cpp
 *  Runs in the emulator child process. AudioInterrupt() calls Execute68k
 *  to get Apple Mixer data, reads PCM from Mac memory, and writes raw
 *  S16MSB frames to IPC shared memory ring buffer for the parent to
 *  encode and stream via WebRTC.
 *
 *  Architecture (PULL model):
 *  - Parent sends IPC_INPUT_AUDIO_REQUEST every 20ms
 *  - audio_request_data() wakes the audio thread
 *  - Audio thread triggers INTFLAG_AUDIO → AudioInterrupt() runs
 *  - AudioInterrupt calls Execute68k(adatGetSourceData) to fill buffer
 *  - Audio thread reads PCM from Mac memory → writes to IPC SHM ring buffer
 */

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "audio.h"
#include "audio_defs.h"
#include "ipc_protocol.h"
#include "platform.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>

#define DEBUG 0
#include "debug.h"

// IPC SHM pointer (set by audio_direct_set_ipc_buffer)
static IPCBuffer* g_ipc_shm = nullptr;

// Audio thread
static std::thread audio_thread;
static std::atomic<bool> audio_thread_running(false);

// Synchronization for AudioInterrupt (audio thread → Mac emulation thread)
static std::mutex audio_irq_mutex;
static std::condition_variable audio_irq_done_cv;
static bool audio_irq_done = false;

// Synchronization for server audio requests (parent → audio thread)
static std::mutex audio_request_mutex;
static std::condition_variable audio_request_cv;
static bool audio_request_pending = false;

// Counters
static uint64_t audio_frames_sent = 0;
static uint64_t last_log_frame = 0;

// Forward declaration
static void audio_thread_func();


/*
 *  Set IPC buffer pointer (called from main.cpp before audio_direct_init)
 */

void audio_direct_set_ipc_buffer(IPCBuffer* buf)
{
	g_ipc_shm = buf;
}


/*
 *  Platform API: audio_init
 */

void audio_direct_init(void)
{
	// Init audio status and feature flags (from legacy audio_ipc.cpp)
	AudioStatus.sample_rate = 44100 << 16;  // Mac 16.16 fixed point
	AudioStatus.sample_size = 16;
	AudioStatus.channels = 2;
	AudioStatus.mixer = 0;
	AudioStatus.num_sources = 0;
	audio_component_flags = cmpWantsRegisterMessage | kStereoOut | k16BitOut;

	// Supported audio formats
	audio_sample_rates.push_back(11025 << 16);
	audio_sample_rates.push_back(22050 << 16);
	audio_sample_rates.push_back(44100 << 16);
	audio_sample_rates.push_back(48000 << 16);

	audio_sample_sizes.push_back(8);
	audio_sample_sizes.push_back(16);

	audio_channel_counts.push_back(1);
	audio_channel_counts.push_back(2);

	// Set frames per block (20ms worth at current rate)
	uint32_t sample_rate = AudioStatus.sample_rate >> 16;
	audio_frames_per_block = (sample_rate * 20) / 1000;

	// Mark audio as available
	audio_open = true;

	// Start audio thread (waits for requests from parent)
	audio_thread_running = true;
	audio_thread = std::thread(audio_thread_func);

	fprintf(stderr, "[AudioDirect] Initialized: %u Hz, %d-bit, %d ch, %d samples/block, IPC SHM=%p\n",
	        sample_rate, 16, 2, audio_frames_per_block, (void*)g_ipc_shm);
}


/*
 *  Platform API: audio_exit
 */

void audio_direct_exit(void)
{
	if (audio_thread_running) {
		audio_thread_running = false;
		// Wake thread so it exits
		{
			std::lock_guard<std::mutex> lock(audio_request_mutex);
			audio_request_pending = true;
			audio_request_cv.notify_one();
		}
		if (audio_thread.joinable()) {
			audio_thread.join();
		}
	}

	audio_open = false;
	fprintf(stderr, "[AudioDirect] Shutdown (%" PRIu64 " frames sent)\n", audio_frames_sent);
}


/*
 *  audio_enter_stream - First source added (num_sources 0→1)
 */

void audio_enter_stream()
{
	D(bug("[AudioDirect] Stream started\n"));
}


/*
 *  audio_exit_stream - Last source removed (num_sources 1→0)
 */

void audio_exit_stream()
{
	D(bug("[AudioDirect] Stream stopped\n"));
}


/*
 *  AudioInterrupt - Mac audio interrupt handler
 *
 *  Called from emul_op.cpp when INTFLAG_AUDIO is set.
 *  Calls Execute68k to get Apple Mixer to fill audio buffer,
 *  then signals the audio thread that data is ready.
 *
 *  Ported directly from legacy audio_ipc.cpp
 */

void AudioInterrupt(void)
{
	D(bug("[AudioDirect] AudioInterrupt\n"));

	if (!audio_data) {
		// Audio component closed
	} else if (AudioStatus.mixer) {
		M68kRegisters r;
		r.a[0] = audio_data + adatStreamInfo;
		r.a[1] = AudioStatus.mixer;
		Execute68k(audio_data + adatGetSourceData, &r);
		D(bug(" GetSourceData() returns %08lx\n", (unsigned long)r.d[0]));
	} else {
		WriteMacInt32(audio_data + adatStreamInfo, 0);
	}

	// Signal audio thread that interrupt is complete
	{
		std::lock_guard<std::mutex> lock(audio_irq_mutex);
		audio_irq_done = true;
	}
	audio_irq_done_cv.notify_one();
}


/*
 *  audio_request_data - Called when parent sends IPC_INPUT_AUDIO_REQUEST
 *  Wakes up the audio thread to produce a frame.
 */

void audio_request_data(uint32_t requested_samples)
{
	(void)requested_samples;
	{
		std::lock_guard<std::mutex> lock(audio_request_mutex);
		audio_request_pending = true;
	}
	audio_request_cv.notify_one();
}


/*
 *  Audio thread - PULL model (waits for requests from parent)
 *
 *  1. Wait for audio_request_data() signal
 *  2. Trigger INTFLAG_AUDIO → AudioInterrupt() → Execute68k
 *  3. Read PCM from Mac memory
 *  4. Write raw S16MSB frame to IPC SHM ring buffer
 */

static void audio_thread_func()
{
	fprintf(stderr, "[AudioDirect] Audio thread started (PULL model)\n");

	while (audio_thread_running) {
		// Wait for request from parent
		{
			std::unique_lock<std::mutex> lock(audio_request_mutex);
			audio_request_cv.wait(lock, []{
				return audio_request_pending || !audio_thread_running;
			});
			if (!audio_thread_running) break;
			audio_request_pending = false;
		}

		if (!g_ipc_shm) continue;

		if (AudioStatus.num_sources > 0) {
			uint32_t sample_rate = AudioStatus.sample_rate >> 16;
			if (sample_rate == 0) sample_rate = 44100;

			// Trigger Mac audio interrupt
			SetInterruptFlag(INTFLAG_AUDIO);
			TriggerInterrupt();

			// Wait for AudioInterrupt() to complete
			{
				std::unique_lock<std::mutex> lock(audio_irq_mutex);
				auto timeout = std::chrono::milliseconds(10);
				audio_irq_done_cv.wait_for(lock, timeout, []{ return audio_irq_done; });
				if (!audio_irq_done) {
					// Timeout - send silence
					goto send_silence;
				}
				audio_irq_done = false;
			}

			// Read audio data from Mac memory and write to IPC SHM
			if (audio_data) {
				uint32 apple_stream_info = ReadMacInt32(audio_data + adatStreamInfo);

				if (apple_stream_info) {
					uint32 sample_count = ReadMacInt32(apple_stream_info + scd_sampleCount);
					uint32 buffer_ptr = ReadMacInt32(apple_stream_info + scd_buffer);
					uint32 num_channels = ReadMacInt16(apple_stream_info + scd_numChannels);
					uint32 sample_size = ReadMacInt16(apple_stream_info + scd_sampleSize);

					if (sample_count > 0 && buffer_ptr != 0) {
						// Check ring buffer space
						uint32_t write_idx = IPC_ATOMIC_LOAD(g_ipc_shm->audio_write_idx);
						uint32_t read_idx = IPC_ATOMIC_LOAD(g_ipc_shm->audio_read_idx);
						uint32_t next_write = (write_idx + 1) % IPC_AUDIO_FRAME_RING_SIZE;

						if (next_write == read_idx) {
							// Ring full, drop frame
							continue;
						}

						IPCAudioFrame* frame = &g_ipc_shm->audio_frames[write_idx];

						// Fill metadata
						frame->sample_rate = sample_rate;
						frame->channels = num_channels;
						frame->samples = sample_count;
						frame->format = 1; // PCM_S16

						// Copy raw audio data (S16MSB, big-endian)
						uint32_t bytes_per_sample = sample_size >> 3;
						size_t data_len = sample_count * bytes_per_sample * num_channels;
						if (data_len > IPC_AUDIO_MAX_FRAME_BYTES) {
							data_len = IPC_AUDIO_MAX_FRAME_BYTES;
							frame->samples = data_len / (bytes_per_sample * num_channels);
						}

						uint8_t* src = Mac2HostAddr(buffer_ptr);

						if (sample_size == 8) {
							// Convert U8 to S16MSB (big-endian) in-place for consistent format
							uint8_t* src_u8 = src;
							int16_t* dst = (int16_t*)frame->data;
							uint32_t total = frame->samples * num_channels;
							for (uint32_t i = 0; i < total; i++) {
								int16_t val = ((int16_t)src_u8[i] - 128) << 8;
								// Store as big-endian
								uint8_t* p = (uint8_t*)&dst[i];
								p[0] = (val >> 8) & 0xFF;
								p[1] = val & 0xFF;
							}
						} else {
							// S16MSB — direct copy (Mac native format)
							memcpy(frame->data, src, data_len);
						}

						// Publish frame
						IPC_ATOMIC_STORE(g_ipc_shm->audio_write_idx, next_write);

						audio_frames_sent++;
						if (audio_frames_sent - last_log_frame >= 100) {
							fprintf(stderr, "[AudioDirect] %" PRIu64 " frames, %u samples, %u Hz, %u ch\n",
							        audio_frames_sent, frame->samples, sample_rate, num_channels);
							last_log_frame = audio_frames_sent;
						}
						continue;
					}
				}
			}

send_silence:
			// No data from Mac — send silence frame
			{
				uint32_t write_idx = IPC_ATOMIC_LOAD(g_ipc_shm->audio_write_idx);
				uint32_t read_idx = IPC_ATOMIC_LOAD(g_ipc_shm->audio_read_idx);
				uint32_t next_write = (write_idx + 1) % IPC_AUDIO_FRAME_RING_SIZE;

				if (next_write != read_idx) {
					IPCAudioFrame* frame = &g_ipc_shm->audio_frames[write_idx];
					uint32_t sample_rate = AudioStatus.sample_rate >> 16;
					if (sample_rate == 0) sample_rate = 44100;

					frame->sample_rate = sample_rate;
					frame->channels = AudioStatus.channels;
					frame->samples = (sample_rate * 20) / 1000;
					frame->format = 1;
					memset(frame->data, 0, frame->samples * 2 * frame->channels);

					IPC_ATOMIC_STORE(g_ipc_shm->audio_write_idx, next_write);
				}
			}
		} else {
			// No sources — send silence
			uint32_t write_idx = IPC_ATOMIC_LOAD(g_ipc_shm->audio_write_idx);
			uint32_t read_idx = IPC_ATOMIC_LOAD(g_ipc_shm->audio_read_idx);
			uint32_t next_write = (write_idx + 1) % IPC_AUDIO_FRAME_RING_SIZE;

			if (next_write != read_idx) {
				IPCAudioFrame* frame = &g_ipc_shm->audio_frames[write_idx];
				frame->sample_rate = 44100;
				frame->channels = 2;
				frame->samples = 882;
				frame->format = 1;
				memset(frame->data, 0, frame->samples * 2 * frame->channels);
				IPC_ATOMIC_STORE(g_ipc_shm->audio_write_idx, next_write);
			}
		}
	}

	fprintf(stderr, "[AudioDirect] Audio thread exiting\n");
}


/*
 *  Audio info stubs
 */

bool audio_get_main_mute(void)       { return false; }
uint32 audio_get_main_volume(void)   { return 0x0100; }
bool audio_get_speaker_mute(void)    { return false; }
uint32 audio_get_speaker_volume(void){ return 0x0100; }
void audio_set_main_mute(bool)       {}
void audio_set_main_volume(uint32)   {}
void audio_set_speaker_mute(bool)    {}
void audio_set_speaker_volume(uint32){}

bool audio_set_sample_rate(int)  { return true; }
bool audio_set_sample_size(int)  { return true; }
bool audio_set_channels(int)     { return true; }
