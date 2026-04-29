/*
 *  audio_ipc_reader.cpp - Parent-side audio IPC reader
 *
 *  Ported from legacy/web-streaming/server/server.cpp audio_loop_mac_ipc().
 *  Runs in the parent (webserver) process at 50Hz (20ms frames):
 *
 *  1. Send IPC_INPUT_AUDIO_REQUEST to child
 *  2. Sleep 8ms (give child time to produce frame)
 *  3. Read audio frame from IPC SHM ring buffer
 *  4. Byte-swap S16MSB (Mac big-endian) → S16LE (host)
 *  5. Submit to AudioOutput ring buffer → encoder thread → WebRTC
 */

#include "audio_ipc_reader.h"
#include "ipc_client.h"   // For g_ipc_shm_mutex
#include "ipc_protocol.h"
#include "encoders/audio_config.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <shared_mutex>
#include <thread>
#include <atomic>
#include <chrono>

static std::thread g_reader_thread;
static std::atomic<bool> g_reader_running(false);

static void audio_ipc_reader_loop(IPCClient* client, AudioOutput* output)
{
    fprintf(stderr, "[AudioIPCReader] Thread started (20ms PULL model)\n");

    const auto frame_duration = std::chrono::milliseconds(AUDIO_FRAME_DURATION_MS);
    uint64_t frames_consumed = 0;
    uint64_t frames_underrun = 0;
    bool logged_format = false;

    while (g_reader_running.load(std::memory_order_relaxed)) {
        auto frame_start = std::chrono::steady_clock::now();

        // PULL: Request audio from child. send_audio_request() only uses
        // the control socket, not SHM, so no lock needed here.
        client->send_audio_request(AUDIO_FRAME_SIZE);

        // Give child time to respond (trigger interrupt, execute 68k, write frame)
        std::this_thread::sleep_for(std::chrono::milliseconds(8));

        // Hold shared lock for the entire SHM access window — a concurrent
        // stop()/restart would otherwise munmap the page under our feet.
        std::shared_lock<std::shared_mutex> shm_lock(g_ipc_shm_mutex);
        IPCBuffer* shm = client->shm();
        if (!shm || !client->is_connected()) {
            shm_lock.unlock();
            std::this_thread::sleep_for(frame_duration);
            continue;
        }

        // Read from SHM ring buffer. Mask both indices defensively in case
        // a child crash/restart left a stale or corrupt counter in SHM —
        // an unmasked index would OOB into adjacent SHM regions.
        uint32_t read_idx = IPC_ATOMIC_LOAD(shm->audio_read_idx) % IPC_AUDIO_FRAME_RING_SIZE;
        uint32_t write_idx = IPC_ATOMIC_LOAD(shm->audio_write_idx) % IPC_AUDIO_FRAME_RING_SIZE;

        if (read_idx == write_idx) {
            // Ring buffer empty — underrun
            frames_underrun++;
            // Submit silence to keep encoder going
            int16_t silence[AUDIO_FRAME_SIZE * AUDIO_CHANNELS] = {};
            output->submit_samples(silence, AUDIO_FRAME_SIZE * AUDIO_CHANNELS,
                                   AUDIO_SAMPLE_RATE, AUDIO_CHANNELS);
            goto next_frame;
        }

        {
            const IPCAudioFrame* frame = &shm->audio_frames[read_idx];

            // Byte-swap S16MSB → S16LE and submit to AudioOutput
            int16_t pcm_buffer[960 * 2];  // Max: 48kHz stereo 20ms
            uint32_t total_samples = frame->samples * frame->channels;
            if (total_samples > 960 * 2) total_samples = 960 * 2;

            const uint8_t* src_bytes = frame->data;

            // Detect 8-bit audio masquerading as 16-bit (duplicated bytes)
            // Legacy compatibility: some Mac apps produce 8-bit stored as duplicated 16-bit
            bool is_8bit_duplicated = true;
            uint32_t check_count = total_samples < 20 ? total_samples : 20;
            for (uint32_t i = 0; i < check_count && is_8bit_duplicated; i++) {
                if (src_bytes[i * 2] != src_bytes[i * 2 + 1]) {
                    is_8bit_duplicated = false;
                }
            }

            if (!logged_format && total_samples > 0) {
                if (is_8bit_duplicated) {
                    fprintf(stderr, "[AudioIPCReader] Detected 8-bit audio, converting to 16-bit\n");
                } else {
                    fprintf(stderr, "[AudioIPCReader] Detected true 16-bit audio, byte-swapping\n");
                }
                logged_format = true;
            }

            if (is_8bit_duplicated) {
                // 8-bit: read high byte as signed, scale to 16-bit
                for (uint32_t i = 0; i < total_samples; i++) {
                    int8_t sample_8bit = (int8_t)src_bytes[i * 2];
                    pcm_buffer[i] = (int16_t)sample_8bit << 8;
                }
            } else {
                // True 16-bit: byte-swap big-endian to little-endian
                for (uint32_t i = 0; i < total_samples; i++) {
                    int hi = src_bytes[i * 2];
                    int lo = src_bytes[i * 2 + 1];
                    pcm_buffer[i] = (int16_t)((hi << 8) | lo);
                }
            }

            // Pad to AUDIO_FRAME_SIZE if needed (Opus wants exact frame sizes)
            uint32_t target_samples = AUDIO_FRAME_SIZE * frame->channels;
            if (total_samples < target_samples) {
                memset(&pcm_buffer[total_samples], 0,
                       (target_samples - total_samples) * sizeof(int16_t));
                total_samples = target_samples;
            }

            // Submit to ring buffer
            output->submit_samples(pcm_buffer, total_samples,
                                   frame->sample_rate, frame->channels);

            // Advance read index
            uint32_t next_read = (read_idx + 1) % IPC_AUDIO_FRAME_RING_SIZE;
            IPC_ATOMIC_STORE(shm->audio_read_idx, next_read);
            frames_consumed++;

        }

next_frame:
        // Sleep for remainder of 20ms frame
        auto elapsed = std::chrono::steady_clock::now() - frame_start;
        auto remaining = frame_duration - elapsed;
        if (remaining > std::chrono::milliseconds(0)) {
            std::this_thread::sleep_for(remaining);
        }
    }

    fprintf(stderr, "[AudioIPCReader] Thread exiting (%" PRIu64 " frames, %" PRIu64 " underruns)\n",
            frames_consumed, frames_underrun);
}


void audio_ipc_reader_start(IPCClient* client, AudioOutput* output)
{
    if (g_reader_running.load()) return;

    g_reader_running.store(true, std::memory_order_release);
    g_reader_thread = std::thread(audio_ipc_reader_loop, client, output);
}

void audio_ipc_reader_stop(void)
{
    if (!g_reader_running.load()) return;

    g_reader_running.store(false, std::memory_order_release);
    if (g_reader_thread.joinable()) {
        g_reader_thread.join();
    }
}
