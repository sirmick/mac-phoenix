/*
 * Video Encoder Thread - In-Process Architecture
 *
 * Reads frames from VideoOutput triple buffer and encodes to H.264/VP9/WebP/PNG.
 * Handles codec changes dynamically by reinitializing encoder.
 *
 * Thread Safety:
 * - Reads from VideoOutput (lock-free triple buffer)
 * - Sends encoded frames to WebRTC (thread-safe queue)
 * - Checks config for codec changes (atomic read)
 */

#include "video_encoder_thread.h"
#include "video_output.h"
#include "../../config/emulator_config.h"
#include "../../webrtc/webrtc_server.h"
#include "encoders/h264_encoder.h"
#include "encoders/vp9_encoder.h"
#include "encoders/webp_encoder.h"
#include "encoders/png_encoder.h"
#include "encoders/codec.h"

#include <memory>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace video {

// External globals (to be replaced with proper dependency injection later)
extern std::atomic<bool> g_running;
extern std::atomic<bool> g_request_keyframe;

// Statistics
static std::atomic<uint64_t> g_frames_encoded(0);
static std::atomic<uint64_t> g_frames_dropped(0);

/**
 * Create encoder based on codec type
 */
static std::unique_ptr<VideoCodec> create_video_encoder(CodecType codec) {
    switch (codec) {
        case CodecType::H264:
            fprintf(stderr, "[VideoEncoder] Creating H.264 encoder\n");
            return std::make_unique<H264Encoder>();

        case CodecType::VP9:
            fprintf(stderr, "[VideoEncoder] Creating VP9 encoder\n");
            return std::make_unique<VP9Encoder>();

        case CodecType::WEBP:
            fprintf(stderr, "[VideoEncoder] Creating WebP encoder\n");
            return std::make_unique<WebPEncoder>();

        case CodecType::PNG:
        default:
            fprintf(stderr, "[VideoEncoder] Creating PNG encoder\n");
            return std::make_unique<PNGEncoder>();
    }
}

/**
 * Send encoded frame to WebRTC
 */
static void send_encoded_frame(const EncodedFrame& frame) {
    static bool debug_frames = (getenv("MACEMU_DEBUG_FRAMES") != nullptr);
    static int frame_count = 0;

    g_frames_encoded++;
    frame_count++;

    // Send to WebRTC server if available
    if (webrtc::g_server) {
        webrtc::g_server->send_video_frame(
            frame.data.data(),
            frame.data.size(),
            frame.is_keyframe
        );

        if (debug_frames && (frame_count % 60 == 0 || frame.is_keyframe)) {
            fprintf(stderr, "[VideoEncoder] Sent frame #%d to WebRTC: %zu bytes, keyframe=%d\n",
                    frame_count, frame.data.size(), frame.is_keyframe);
        }
    } else {
        if (debug_frames && frame_count == 1) {
            fprintf(stderr, "[VideoEncoder] WARNING: webrtc::g_server is NULL, frames not being sent!\n");
        }
    }
}

/**
 * Video Encoder Thread Main Loop
 *
 * @param video_output Triple buffer to read frames from
 * @param config Configuration (for codec selection)
 */
