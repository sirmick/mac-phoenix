/*
 * Video Encoder Thread - In-Process Architecture
 *
 * Reads frames from VideoOutput triple buffer and encodes to H.264/VP9/WebP/PNG.
 * Handles codec changes dynamically by reinitializing encoder.
 *
 * When an IPCBuffer* is provided (subprocess mode), reads frames directly from
 * IPC shared memory — zero-copy, eliminating the relay thread's memcpy.
 * VideoOutput is still used for screenshot API (snapshot_frame).
 *
 * For PNG/WebP (DataChannel codecs): computes dirty rectangles by comparing
 * current frame against previous frame, encodes only the changed region.
 * This reduces frame size from ~280KB to typically 5-50KB for static UI.
 *
 * Thread Safety:
 * - Reads from VideoOutput or IPC SHM (lock-free triple buffer)
 * - Sends encoded frames to WebRTC (thread-safe queue)
 * - Checks config for codec changes (atomic read)
 */

#include "video_encoder_thread.h"
#include "video_output.h"
#include "../../config/emulator_config.h"
#include "../../ipc/ipc_client.h"   // For g_ipc_shm_mutex
#include "../../ipc/ipc_protocol.h"
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
#include <shared_mutex>
#include <thread>
#include <libyuv.h>
#ifdef __linux__
#include <poll.h>
#include <unistd.h>
#endif

