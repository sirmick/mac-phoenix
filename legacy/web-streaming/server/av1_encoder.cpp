/*
 * AV1 Encoder using SVT-AV1
 */

#include "av1_encoder.h"
#include <cstdio>
#include <cstring>
#include <libyuv.h>

// Debug flags (from server.cpp)
extern bool g_debug_mode_switch;

bool AV1Encoder::init(int width, int height, int fps) {
    cleanup();

    // STEP 1: Create encoder handle - this loads config_ with default parameters
    EbErrorType res = svt_av1_enc_init_handle(&encoder_, nullptr, &config_);
    if (res != EB_ErrorNone) {
        fprintf(stderr, "AV1: Failed to create encoder handle (error %d)\n", res);
        return false;
    }

    // STEP 2: Configure encoder for low-latency real-time encoding
    // Now that we have defaults, override them with our settings
    config_.source_width = width;
    config_.source_height = height;
    config_.frame_rate_numerator = fps;
    config_.frame_rate_denominator = 1;

    // Encoder preset: 8 = fast, good quality (0=slowest/best, 13=fastest/worst)
    config_.enc_mode = 8;

    // Rate control: CQP mode for predictable latency
    config_.rate_control_mode = 0;  // SVT_AV1_RC_MODE_CQP_OR_CRF
    config_.qp = 35;  // Quality: 0=lossless, 63=worst (35=good balance for dithered content)
    config_.target_bit_rate = 2000000;  // Not used in CQP mode

    // Low-latency settings
    config_.intra_period_length = fps * 5;  // Keyframe every 5 seconds
    config_.intra_refresh_type = SVT_AV1_KF_REFRESH;  // Regular keyframes
    config_.hierarchical_levels = 0;  // Disable temporal layers for lowest latency
    config_.pred_structure = SVT_AV1_PRED_LOW_DELAY_B;  // Low-delay B mode (lowest latency)

    // Threading
    config_.logical_processors = 0;  // Auto-detect
    config_.target_socket = -1;

    // Quality/speed tradeoffs for real-time
    config_.tile_rows = 0;  // Auto
    config_.tile_columns = 0;  // Auto
    config_.enable_adaptive_quantization = 0;  // Must be 0 for CQP mode
    config_.film_grain_denoise_strength = 0;  // Disable film grain (we have real dither patterns)

    // STEP 3: Apply the configured parameters to the encoder
    res = svt_av1_enc_set_parameter(encoder_, &config_);
    if (res != EB_ErrorNone) {
        fprintf(stderr, "AV1: Failed to set encoder parameters (error %d)\n", res);
        cleanup();
        return false;
    }

    // Initialize encoder
    res = svt_av1_enc_init(encoder_);
    if (res != EB_ErrorNone) {
        fprintf(stderr, "AV1: Failed to initialize encoder (error %d)\n", res);
        cleanup();
        return false;
    }

    // Allocate input buffer structures (reused for all frames)
    input_buffer_ = (EbBufferHeaderType*)malloc(sizeof(EbBufferHeaderType));
    if (!input_buffer_) {
        fprintf(stderr, "AV1: Failed to allocate input buffer header\n");
        cleanup();
        return false;
    }
    memset(input_buffer_, 0, sizeof(EbBufferHeaderType));
    input_buffer_->size = sizeof(EbBufferHeaderType);

    input_picture_ = (EbSvtIOFormat*)malloc(sizeof(EbSvtIOFormat));
    if (!input_picture_) {
        fprintf(stderr, "AV1: Failed to allocate input picture structure\n");
        cleanup();
        return false;
    }
    memset(input_picture_, 0, sizeof(EbSvtIOFormat));

    // Link the picture to the buffer header (permanent association)
    input_buffer_->p_buffer = (uint8_t*)input_picture_;

    width_ = width;
    height_ = height;
    fps_ = fps;
    frame_count_ = 0;

    if (g_debug_mode_switch) {
        fprintf(stderr, "AV1: Encoder initialized %dx%d @ %d fps (preset %d, QP %d)\n",
                width, height, fps, config_.enc_mode, config_.qp);
    }

    return true;
}

void AV1Encoder::cleanup() {
    // Send EOS (End of Stream) signal before deinit
    if (encoder_ && frame_count_ > 0) {
        EbBufferHeaderType eos_buffer;
        memset(&eos_buffer, 0, sizeof(eos_buffer));
        eos_buffer.pic_type = EB_AV1_INVALID_PICTURE;
        eos_buffer.flags = EB_BUFFERFLAG_EOS;
        svt_av1_enc_send_picture(encoder_, &eos_buffer);

        // Flush remaining packets
        EbBufferHeaderType* output_buffer = nullptr;
        while (svt_av1_enc_get_packet(encoder_, &output_buffer, 1) == EB_ErrorNone) {
            if (output_buffer) {
                svt_av1_enc_release_out_buffer(&output_buffer);
            }
        }
    }

    // Free input buffers
    if (input_picture_) {
        free(input_picture_);
        input_picture_ = nullptr;
    }
    if (input_buffer_) {
        free(input_buffer_);
        input_buffer_ = nullptr;
    }

    // Deinitialize encoder
    if (encoder_) {
        svt_av1_enc_deinit(encoder_);
        svt_av1_enc_deinit_handle(encoder_);
        encoder_ = nullptr;
    }
    frame_count_ = 0;
}