void video_encoder_main(VideoOutput* video_output, config::EmulatorConfig* config) {
    fprintf(stderr, "[VideoEncoder] Thread starting\n");

    // Debug flags
    static bool debug_frames = (getenv("MACEMU_DEBUG_FRAMES") != nullptr);
    static bool debug_perf = config ? config->debug_perf : (getenv("MACEMU_DEBUG_PERF") != nullptr);

    // Initialize encoder with codec from config
    CodecType current_codec = CodecType::PNG;  // Default
    if (config) {
        const std::string& codec_str = config->codec;
        if (codec_str == "h264") current_codec = CodecType::H264;
        else if (codec_str == "vp9") current_codec = CodecType::VP9;
        else if (codec_str == "webp") current_codec = CodecType::WEBP;
        else current_codec = CodecType::PNG;
    }

    auto encoder = create_video_encoder(current_codec);
    bool encoder_initialized = false;

    // Statistics
    auto last_stats_time = std::chrono::steady_clock::now();
    int frames_since_stats = 0;

    fprintf(stderr, "[VideoEncoder] Entering frame processing loop\n");

    while (g_running.load(std::memory_order_relaxed)) {
        // Check for codec changes (read config atomically)
        // In production, config changes trigger an event instead of polling
        CodecType new_codec = current_codec;
        if (config) {
            const std::string& codec_str = config->codec;
            if (codec_str == "h264") new_codec = CodecType::H264;
            else if (codec_str == "vp9") new_codec = CodecType::VP9;
            else if (codec_str == "webp") new_codec = CodecType::WEBP;
            else new_codec = CodecType::PNG;
        }

        if (new_codec != current_codec) {
            fprintf(stderr, "[VideoEncoder] Codec change detected: %d -> %d\n",
                    (int)current_codec, (int)new_codec);

            // Reinitialize encoder
            encoder = create_video_encoder(new_codec);
            encoder_initialized = false;
            current_codec = new_codec;

            // Request keyframe for new peers
            g_request_keyframe.store(true, std::memory_order_release);

            // Notify WebRTC to disconnect peers so they reconnect with new codec
            if (webrtc::g_server) {
                webrtc::g_server->notify_codec_change(new_codec);
            }
        }

        // Wait for new frame (blocks until frame available or timeout)
        const FrameBuffer* frame = video_output->wait_for_frame(100);  // 100ms timeout

        if (!frame) {
            // Timeout or shutdown
            if (debug_frames && frames_since_stats == 0) {
                fprintf(stderr, "[VideoEncoder] No frame available (timeout)\n");
            }
            continue;
        }

        if (debug_frames) {
            fprintf(stderr, "[VideoEncoder] Received frame %dx%d format=%d\n",
                    frame->width, frame->height, (int)frame->format);
        }

        // Initialize encoder on first frame (need width/height)
        if (!encoder_initialized) {
            if (encoder->init(frame->width, frame->height, 60)) {
                fprintf(stderr, "[VideoEncoder] Initialized %dx%d @ 60 FPS\n",
                        frame->width, frame->height);
                encoder_initialized = true;
            } else {
                fprintf(stderr, "[VideoEncoder] ERROR: Failed to initialize encoder\n");
                video_output->release_frame();
                continue;
            }
        }

        // Check if keyframe requested
        if (g_request_keyframe.exchange(false, std::memory_order_acq_rel)) {
            encoder->request_keyframe();
            fprintf(stderr, "[VideoEncoder] Keyframe requested\n");
        }

        // Encode frame based on pixel format
        EncodedFrame encoded;
        auto encode_start = std::chrono::steady_clock::now();

        if (frame->format == PIXFMT_BGRA) {
            // BGRA format (bytes B,G,R,A - libyuv "ARGB")
            encoded = encoder->encode_bgra(reinterpret_cast<const uint8_t*>(frame->pixels),
                                          frame->width, frame->height,
                                          frame->width * 4);  // stride
        } else {
            // ARGB format (bytes A,R,G,B - libyuv "BGRA", Mac native)
            encoded = encoder->encode_argb(reinterpret_cast<const uint8_t*>(frame->pixels),
                                          frame->width, frame->height,
                                          frame->width * 4);
        }

        auto encode_end = std::chrono::steady_clock::now();
        auto encode_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            encode_end - encode_start).count();

        // Release frame (mark as consumed)
        video_output->release_frame();

        // Send encoded frame to WebRTC
        if (encoded.data.size() > 0) {
            send_encoded_frame(encoded);
            frames_since_stats++;

            // Debug: Log first 10 encoded frames
            static int encoded_count = 0;
            encoded_count++;
            if (encoded_count <= 10) {
                fprintf(stderr, "[VideoEncoder] Encoded frame #%d: %zu bytes, keyframe=%d, took %ld ms\n",
                        encoded_count, encoded.data.size(), encoded.is_keyframe, encode_ms);
            }
        } else {
            fprintf(stderr, "[VideoEncoder] WARNING: Encoding produced empty frame\n");
        }

        // Print statistics every 3 seconds
        auto now = std::chrono::steady_clock::now();
        auto stats_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_stats_time).count();

        if (stats_elapsed >= 3) {
            double fps = frames_since_stats / (double)stats_elapsed;
            uint64_t total_encoded = g_frames_encoded.load(std::memory_order_relaxed);
            uint64_t total_dropped = g_frames_dropped.load(std::memory_order_relaxed);

            fprintf(stderr, "[VideoEncoder] Stats: %.1f FPS, %llu encoded, %llu dropped, encode: %ld ms\n",
                    fps, (unsigned long long)total_encoded, (unsigned long long)total_dropped, encode_ms);

            last_stats_time = now;
            frames_since_stats = 0;
        }
    }

    fprintf(stderr, "[VideoEncoder] Thread exiting\n");
}

} // namespace video
