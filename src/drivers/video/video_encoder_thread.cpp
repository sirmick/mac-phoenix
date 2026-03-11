/*
 * Video Encoder Thread - In-Process Architecture
 *
 * Reads frames from VideoOutput triple buffer and encodes to H.264/VP9/WebP/PNG.
 * Handles codec changes dynamically by reinitializing encoder.
 *
 * For PNG/WebP (DataChannel codecs): computes dirty rectangles by comparing
 * current frame against previous frame, encodes only the changed region.
 * This reduces frame size from ~280KB to typically 5-50KB for static UI.
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
#include <cstring>
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
            frame.is_keyframe,
            frame.width,
            frame.height,
            frame.dirty_x,
            frame.dirty_y,
            frame.dirty_width,
            frame.dirty_height,
            frame.frame_width,
            frame.frame_height
        );

        if (debug_frames && (frame_count % 60 == 0 || frame.is_keyframe)) {
            fprintf(stderr, "[VideoEncoder] Sent frame #%d to WebRTC: %zu bytes, keyframe=%d, dirty=%dx%d+%d+%d\n",
                    frame_count, frame.data.size(), frame.is_keyframe,
                    frame.dirty_width, frame.dirty_height, frame.dirty_x, frame.dirty_y);
        }
    } else {
        if (debug_frames && frame_count == 1) {
            fprintf(stderr, "[VideoEncoder] WARNING: webrtc::g_server is NULL, frames not being sent!\n");
        }
    }
}

/**
 * Compute dirty rectangle by comparing current frame against previous frame.
 * Returns true if changes found (dirty rect populated), false if identical.
 *
 * Ported from legacy/BasiliskII/src/IPC/video_ipc.cpp
 */
static bool compute_dirty_rect(const uint32_t* curr, const uint32_t* prev,
                                int width, int height,
                                int& out_x, int& out_y, int& out_w, int& out_h) {
    int min_x = width, max_x = 0;
    int min_y = height, max_y = 0;
    bool found = false;

    for (int y = 0; y < height; y++) {
        const uint32_t* curr_row = curr + y * width;
        const uint32_t* prev_row = prev + y * width;

        for (int x = 0; x < width; x++) {
            if (curr_row[x] != prev_row[x]) {
                if (!found) {
                    found = true;
                    min_y = y;
                }
                max_y = y;
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
            }
        }
    }

    if (!found) return false;

    // Add 1-pixel margin for PNG filtering artifacts
    out_x = (min_x > 1) ? min_x - 1 : 0;
    out_y = (min_y > 1) ? min_y - 1 : 0;
    out_w = (max_x < width - 2) ? (max_x - out_x + 2) : (width - out_x);
    out_h = (max_y < height - 2) ? (max_y - out_y + 2) : (height - out_y);

    // If dirty rect > 75% of screen, use full frame
    int dirty_pixels = out_w * out_h;
    int total_pixels = width * height;
    if (dirty_pixels > (total_pixels * 3 / 4)) {
        out_x = 0;
        out_y = 0;
        out_w = width;
        out_h = height;
    }

    return true;
}

/**
 * Encode a region as horizontal strips that fit within DataChannel size limit.
 * Sends each strip individually via send_encoded_frame().
 * Returns number of strips sent.
 */
