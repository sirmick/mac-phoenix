/*
 * HTTP Video Stream Handler
 *
 * Streams video frames over plain HTTP chunked transfer encoding.
 * Each frame is length-prefixed with the same 45-byte metadata header
 * used by the DataChannel path (timestamps, dirty rect, cursor position).
 *
 * Wire format per frame:
 *   [4-byte total_length (LE uint32)] [45-byte header] [PNG image data]
 *
 * The 45-byte header:
 *   Offset  Size  Field
 *   0       8     t1_frame_ready (ms since epoch, LE uint64)
 *   8       4     dirty_x (LE uint32)
 *   12      4     dirty_y (LE uint32)
 *   16      4     dirty_width (LE uint32)
 *   20      4     dirty_height (LE uint32)
 *   24      4     frame_width (LE uint32)
 *   28      4     frame_height (LE uint32)
 *   32      8     t4_send_time (ms since epoch, LE uint64)
 *   40      2     cursor_x (LE uint16)
 *   42      2     cursor_y (LE uint16)
 *   44      1     cursor_visible (uint8)
 *
 * Wrapped in HTTP chunked encoding: "hex_size\r\n[payload]\r\n"
 */

#include "http_stream.h"
#include "../drivers/video/video_output.h"
#include "../drivers/video/encoders/png_encoder.h"
#include "../drivers/video/encoders/codec.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <thread>
#include <vector>

namespace http {

// Header size: 8+4+4+4+4+4+4+8+2+2+1 = 45 bytes
static constexpr int HEADER_SIZE = 45;

// Length prefix size
static constexpr int LENGTH_PREFIX_SIZE = 4;

/**
 * Compute dirty rectangle by comparing current frame against previous frame.
 * Returns true if changes found, false if frames are identical.
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

    // Add 1-pixel margin for PNG filter artifacts
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
 * Parse a simple query param value from a query string.
 * e.g., parse_query_param("fps=15&codec=png", "fps") returns "15"
 */
static std::string parse_query_param(const std::string& query, const std::string& key) {
    std::string search = key + "=";
    size_t pos = query.find(search);
    if (pos == std::string::npos) return "";

    size_t start = pos + search.size();
    size_t end = query.find('&', start);
    if (end == std::string::npos) end = query.size();
    return query.substr(start, end - start);
}

/**
 * Send all bytes over a socket. Returns false on error (client disconnected).
 */
static bool send_all(int fd, const void* data, size_t len) {
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t sent = send(fd, ptr, remaining, MSG_NOSIGNAL);
        if (sent <= 0) {
            return false;  // Client disconnected or error
        }
        ptr += sent;
        remaining -= sent;
    }
    return true;
}

/**
 * Get current time in milliseconds since epoch.
 */
static uint64_t now_ms() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

/**
 * Write a little-endian uint32 into a buffer.
 */
static void write_le32(uint8_t* buf, uint32_t val) {
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
    buf[2] = (val >> 16) & 0xFF;
    buf[3] = (val >> 24) & 0xFF;
}

/**
 * Write a little-endian uint64 into a buffer.
 */
static void write_le64(uint8_t* buf, uint64_t val) {
    write_le32(buf, static_cast<uint32_t>(val));
    write_le32(buf + 4, static_cast<uint32_t>(val >> 32));
}

/**
 * Write a little-endian uint16 into a buffer.
 */
static void write_le16(uint8_t* buf, uint16_t val) {
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
}