namespace video {

// External globals (to be replaced with proper dependency injection later)
extern std::atomic<bool> g_running;
extern std::atomic<bool> g_request_keyframe;

// Statistics
static std::atomic<uint64_t> g_frames_encoded(0);
static std::atomic<uint64_t> g_frames_dropped(0);

/**
 * Create encoder based on codec type.
 * Falls back to PNG if the requested codec was not compiled in.
 */
static std::unique_ptr<VideoCodec> create_video_encoder(CodecType codec) {
    switch (codec) {
#ifdef HAVE_OPENH264
        case CodecType::H264:
            fprintf(stderr, "[VideoEncoder] Creating H.264 encoder\n");
            return std::make_unique<H264Encoder>();
#endif

#ifdef HAVE_VPX
        case CodecType::VP9:
            fprintf(stderr, "[VideoEncoder] Creating VP9 encoder\n");
            return std::make_unique<VP9Encoder>();
#endif

#ifdef HAVE_LIBWEBP
        case CodecType::WEBP:
            fprintf(stderr, "[VideoEncoder] Creating WebP encoder\n");
            return std::make_unique<WebPEncoder>();
#endif

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
#ifdef HAVE_LIBWEBP
            auto* webp_enc = dynamic_cast<WebPEncoder*>(encoder);
#endif

            if (format == PIXFMT_BGRA) {
                if (png_enc)
                    strip = png_enc->encode_bgra_rect(
                        reinterpret_cast<const uint8_t*>(pixels), frame_w, frame_h, frame_w * 4,
                        rect_x, sy, rect_w, sh);
#ifdef HAVE_LIBWEBP
                else if (webp_enc)
                    strip = webp_enc->encode_bgra_rect(
                        reinterpret_cast<const uint8_t*>(pixels), frame_w, frame_h, frame_w * 4,
                        rect_x, sy, rect_w, sh);
#endif
            } else {
                // ARGB (bytes A,R,G,B = libyuv "BGRA") → BGRA (bytes B,G,R,A = libyuv "ARGB")
                // Use libyuv for SIMD-accelerated conversion instead of byte-at-a-time loop
                std::vector<uint8_t> strip_bgra(rect_w * sh * 4);
                for (int ry = 0; ry < sh; ry++) {
                    const uint8_t* src = reinterpret_cast<const uint8_t*>(pixels) + (sy + ry) * frame_w * 4 + rect_x * 4;
                    uint8_t* dst = strip_bgra.data() + ry * rect_w * 4;
                    libyuv::BGRAToARGB(src, rect_w * 4, dst, rect_w * 4, rect_w, 1);
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
 * @param video_output Triple buffer for screenshots (always needed)
 * @param config Configuration (for codec selection)
 * @param ipc_shm Optional IPC shared memory — when set, read frames directly
 *                from SHM (zero-copy) instead of going through VideoOutput.
 */
void video_encoder_main(VideoOutput* video_output, config::EmulatorConfig* config,
                        std::atomic<IPCBuffer*>* ipc_shm_ptr,
                        std::atomic<int>* ipc_eventfd_ptr) {
    fprintf(stderr, "[VideoEncoder] Thread starting%s\n",
            ipc_shm_ptr ? " (IPC zero-copy capable)" : "");

    // Debug flags
    static bool debug_frames = (getenv("MACEMU_DEBUG_FRAMES") != nullptr);
    [[maybe_unused]] static bool debug_perf = config ? config->debug_perf : (getenv("MACEMU_DEBUG_PERF") != nullptr);

    // IPC direct-read state (reset on each subprocess restart)
    uint64_t ipc_last_frame_count = 0;
    int ipc_eventfd = -1;
    bool ipc_logged = false;
    bool ipc_was_connected = false;  // Track IPC transitions for reset

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
    int enc_w = 0, enc_h = 0;

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

        // ── Frame acquisition: IPC zero-copy or VideoOutput ─────────
        const uint32_t* pixels = nullptr;
        int w = 0, h = 0;
        PixelFormat format = PIXFMT_ARGB;
        bool from_ipc = false;

        // Deferred shared lock — only engaged for IPC path, persists
        // through encoding so disconnect() can't munmap the SHM page
        // while we're reading pixel data from it.
        std::shared_lock<std::shared_mutex> ipc_lock(g_ipc_shm_mutex, std::defer_lock);

        // Phase 1: Check IPC availability and wait for frame (NO LOCK).
        // The atomic pointer check is lock-free. Poll/sleep happens
        // outside the shared lock so disconnect() isn't blocked for 16ms.
        IPCBuffer* ipc_shm = ipc_shm_ptr ? ipc_shm_ptr->load(std::memory_order_acquire) : nullptr;
        if (ipc_shm) {
            // Detect subprocess restart: SHM went away and came back
            if (!ipc_was_connected) {
                fprintf(stderr, "[VideoEncoder] IPC (re)connected — resetting encoder state\n");
                ipc_last_frame_count = 0;
                ipc_eventfd = -1;
                ipc_logged = false;
                have_prev_frame = false;
                encoder_initialized = false;
                ipc_was_connected = true;
                g_request_keyframe.store(true, std::memory_order_release);
            }

            // Pick up parent's eventfd on first IPC connection
            if (ipc_eventfd < 0 && ipc_eventfd_ptr) {
                ipc_eventfd = ipc_eventfd_ptr->load(std::memory_order_acquire);
                if (ipc_eventfd >= 0 && !ipc_logged) {
                    fprintf(stderr, "[VideoEncoder] IPC zero-copy active, eventfd=%d\n", ipc_eventfd);
                    ipc_logged = true;
                }
            }

            // Wait for frame notification (no lock held — disconnect() can proceed)
#ifdef __linux__
            if (ipc_eventfd >= 0) {
                struct pollfd pfd = { ipc_eventfd, POLLIN, 0 };
                int ret = poll(&pfd, 1, 16);  // 16ms = ~60fps
                if (ret > 0) {
                    uint64_t val;
                    ssize_t ignored = read(ipc_eventfd, &val, sizeof(val));
                    (void)ignored;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
#else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif

            // Phase 2: Acquire shared lock and read frame from SHM.
            // The lock prevents disconnect() from munmapping the page
            // while we dereference it. Held through encoding below.
            ipc_lock.lock();
            ipc_shm = ipc_shm_ptr->load(std::memory_order_acquire);
            if (!ipc_shm) {
                ipc_was_connected = false;
                ipc_eventfd = -1;
                continue;
            }

            uint64_t fc = IPC_ATOMIC_LOAD(ipc_shm->frame_count);
            if (fc <= ipc_last_frame_count) continue;
            ipc_last_frame_count = fc;

            uint32_t idx = IPC_ATOMIC_LOAD(ipc_shm->ready_index);
            if (idx >= IPC_NUM_BUFFERS) continue;

            w = ipc_shm->width;
            h = ipc_shm->height;
            if (w <= 0 || h <= 0) continue;

            pixels = reinterpret_cast<const uint32_t*>(ipc_shm->frames[idx]);
            format = static_cast<PixelFormat>(ipc_shm->pixel_format);
            from_ipc = true;

            // Update VideoOutput for screenshot API (this is the only remaining copy)
            video_output->submit_frame(pixels, w, h, format);
        } else {
            // IPC disconnected — mark so we detect reconnection
            ipc_was_connected = false;

            // In-process: read from VideoOutput triple buffer
            const FrameBuffer* frame = video_output->wait_for_frame(16);  // 16ms timeout

            if (!frame) {
                if (debug_frames && frames_since_stats == 0) {
                    fprintf(stderr, "[VideoEncoder] No frame available (timeout)\n");
                }
                continue;
            }

            w = frame->width;
            h = frame->height;
            pixels = frame->pixels;
            format = frame->format;
        }

        if (debug_frames) {
            fprintf(stderr, "[VideoEncoder] Received frame %dx%d format=%d%s\n",
                    w, h, (int)format, from_ipc ? " (IPC)" : "");
        }

        // Detect resolution change — reinitialize encoder
        if (encoder_initialized && (w != enc_w || h != enc_h)) {
            fprintf(stderr, "[VideoEncoder] Resolution changed %dx%d -> %dx%d, reinitializing\n",
                    enc_w, enc_h, w, h);
            encoder_initialized = false;
            have_prev_frame = false;
            g_request_keyframe.store(true, std::memory_order_release);
        }

        // Initialize encoder on first frame (need width/height)
        if (!encoder_initialized) {
            if (encoder->init(w, h, 60)) {
                fprintf(stderr, "[VideoEncoder] Initialized %dx%d @ 60 FPS\n", w, h);
                encoder_initialized = true;
                enc_w = w;
                enc_h = h;
            } else {
                fprintf(stderr, "[VideoEncoder] ERROR: Failed to initialize encoder\n");
                if (!from_ipc) video_output->release_frame();
                continue;
            }
        }

        // Check if keyframe requested
        bool keyframe_requested = g_request_keyframe.exchange(false, std::memory_order_acq_rel);
        if (keyframe_requested) {
            encoder->request_keyframe();
            fprintf(stderr, "[VideoEncoder] Keyframe requested\n");
        }

        const bool is_dc_codec = (current_codec == CodecType::PNG || current_codec == CodecType::WEBP);

        // ── Dirty rect path (PNG/WebP only) ───────────────────────────
        if (is_dc_codec && have_prev_frame && !keyframe_requested
            && (int)prev_frame.size() == w * h) {

            // IPC mode has no dirty rect hints — always compute
            int dx, dy, dw, dh;
            bool changed = compute_dirty_rect(pixels, prev_frame.data(), w, h, dx, dy, dw, dh);

            if (!changed) {
                // No changes — skip this frame entirely
                skipped_frames++;
                if (!from_ipc) video_output->release_frame();
                continue;
            }

            // Encode the dirty rectangle
            auto encode_start = std::chrono::steady_clock::now();

            // Encode rect (may split into strips if too large for DataChannel)
            int sent = encode_and_send_strips(encoder.get(), pixels, w, h,
                                               format, dx, dy, dw, dh);
            frames_since_stats += sent;
            dirty_rect_frames++;

            auto encode_end = std::chrono::steady_clock::now();
            last_encode_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                encode_end - encode_start).count();

            // Save current frame for next comparison (fallback path needs it)
            memcpy(prev_frame.data(), pixels, w * h * 4);
            if (!from_ipc) video_output->release_frame();

        } else if (is_dc_codec) {
            // ── First/keyframe DC frame: send as strips to fit within DC size limit ──
            auto encode_start = std::chrono::steady_clock::now();

            int sent = encode_and_send_strips(encoder.get(), pixels, w, h,
                                               format, 0, 0, w, h);
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

            if (!from_ipc) video_output->release_frame();

        } else {
            // ── Full frame path (H264/VP9) ──────────────────────────────

            auto encode_start = std::chrono::steady_clock::now();
            EncodedFrame encoded;

            if (format == PIXFMT_BGRA) {
                encoded = encoder->encode_bgra(reinterpret_cast<const uint8_t*>(pixels),
                                              w, h, w * 4);
            } else {
                encoded = encoder->encode_argb(reinterpret_cast<const uint8_t*>(pixels),
                                              w, h, w * 4);
            }

            auto encode_end = std::chrono::steady_clock::now();
            last_encode_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                encode_end - encode_start).count();

            if (!from_ipc) video_output->release_frame();

            if (encoded.data.size() > 0) {
                send_encoded_frame(encoded);
                frames_since_stats++;
            } else {
                fprintf(stderr, "[VideoEncoder] WARNING: Encoding produced empty frame\n");
            }
        }

        // Print statistics every 3 seconds
        auto now = std::chrono::steady_clock::now();
        auto stats_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_stats_time).count();

        if (stats_elapsed >= 3) {
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
