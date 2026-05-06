/*
 * Video Output Implementation - Lock-free Triple Buffer
 */

#include "video_output.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>

VideoOutput::VideoOutput(int max_width, int max_height)
    : write_index(0)
    , ready_index(0)
    , frame_count(0)
    , dropped_frames(0)
    , last_read_sequence(0)
    , shutdown_requested(false)
    , max_width(max_width)
    , max_height(max_height)
{
    allocate_buffers();
}

VideoOutput::~VideoOutput() {
    free_buffers();
}

void VideoOutput::allocate_buffers() {
    size_t num_pixels = static_cast<size_t>(max_width) * max_height;

    for (int i = 0; i < VIDEO_NUM_BUFFERS; i++) {
        buffers[i].pixel_storage.resize(num_pixels, 0);
        buffers[i].pixels = buffers[i].pixel_storage.data();

        // Initialize metadata
        buffers[i].width = 0;
        buffers[i].height = 0;
        buffers[i].format = PIXFMT_BGRA;
        buffers[i].sequence = 0;
        buffers[i].timestamp_us = 0;
        buffers[i].dirty_x = 0;
        buffers[i].dirty_y = 0;
        buffers[i].dirty_width = 0;
        buffers[i].dirty_height = 0;
        buffers[i].cursor_x = 0;
        buffers[i].cursor_y = 0;
        buffers[i].cursor_visible = 0;
    }
}

void VideoOutput::free_buffers() {
    for (int i = 0; i < VIDEO_NUM_BUFFERS; i++) {
        buffers[i].pixel_storage.clear();
        buffers[i].pixels = nullptr;
    }
}

void VideoOutput::submit_frame(const uint32_t* pixels, int width, int height, PixelFormat format) {
    // Full frame update (no dirty rect)
    submit_frame_dirty(pixels, width, height, format, 0, 0, width, height);
}

void VideoOutput::submit_frame_dirty(const uint32_t* pixels, int width, int height, PixelFormat format,
                                     uint32_t dirty_x, uint32_t dirty_y,
                                     uint32_t dirty_width, uint32_t dirty_height) {
    // Validate dimensions
    if (width <= 0 || width > max_width || height <= 0 || height > max_height) {
        fprintf(stderr, "[VideoOutput] Invalid frame dimensions: %dx%d (max %dx%d)\n",
                width, height, max_width, max_height);
        return;
    }

    // Get current write buffer (atomic acquire)
    int idx = write_index.load(std::memory_order_acquire);

    // Update metadata
    FrameBuffer* buf = &buffers[idx];
    buf->width = width;
    buf->height = height;
    buf->format = format;
    buf->dirty_x = dirty_x;
    buf->dirty_y = dirty_y;
    buf->dirty_width = dirty_width;
    buf->dirty_height = dirty_height;

    // Copy pixel data
    size_t frame_size = width * height * 4;  // 4 bytes per pixel
    memcpy(buf->pixels, pixels, frame_size);

    // Update sequence and timestamp
    buf->sequence = frame_count.load(std::memory_order_relaxed) + 1;

    // Get current time (microseconds since epoch)
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    buf->timestamp_us = micros;

    // Advance write index (circular, atomic release)
    // This makes the frame visible to the encoder thread
    int next_idx = (idx + 1) % VIDEO_NUM_BUFFERS;
    write_index.store(next_idx, std::memory_order_release);

    // Update ready index to point to the just-completed frame
    // This is where the encoder should read from
    ready_index.store(idx, std::memory_order_release);

    // Increment frame count
    frame_count.fetch_add(1, std::memory_order_relaxed);

    // Wake encoder thread
    notify_frame();
}

void VideoOutput::set_cursor(uint16_t x, uint16_t y, bool visible) {
    // Update cursor position in current write buffer
    int idx = write_index.load(std::memory_order_acquire);
    buffers[idx].cursor_x = x;
    buffers[idx].cursor_y = y;
    buffers[idx].cursor_visible = visible ? 1 : 0;
}

