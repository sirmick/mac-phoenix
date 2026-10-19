/*
 * PNG Encoder (zlib, png_write.cpp)
 */

#include "png_encoder.h"
#include "png_write.h"
#include <cstring>
#include <cstdio>

// libyuv for fast I420 to RGB conversion
#include <libyuv.h>

// Debug flags (from server.cpp)
extern bool g_debug_mode_switch;
extern bool g_debug_png;

bool PNGEncoder::init(int width, int height, int fps) {
    cleanup();

    width_ = width;
    height_ = height;
    fps_ = fps;

    // Pre-allocate RGB buffer
    rgb_buffer_.resize(width * height * 3);  // RGB24

    if (g_debug_png) {
        fprintf(stderr, "PNG: Encoder initialized %dx%d\n", width, height);
    }
    return true;
}

void PNGEncoder::cleanup() {
    rgb_buffer_.clear();
    png_buffer_.clear();
    width_ = 0;
    height_ = 0;
    frame_count_ = 0;
    total_size_ = 0;
}

void PNGEncoder::i420_to_rgb(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                              int width, int height, int y_stride, int uv_stride) {
    // Use libyuv for fast conversion
    // PNG wants RGB (R first), so use I420ToRAW
    libyuv::I420ToRAW(
        y, y_stride,
        u, uv_stride,
        v, uv_stride,
        rgb_buffer_.data(), width * 3,
        width, height
    );
}

bool PNGEncoder::encode_rgb_to_png(int width, int height) {
    if (!png_encode_rgb(rgb_buffer_.data(), width, height, png_buffer_)) {
        if (g_debug_png) {
            fprintf(stderr, "PNG: encode failed\n");
        }
        return false;
    }
    return true;
}

EncodedFrame PNGEncoder::encode_i420(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                                      int width, int height, int y_stride, int uv_stride) {
    EncodedFrame result;
    result.codec = CodecType::PNG;
    result.width = width;
    result.height = height;
    result.is_keyframe = true;  // PNG frames are always keyframes

    // Reinit if size changed
    if (width != width_ || height != height_) {
        if (!init(width, height)) {
            return result;
        }
    }

    // Convert I420 to RGB
    i420_to_rgb(y, u, v, width, height, y_stride, uv_stride);

    // Encode to PNG
    if (!encode_rgb_to_png(width, height)) {
        return result;
    }

    result.data = std::move(png_buffer_);
    png_buffer_.clear();  // Reset after move

    // Stats
    frame_count_++;
    total_size_ += result.data.size();

    // Log every 30 frames (only if debug enabled)
    if (g_debug_png && frame_count_ % 30 == 0) {
        float avg_size = static_cast<float>(total_size_) / frame_count_;
        fprintf(stderr, "PNG: frame=%d size=%zu bytes (avg %.1f KB)\n",
                frame_count_, result.data.size(), avg_size / 1024.0f);
    }

    return result;
}

EncodedFrame PNGEncoder::encode_bgra(const uint8_t* bgra, int width, int height, int stride) {
    EncodedFrame result;
    result.codec = CodecType::PNG;
    result.width = width;
    result.height = height;
    result.is_keyframe = true;

    // Reinit if size changed
    if (width != width_ || height != height_) {
        if (!init(width, height)) {
            return result;
        }
    }

    // Convert BGRA to RGB using libyuv
    // BGRA = bytes B,G,R,A = libyuv "ARGB"
    // ARGBToRAW converts to RGB
    libyuv::ARGBToRAW(
        bgra, stride,
        rgb_buffer_.data(), width * 3,
        width, height
    );

    // Encode to PNG
    if (!encode_rgb_to_png(width, height)) {
        return result;
    }

    result.data = std::move(png_buffer_);
    png_buffer_.clear();

    frame_count_++;
    total_size_ += result.data.size();

    return result;
}

EncodedFrame PNGEncoder::encode_argb(const uint8_t* argb, int width, int height, int stride) {
    EncodedFrame result;
    result.codec = CodecType::PNG;
    result.width = width;
    result.height = height;
    result.is_keyframe = true;

    // Reinit if size changed
    if (width != width_ || height != height_) {
        if (!init(width, height)) {
            return result;
        }
    }

    // Convert ARGB to RGB manually
    // ARGB = bytes A,R,G,B in memory (Mac native 32-bit)
    // RGB = bytes R,G,B in memory
    // libyuv doesn't have a direct function for this, so we do it manually
    uint8_t* dst = rgb_buffer_.data();
    for (int row = 0; row < height; row++) {
        const uint8_t* src_row = argb + row * stride;
        uint8_t* dst_row = dst + row * width * 3;
        for (int col = 0; col < width; col++) {
            // ARGB bytes: A, R, G, B at offsets 0, 1, 2, 3
            dst_row[col * 3 + 0] = src_row[col * 4 + 1];  // R
            dst_row[col * 3 + 1] = src_row[col * 4 + 2];  // G
            dst_row[col * 3 + 2] = src_row[col * 4 + 3];  // B
        }
    }

    // Encode to PNG
    if (!encode_rgb_to_png(width, height)) {
        return result;
    }

    result.data = std::move(png_buffer_);
    png_buffer_.clear();

    frame_count_++;
    total_size_ += result.data.size();

    return result;
}

EncodedFrame PNGEncoder::encode_bgra_rect(const uint8_t* bgra, int /*frame_width*/, int /*frame_height*/, int stride,
                                           int rect_x, int rect_y, int rect_width, int rect_height) {
    EncodedFrame result;
    result.codec = CodecType::PNG;
    result.width = rect_width;
    result.height = rect_height;
    result.is_keyframe = true;

    // Allocate RGB buffer for the rectangle
    std::vector<uint8_t> rect_rgb(rect_width * rect_height * 3);

    // Extract and convert the rectangle from BGRA to RGB
    for (int y = 0; y < rect_height; y++) {
        const uint8_t* src_row = bgra + (rect_y + y) * stride + rect_x * 4;
        uint8_t* dst_row = rect_rgb.data() + y * rect_width * 3;

        for (int x = 0; x < rect_width; x++) {
            // BGRA = bytes B,G,R,A at offsets 0,1,2,3
            // RGB = bytes R,G,B
            dst_row[x * 3 + 0] = src_row[x * 4 + 2];  // R
            dst_row[x * 3 + 1] = src_row[x * 4 + 1];  // G
            dst_row[x * 3 + 2] = src_row[x * 4 + 0];  // B
        }
    }

    if (!png_encode_rgb(rect_rgb.data(), rect_width, rect_height, png_buffer_)) {
        if (g_debug_png) {
            fprintf(stderr, "PNG: encode_rect failed\n");
        }
        return result;
    }

    result.data = std::move(png_buffer_);
    png_buffer_.clear();

    frame_count_++;
    total_size_ += result.data.size();

    return result;
}
