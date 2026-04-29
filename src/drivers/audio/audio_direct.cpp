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
#include "encoders/audio_config.h"
#include "platform.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cmath>

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

// Classic Sound Manager beep queue (used by Mac SE / System 6 / pre-ASC 68k Macs).
// PCM samples are synthesized on the Mac CPU thread inside SoundMgr_SysBeep() and
// drained by the audio thread, 20ms per IPC frame. Format: S16MSB stereo at 48kHz
// (matches the rest of the audio pipeline so Opus encodes without resampling).
static std::mutex beep_queue_mutex;
static std::vector<int16_t> beep_queue;  // interleaved stereo, big-endian wire format

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
	AudioStatus.sample_rate = AUDIO_SAMPLE_RATE << 16;  // 48kHz — must match Opus encoder
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
 *  SoundMgr_SysBeep - Classic Sound Manager _SysBeep hook
 *
 *  Called from the EmulOp dispatcher when the SE / System 6 ROM's _SysBeep
 *  trap fires. The real ROM would program VIA T1 to oscillate PB7 into the
 *  speaker; instead we synthesize a 750 Hz square wave at the pipeline's
 *  native 48 kHz / 16-bit / stereo format and push it into the beep queue.
 *  The audio thread drains the queue 20ms at a time into the IPC ring.
 *
 *  duration_ticks is the Pascal arg from _SysBeep(duration: Integer).
 *  Classic Mac OS treats 0 as "default" (~30 ticks ≈ 500 ms). We cap the
 *  queue at 1 s so a runaway caller can't balloon memory.
 */