EncodedFrame AV1Encoder::encode_i420(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                                      int width, int height, int y_stride, int uv_stride) {
    EncodedFrame result;
    result.codec = CodecType::AV1;
    result.width = width;
    result.height = height;
    result.is_keyframe = false;

    if (!encoder_ || width != width_ || height != height_) {
        if (!init(width, height)) {
            return result;
        }
        force_keyframe_ = true;
    }

    // Fill in the input picture structure with pointers to our I420 data
    input_picture_->width = width;
    input_picture_->height = height;
    input_picture_->luma = const_cast<uint8_t*>(y);
    input_picture_->cb = const_cast<uint8_t*>(u);
    input_picture_->cr = const_cast<uint8_t*>(v);
    input_picture_->y_stride = y_stride;
    input_picture_->cb_stride = uv_stride;
    input_picture_->cr_stride = uv_stride;

    // Fill in the input buffer header
    input_buffer_->pts = frame_count_;
    input_buffer_->p_app_private = nullptr;
    input_buffer_->n_filled_len = 0;  // Encoder doesn't use this for input
    input_buffer_->n_alloc_len = 0;
    input_buffer_->flags = 0;

    // Set frame type
    if (force_keyframe_) {
        input_buffer_->pic_type = EB_AV1_KEY_PICTURE;
        force_keyframe_ = false;
        fprintf(stderr, "AV1: Forcing keyframe\n");
    } else {
        input_buffer_->pic_type = EB_AV1_INVALID_PICTURE;  // Let encoder decide
    }

    // Send frame to encoder
    EbErrorType res = svt_av1_enc_send_picture(encoder_, input_buffer_);
    if (res != EB_ErrorNone) {
        fprintf(stderr, "AV1: Failed to send picture (error %d)\n", res);
        return result;
    }

    frame_count_++;

    // Get encoded output (non-blocking)
    // Note: SVT-AV1 has encoding delay - first few frames won't produce output immediately
    EbBufferHeaderType* output_buffer = nullptr;
    res = svt_av1_enc_get_packet(encoder_, &output_buffer, 0);  // 0 = non-blocking

    if (res == EB_ErrorMax || res == EB_NoErrorEmptyQueue) {
        // End of stream or no output yet (normal for first few frames)
        return result;
    }

    if (res != EB_ErrorNone || !output_buffer) {
        // Only log if it's not the "insufficient resources" error (encoder delay)
        if (res != (EbErrorType)0x80001003) {  // EB_ErrorInsufficientResources
            fprintf(stderr, "AV1: Failed to get output packet (error %d)\n", res);
        }
        return result;
    }

    if (output_buffer->n_filled_len > 0) {
        // Copy encoded data
        result.data.assign(
            output_buffer->p_buffer,
            output_buffer->p_buffer + output_buffer->n_filled_len
        );

        result.is_keyframe = (output_buffer->pic_type == EB_AV1_KEY_PICTURE);

        // Log frame info
        static int keyframe_count = 0;
        static int p_frame_count = 0;
        static int64_t p_size_total = 0;

        if (result.is_keyframe) {
            keyframe_count++;
            fprintf(stderr, "AV1: Keyframe %llu, size=%zu bytes (%.1f KB)\n",
                    (unsigned long long)frame_count_, result.data.size(), result.data.size() / 1024.0f);
        } else {
            p_frame_count++;
            p_size_total += result.data.size();
        }

        // Log stats every 90 frames
        if (frame_count_ % 90 == 0) {
            int avg_p = p_frame_count > 0 ? (int)(p_size_total / p_frame_count) : 0;
            fprintf(stderr, "AV1: Frame stats - total=%llu keyframes=%d P=%d avg_p=%d bytes (%.1f KB)\n",
                    (unsigned long long)frame_count_, keyframe_count, p_frame_count, avg_p, avg_p / 1024.0f);
        }

        // Release output buffer
        svt_av1_enc_release_out_buffer(&output_buffer);
    }

    return result;
}

EncodedFrame AV1Encoder::encode_bgra(const uint8_t* bgra, int width, int height, int stride) {
    // Convert BGRA to I420
    size_t y_size = width * height;
    size_t uv_size = (width / 2) * (height / 2);
    size_t total_size = y_size + 2 * uv_size;

    if (i420_buffer_.size() < total_size) {
        i420_buffer_.resize(total_size);
    }

    uint8_t* y = i420_buffer_.data();
    uint8_t* u = y + y_size;
    uint8_t* v = u + uv_size;

    // BGRA to I420 conversion using libyuv
    libyuv::ARGBToI420(
        bgra, stride,
        y, width,
        u, width / 2,
        v, width / 2,
        width, height
    );

    return encode_i420(y, u, v, width, height, width, width / 2);
}