void handle_stream(const Request& req, int client_fd, APIContext* ctx) {
    fprintf(stderr, "[HTTPStream] Client connected (fd=%d)\n", client_fd);

    // Set TCP_NODELAY for low latency
    int flag = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // Parse query parameters
    int target_fps = 30;
    std::string fps_str = parse_query_param(req.query, "fps");
    if (!fps_str.empty()) {
        int parsed = std::stoi(fps_str);
        if (parsed >= 1 && parsed <= 60) target_fps = parsed;
    }

    auto frame_interval = std::chrono::milliseconds(1000 / target_fps);

    // Check video output availability
    if (!ctx || !ctx->video_output) {
        fprintf(stderr, "[HTTPStream] No video output available, closing\n");
        close(client_fd);
        return;
    }

    VideoOutput* video_output = ctx->video_output;

    // Per-client state
    PNGEncoder encoder;
    bool encoder_initialized = false;

    std::vector<uint32_t> curr_frame(1920 * 1080);
    std::vector<uint32_t> prev_frame;
    bool have_prev_frame = false;
    uint64_t last_sequence = 0;

    // Reusable send buffer: [4-byte length] [45-byte header] [image data]
    std::vector<uint8_t> send_buf;

    fprintf(stderr, "[HTTPStream] Streaming at %d FPS\n", target_fps);

    // Heartbeat: send a 1-byte keepalive chunk every second when idle.
    // This prevents proxies from timing out the connection.
    // The client recognizes a frame with totalLen=0 as a heartbeat and ignores it.
    auto last_send_time = std::chrono::steady_clock::now();
    const auto heartbeat_interval = std::chrono::seconds(1);

    while (true) {
        auto frame_start = std::chrono::steady_clock::now();

        // Snapshot the current frame with metadata
        int width = 0, height = 0;
        PixelFormat format;
        uint64_t sequence = 0;
        uint16_t cursor_x = 0, cursor_y = 0;
        uint8_t cursor_visible = 0;

        bool got_frame = video_output->snapshot_frame_ex(
            curr_frame.data(), &width, &height, &format,
            &sequence, &cursor_x, &cursor_y, &cursor_visible);

        if (!got_frame || sequence == last_sequence) {
            // No new frame — send heartbeat if idle too long
            auto since_send = std::chrono::steady_clock::now() - last_send_time;
            if (since_send >= heartbeat_interval) {
                // Send a keepalive: 4 bytes of zeros (totalLen=0, client skips it)
                uint8_t heartbeat[4] = {0, 0, 0, 0};
                char hb_chunk_header[16];
                int hb_hdr_len = snprintf(hb_chunk_header, sizeof(hb_chunk_header), "4\r\n");
                if (!send_all(client_fd, hb_chunk_header, hb_hdr_len)) break;
                if (!send_all(client_fd, heartbeat, 4)) break;
                if (!send_all(client_fd, "\r\n", 2)) break;
                last_send_time = std::chrono::steady_clock::now();
            }

            auto elapsed = std::chrono::steady_clock::now() - frame_start;
            auto remaining = frame_interval - elapsed;
            if (remaining.count() > 0) {
                std::this_thread::sleep_for(remaining);
            }
            continue;
        }

        last_sequence = sequence;
        uint64_t t1 = now_ms();

        // Initialize encoder on first frame
        if (!encoder_initialized) {
            if (!encoder.init(width, height, target_fps)) {
                fprintf(stderr, "[HTTPStream] Failed to initialize PNG encoder\n");
                break;
            }
            encoder_initialized = true;
        }

        // Compute dirty rect
        int dx = 0, dy = 0, dw = width, dh = height;

        if (have_prev_frame && (int)prev_frame.size() == width * height) {
            bool changed = compute_dirty_rect(
                curr_frame.data(), prev_frame.data(), width, height,
                dx, dy, dw, dh);

            if (!changed) {
                // No changes — skip this frame
                auto elapsed = std::chrono::steady_clock::now() - frame_start;
                auto remaining = frame_interval - elapsed;
                if (remaining.count() > 0) {
                    std::this_thread::sleep_for(remaining);
                }
                continue;
            }
        }

        // Encode the dirty rect (or full frame)
        EncodedFrame encoded;
        if (format == PIXFMT_BGRA) {
            encoded = encoder.encode_bgra_rect(
                reinterpret_cast<const uint8_t*>(curr_frame.data()),
                width, height, width * 4,
                dx, dy, dw, dh);
        } else {
            // ARGB: convert dirty rect to BGRA then encode
            std::vector<uint8_t> bgra_rect(dw * dh * 4);
            for (int ry = 0; ry < dh; ry++) {
                const uint8_t* src = reinterpret_cast<const uint8_t*>(curr_frame.data()) +
                    (dy + ry) * width * 4 + dx * 4;
                uint8_t* dst = bgra_rect.data() + ry * dw * 4;
                for (int rx = 0; rx < dw; rx++) {
                    dst[rx * 4 + 0] = src[rx * 4 + 3]; // B
                    dst[rx * 4 + 1] = src[rx * 4 + 2]; // G
                    dst[rx * 4 + 2] = src[rx * 4 + 1]; // R
                    dst[rx * 4 + 3] = src[rx * 4 + 0]; // A
                }
            }
            encoded = encoder.encode_bgra(bgra_rect.data(), dw, dh, dw * 4);
        }

        if (encoded.data.empty()) {
            auto elapsed = std::chrono::steady_clock::now() - frame_start;
            auto remaining = frame_interval - elapsed;
            if (remaining.count() > 0) {
                std::this_thread::sleep_for(remaining);
            }
            continue;
        }

        // Save current frame for next comparison
        if ((int)prev_frame.size() != width * height) {
            prev_frame.resize(width * height);
        }
        memcpy(prev_frame.data(), curr_frame.data(), width * height * 4);
        have_prev_frame = true;

        // Build the frame: [4-byte length] [45-byte header] [image data]
        uint32_t total_len = HEADER_SIZE + encoded.data.size();
        size_t payload_size = LENGTH_PREFIX_SIZE + total_len;

        send_buf.resize(payload_size);
        uint8_t* p = send_buf.data();

        // 4-byte length prefix
        write_le32(p, total_len);
        p += LENGTH_PREFIX_SIZE;

        // 45-byte header
        uint64_t t4 = now_ms();

        write_le64(p + 0, t1);                              // t1_frame_ready
        write_le32(p + 8, (uint32_t)dx);                    // dirty_x
        write_le32(p + 12, (uint32_t)dy);                   // dirty_y
        write_le32(p + 16, (uint32_t)dw);                   // dirty_width
        write_le32(p + 20, (uint32_t)dh);                   // dirty_height
        write_le32(p + 24, (uint32_t)width);                // frame_width
        write_le32(p + 28, (uint32_t)height);               // frame_height
        write_le64(p + 32, t4);                             // t4_send_time
        write_le16(p + 40, cursor_x);                       // cursor_x
        write_le16(p + 42, cursor_y);                       // cursor_y
        p[44] = cursor_visible;                             // cursor_visible
        p += HEADER_SIZE;

        // Image data
        memcpy(p, encoded.data.data(), encoded.data.size());

        // Send as HTTP chunk: "hex_size\r\n[payload]\r\n"
        char chunk_header[32];
        int hdr_len = snprintf(chunk_header, sizeof(chunk_header), "%x\r\n", (unsigned)payload_size);

        if (!send_all(client_fd, chunk_header, hdr_len)) break;
        if (!send_all(client_fd, send_buf.data(), payload_size)) break;
        if (!send_all(client_fd, "\r\n", 2)) break;
        last_send_time = std::chrono::steady_clock::now();

        // Rate limit
        auto elapsed = std::chrono::steady_clock::now() - frame_start;
        auto remaining = frame_interval - elapsed;
        if (remaining.count() > 0) {
            std::this_thread::sleep_for(remaining);
        }
    }

    // Send chunked encoding terminator
    send_all(client_fd, "0\r\n\r\n", 5);

    fprintf(stderr, "[HTTPStream] Client disconnected (fd=%d)\n", client_fd);
    close(client_fd);
}

} // namespace http