const FrameBuffer* VideoOutput::wait_for_frame(int timeout_ms) {
    // Check for shutdown
    if (shutdown_requested.load(std::memory_order_acquire)) {
        return nullptr;
    }

    // Get ready index (atomic acquire - ensures we see all writes to the buffer)
    int idx = ready_index.load(std::memory_order_acquire);
    const FrameBuffer* buf = &buffers[idx];

    // Check if this is a new frame (sequence number increased)
    if (buf->sequence > last_read_sequence) {
        return buf;
    }

    // No new frame yet - wait strategy depends on timeout
    if (timeout_ms == 0) {
        return nullptr;
    }

    // Block on the QSemaphore until notify_frame() releases. The
    // counter accumulates across multiple writes — drain by acquiring
    // whatever's available before re-checking the buffer state. Same
    // semantics the eventfd-with-counter gave us, just portable.
    int remaining_ms = (timeout_ms < 0) ? -1 : timeout_ms;
    auto start = std::chrono::steady_clock::now();
    while (!shutdown_requested.load(std::memory_order_acquire)) {
        bool got = (remaining_ms < 0)
            ? (frame_sem.acquire(1), true)
            : frame_sem.tryAcquire(1, remaining_ms);
        // Drain any extra notifies so the next call doesn't return
        // immediately on a stale wakeup. Cheap — non-blocking polls
        // until the counter is zero.
        if (got) {
            while (frame_sem.tryAcquire(1, 0)) { /* drain */ }
        }

        idx = ready_index.load(std::memory_order_acquire);
        buf = &buffers[idx];
        if (buf->sequence > last_read_sequence) {
            return buf;
        }
        if (!got) return nullptr;  // tryAcquire timed out

        // Spurious wakeup (notify but no real new frame yet) — recompute
        // remaining timeout and try again.
        if (remaining_ms > 0) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            int spent = (int)std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            remaining_ms = timeout_ms - spent;
            if (remaining_ms <= 0) return nullptr;
        }
    }
    return nullptr;
}

void VideoOutput::release_frame() {
    // Update last read sequence to mark this frame as consumed
    int idx = ready_index.load(std::memory_order_acquire);
    const FrameBuffer* buf = &buffers[idx];
    last_read_sequence = buf->sequence;
}

bool VideoOutput::snapshot_frame(uint32_t* out_pixels, int* out_width, int* out_height, PixelFormat* out_format) {
    int idx = ready_index.load(std::memory_order_acquire);
    const FrameBuffer* buf = &buffers[idx];

    // No frame submitted yet
    if (buf->sequence == 0) {
        return false;
    }

    *out_width = buf->width;
    *out_height = buf->height;
    *out_format = buf->format;
    memcpy(out_pixels, buf->pixels, buf->width * buf->height * 4);
    return true;
}

bool VideoOutput::snapshot_frame_ex(uint32_t* out_pixels, int* out_width, int* out_height, PixelFormat* out_format,
                                    uint64_t* out_sequence, uint16_t* out_cursor_x, uint16_t* out_cursor_y,
                                    uint8_t* out_cursor_visible) {
    int idx = ready_index.load(std::memory_order_acquire);
    const FrameBuffer* buf = &buffers[idx];

    if (buf->sequence == 0) {
        return false;
    }

    *out_width = buf->width;
    *out_height = buf->height;
    *out_format = buf->format;
    *out_sequence = buf->sequence;
    *out_cursor_x = buf->cursor_x;
    *out_cursor_y = buf->cursor_y;
    *out_cursor_visible = buf->cursor_visible;
    memcpy(out_pixels, buf->pixels, buf->width * buf->height * 4);
    return true;
}

void VideoOutput::get_stats(uint64_t* out_total_frames, uint64_t* out_dropped_frames) {
    if (out_total_frames) {
        *out_total_frames = frame_count.load(std::memory_order_relaxed);
    }
    if (out_dropped_frames) {
        *out_dropped_frames = dropped_frames.load(std::memory_order_relaxed);
    }
}

void VideoOutput::notify_frame() {
    frame_sem.release();
}

void VideoOutput::shutdown() {
    shutdown_requested.store(true, std::memory_order_release);
    notify_frame();  // Wake encoder thread so it sees the shutdown
}