void SoundMgr_SysBeep(uint16 duration_ticks)
{
	const uint32_t sample_rate = AUDIO_SAMPLE_RATE;  // 48000
	const uint32_t channels = AUDIO_CHANNELS;        // 2
	const uint32_t beep_hz = 750;
	const int16_t amplitude = 0x2000;  // ~25% to avoid clipping after resample

	uint32_t ticks = duration_ticks ? duration_ticks : 30;  // default ~500 ms
	if (ticks > 60) ticks = 60;                              // clamp to 1 s

	uint32_t total_samples = (sample_rate * ticks) / 60;
	uint32_t half_period = sample_rate / (beep_hz * 2);
	if (half_period == 0) half_period = 1;

	std::vector<int16_t> buf;
	buf.reserve(total_samples * channels);

	// Square wave with a short linear fade in/out (~4 ms each side) to avoid
	// a click at the start/stop of the beep.
	uint32_t fade = sample_rate / 250;  // 4 ms
	if (fade * 2 > total_samples) fade = total_samples / 2;

	for (uint32_t i = 0; i < total_samples; i++) {
		int16_t sample = ((i / half_period) & 1) ? amplitude : -amplitude;
		int32_t scaled = sample;
		if (i < fade) scaled = scaled * (int32_t)i / (int32_t)fade;
		else if (i >= total_samples - fade) scaled = scaled * (int32_t)(total_samples - i) / (int32_t)fade;
		int16_t s16 = (int16_t)scaled;
		// Write as big-endian (S16MSB, matches the rest of the pipeline).
		uint8_t hi = (s16 >> 8) & 0xff;
		uint8_t lo = s16 & 0xff;
		int16_t be;
		uint8_t* p = (uint8_t*)&be;
		p[0] = hi; p[1] = lo;
		for (uint32_t c = 0; c < channels; c++) buf.push_back(be);
	}

	{
		std::lock_guard<std::mutex> lock(beep_queue_mutex);
		beep_queue.insert(beep_queue.end(), buf.begin(), buf.end());
	}
	fprintf(stderr, "[AudioDirect] SysBeep queued: %u ticks, %u samples\n",
	        duration_ticks, total_samples);
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
	const bool headless = (g_ipc_shm == nullptr);

	fprintf(stderr, "[AudioDirect] Audio thread started (%s)\n",
	        headless ? "headless — self-paced drain" : "PULL model");

	while (audio_thread_running) {
		if (headless) {
			// Self-pace at 50 Hz (20 ms per frame) to drain the Apple Mixer.
			// Without this, queued bufferCmds are never consumed and the
			// Sound Manager pipeline stalls.
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		} else {
			// Wait for request from parent (IPC_INPUT_AUDIO_REQUEST)
			std::unique_lock<std::mutex> lock(audio_request_mutex);
			audio_request_cv.wait(lock, []{
				return audio_request_pending || !audio_thread_running;
			});
			if (!audio_thread_running) break;
			audio_request_pending = false;
		}

		if (AudioStatus.num_sources > 0) {
			uint32_t sample_rate = AudioStatus.sample_rate >> 16;
			if (sample_rate == 0) sample_rate = 44100;

			// Trigger Mac audio interrupt
			SetInterruptFlag(INTFLAG_AUDIO);
			TriggerInterrupt();

			// Wait for AudioInterrupt() to complete
			bool got_irq;
			{
				std::unique_lock<std::mutex> lock(audio_irq_mutex);
				auto timeout = std::chrono::milliseconds(10);
				audio_irq_done_cv.wait_for(lock, timeout, []{ return audio_irq_done; });
				got_irq = audio_irq_done;
				audio_irq_done = false;
			}

			// Read audio data from Mac memory and write to IPC SHM
			if (got_irq && audio_data && g_ipc_shm) {
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

			// No data from Mac (or no IPC) — send silence frame if IPC available
			if (g_ipc_shm) {
				uint32_t write_idx = IPC_ATOMIC_LOAD(g_ipc_shm->audio_write_idx);
				uint32_t read_idx = IPC_ATOMIC_LOAD(g_ipc_shm->audio_read_idx);
				uint32_t next_write = (write_idx + 1) % IPC_AUDIO_FRAME_RING_SIZE;

				if (next_write != read_idx) {
					IPCAudioFrame* frame = &g_ipc_shm->audio_frames[write_idx];
					uint32_t sr = AudioStatus.sample_rate >> 16;
					if (sr == 0) sr = 44100;
					uint32_t ch = AudioStatus.channels ? (uint32_t)AudioStatus.channels : 1;
					uint32_t samples = (sr * 20) / 1000;
					// Cap silence-frame size to the SHM slot capacity. AudioStatus
					// values come from the guest and a hostile/buggy combo of
					// sample_rate + channels could otherwise overrun frame->data.
					size_t total_bytes = (size_t)samples * 2 * ch;
					if (total_bytes > IPC_AUDIO_MAX_FRAME_BYTES) {
						total_bytes = IPC_AUDIO_MAX_FRAME_BYTES;
						samples = (uint32_t)(total_bytes / (2 * ch));
					}

					frame->sample_rate = sr;
					frame->channels = ch;
					frame->samples = samples;
					frame->format = 1;
					memset(frame->data, 0, total_bytes);

					IPC_ATOMIC_STORE(g_ipc_shm->audio_write_idx, next_write);
				}
			}
		} else if (g_ipc_shm) {
			// No audio component sources. If the classic Sound Manager hook
			// (SoundMgr_SysBeep) has queued PCM, drain one 20 ms frame out of
			// the queue; otherwise emit silence.
			uint32_t write_idx = IPC_ATOMIC_LOAD(g_ipc_shm->audio_write_idx);
			uint32_t read_idx = IPC_ATOMIC_LOAD(g_ipc_shm->audio_read_idx);
			uint32_t next_write = (write_idx + 1) % IPC_AUDIO_FRAME_RING_SIZE;

			if (next_write != read_idx) {
				IPCAudioFrame* frame = &g_ipc_shm->audio_frames[write_idx];
				uint32_t frame_samples = (AUDIO_SAMPLE_RATE * 20) / 1000;  // 960
				uint32_t frame_values = frame_samples * AUDIO_CHANNELS;
				size_t data_len = frame_values * sizeof(int16_t);

				frame->sample_rate = AUDIO_SAMPLE_RATE;
				frame->channels = AUDIO_CHANNELS;
				frame->samples = frame_samples;
				frame->format = 1;

				bool had_beep = false;
				{
					std::lock_guard<std::mutex> lock(beep_queue_mutex);
					if (!beep_queue.empty()) {
						had_beep = true;
						size_t take = beep_queue.size() < frame_values ? beep_queue.size() : frame_values;
						memcpy(frame->data, beep_queue.data(), take * sizeof(int16_t));
						if (take < frame_values) {
							memset(frame->data + take * sizeof(int16_t), 0,
							       data_len - take * sizeof(int16_t));
						}
						beep_queue.erase(beep_queue.begin(), beep_queue.begin() + take);
					}
				}
				if (!had_beep) {
					memset(frame->data, 0, data_len);
				}

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