static int encode_and_send_strips(VideoCodec* encoder, const uint32_t* pixels,
                                   int frame_w, int frame_h, PixelFormat format,
                                   int rect_x, int rect_y, int rect_w, int rect_h) {
    const int DC_TARGET_SIZE = 200000;  // Keep under 200KB (256KB limit minus header)
    int num_strips = 2;
    int strips_sent = 0;

    while (num_strips <= 16) {
        bool all_ok = true;
        strips_sent = 0;
        int strip_h = rect_h / num_strips;
        int remainder = rect_h - strip_h * num_strips;

        for (int s = 0; s < num_strips; s++) {
            int sy = rect_y + s * strip_h;
            int sh = strip_h + (s == num_strips - 1 ? remainder : 0);

            EncodedFrame strip;
            auto* png_enc = dynamic_cast<PNGEncoder*>(encoder);
            auto* webp_enc = dynamic_cast<WebPEncoder*>(encoder);

            if (format == PIXFMT_BGRA) {
                if (png_enc)
                    strip = png_enc->encode_bgra_rect(
                        reinterpret_cast<const uint8_t*>(pixels), frame_w, frame_h, frame_w * 4,
                        rect_x, sy, rect_w, sh);
                else if (webp_enc)
                    strip = webp_enc->encode_bgra_rect(
                        reinterpret_cast<const uint8_t*>(pixels), frame_w, frame_h, frame_w * 4,
                        rect_x, sy, rect_w, sh);
            } else {
                // ARGB: extract strip and convert to BGRA
                std::vector<uint8_t> strip_bgra(rect_w * sh * 4);
                for (int ry = 0; ry < sh; ry++) {
                    const uint8_t* src = reinterpret_cast<const uint8_t*>(pixels) + (sy + ry) * frame_w * 4 + rect_x * 4;
                    uint8_t* dst = strip_bgra.data() + ry * rect_w * 4;
                    for (int rx = 0; rx < rect_w; rx++) {
                        dst[rx * 4 + 0] = src[rx * 4 + 3]; // B
                        dst[rx * 4 + 1] = src[rx * 4 + 2]; // G
                        dst[rx * 4 + 2] = src[rx * 4 + 1]; // R
                        dst[rx * 4 + 3] = src[rx * 4 + 0]; // A
                    }
                }
                strip = encoder->encode_bgra(strip_bgra.data(), rect_w, sh, rect_w * 4);
            }

            if (strip.data.size() > (size_t)DC_TARGET_SIZE) {
                num_strips *= 2;
                all_ok = false;
                break;
            }

            strip.dirty_x = rect_x;
            strip.dirty_y = sy;
            strip.dirty_width = rect_w;
            strip.dirty_height = sh;
            strip.frame_width = frame_w;
            strip.frame_height = frame_h;

            if (strip.data.size() > 0) {
                send_encoded_frame(strip);
                strips_sent++;
            }
        }

        if (all_ok) break;
    }

    return strips_sent;
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
    [[maybe_unused]] static bool debug_perf = config ? config->debug_perf : (getenv("MACEMU_DEBUG_PERF") != nullptr);

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

    // Previous frame buffer for dirty rect computation (PNG/WebP only)
    std::vector<uint32_t> prev_frame;
    bool have_prev_frame = false;

    // Dirty rect statistics
    int dirty_rect_frames = 0;
    int full_frames = 0;
    int skipped_frames = 0;
    uint64_t total_full_bytes = 0;   // What we'd send without dirty rects
    uint64_t total_dirty_bytes = 0;  // What we actually sent

    // General statistics
    auto last_stats_time = std::chrono::steady_clock::now();
    int frames_since_stats = 0;
    long last_encode_ms = 0;

    fprintf(stderr, "[VideoEncoder] Entering frame processing loop\n");

    while (g_running.load(std::memory_order_relaxed)) {
        // Check for codec changes
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

            encoder = create_video_encoder(new_codec);
            encoder_initialized = false;
            current_codec = new_codec;
            have_prev_frame = false;  // Reset dirty rect state on codec change

            g_request_keyframe.store(true, std::memory_order_release);
        }

        // Wait for new frame (blocks until frame available or timeout)
        const FrameBuffer* frame = video_output->wait_for_frame(100);  // 100ms timeout

        if (!frame) {
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
        bool keyframe_requested = g_request_keyframe.exchange(false, std::memory_order_acq_rel);
        if (keyframe_requested) {
            encoder->request_keyframe();
            fprintf(stderr, "[VideoEncoder] Keyframe requested\n");
        }

        const int w = frame->width;
        const int h = frame->height;
        const uint32_t* pixels = frame->pixels;
        const bool is_dc_codec = (current_codec == CodecType::PNG || current_codec == CodecType::WEBP);

        // ── Dirty rect path (PNG/WebP only) ───────────────────────────
        if (is_dc_codec && have_prev_frame && !keyframe_requested
            && (int)prev_frame.size() == w * h) {

            int dx, dy, dw, dh;
            bool changed = compute_dirty_rect(pixels, prev_frame.data(), w, h, dx, dy, dw, dh);

            if (!changed) {
                // No changes — skip this frame entirely
                skipped_frames++;
                // Save current frame as prev (in case of accumulated drift)
                memcpy(prev_frame.data(), pixels, w * h * 4);
                video_output->release_frame();
                continue;
            }

            // Encode the dirty rectangle
            auto encode_start = std::chrono::steady_clock::now();

            // Encode rect (may split into strips if too large for DataChannel)
            int sent = encode_and_send_strips(encoder.get(), pixels, w, h,
                                               frame->format, dx, dy, dw, dh);
            frames_since_stats += sent;
            dirty_rect_frames++;

            auto encode_end = std::chrono::steady_clock::now();
            last_encode_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                encode_end - encode_start).count();

            // Save current frame for next comparison
            memcpy(prev_frame.data(), pixels, w * h * 4);
            video_output->release_frame();

        } else if (is_dc_codec) {
            // ── First/keyframe DC frame: send as strips to fit within DC size limit ──
            auto encode_start = std::chrono::steady_clock::now();

            int sent = encode_and_send_strips(encoder.get(), pixels, w, h,
                                               frame->format, 0, 0, w, h);
            fprintf(stderr, "[VideoEncoder] DC full frame: sent %d strips (%dx%d, keyframe=%d)\n",
                    sent, w, h, keyframe_requested ? 1 : 0);
            frames_since_stats += sent;
            full_frames++;

            // Save current frame for next comparison
            if ((int)prev_frame.size() != w * h) {
                prev_frame.resize(w * h);
            }
            memcpy(prev_frame.data(), pixels, w * h * 4);
            have_prev_frame = true;

            auto encode_end = std::chrono::steady_clock::now();
            last_encode_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                encode_end - encode_start).count();

            video_output->release_frame();

        } else {
            // ── Full frame path (H264/VP9) ──────────────────────────────

            auto encode_start = std::chrono::steady_clock::now();
            EncodedFrame encoded;

            if (frame->format == PIXFMT_BGRA) {
                encoded = encoder->encode_bgra(reinterpret_cast<const uint8_t*>(pixels),
                                              w, h, w * 4);
            } else {
                encoded = encoder->encode_argb(reinterpret_cast<const uint8_t*>(pixels),
                                              w, h, w * 4);
            }

            auto encode_end = std::chrono::steady_clock::now();
            last_encode_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                encode_end - encode_start).count();

            video_output->release_frame();

            if (encoded.data.size() > 0) {
                send_encoded_frame(encoded);
                frames_since_stats++;

                static int encoded_count = 0;
                encoded_count++;
                if (encoded_count <= 10) {
                    fprintf(stderr, "[VideoEncoder] Encoded frame #%d: %zu bytes, keyframe=%d, took %ld ms\n",
                            encoded_count, encoded.data.size(), encoded.is_keyframe, last_encode_ms);
                }
            } else {
                fprintf(stderr, "[VideoEncoder] WARNING: Encoding produced empty frame\n");
            }
        }

        // Print statistics every 3 seconds
        auto now = std::chrono::steady_clock::now();
        auto stats_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_stats_time).count();

        if (stats_elapsed >= 3) {
            double fps = frames_since_stats / (double)stats_elapsed;
            uint64_t total_encoded = g_frames_encoded.load(std::memory_order_relaxed);
            uint64_t total_dropped = g_frames_dropped.load(std::memory_order_relaxed);

            if (is_dc_codec && (dirty_rect_frames > 0 || skipped_frames > 0)) {
                [[maybe_unused]] float saved_pct = total_full_bytes > 0 ?
                    100.0f * (1.0f - (float)total_dirty_bytes / (float)total_full_bytes) : 0;
                fprintf(stderr, "[VideoEncoder] Stats: %.1f FPS | dirty: rects=%d full=%d skip=%d | encode: %ld ms\n",
                        fps, dirty_rect_frames, full_frames, skipped_frames, last_encode_ms);
            } else {
                fprintf(stderr, "[VideoEncoder] Stats: %.1f FPS, %llu encoded, %llu dropped, encode: %ld ms\n",
                        fps, (unsigned long long)total_encoded, (unsigned long long)total_dropped, last_encode_ms);
            }

            last_stats_time = now;
            frames_since_stats = 0;
            dirty_rect_frames = 0;
            full_frames = 0;
            skipped_frames = 0;
            total_full_bytes = 0;
            total_dirty_bytes = 0;
        }
    }

    fprintf(stderr, "[VideoEncoder] Thread exiting\n");
}

} // namespace video
