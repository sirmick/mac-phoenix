/*
 * WebSocket (RFC 6455) — in-process implementation.
 *
 * Replaces libdatachannel's rtc::WebSocketServer so that signaling + input +
 * PNG/WebP frames ride the same TCP listener as HTTP, eliminating the need for
 * a separate signaling port. Sends are thread-safe; a blocking read loop
 * dispatches inbound frames to user callbacks.
 */

#ifndef HTTP_WEBSOCKET_H
#define HTTP_WEBSOCKET_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace http {

class WebSocket : public std::enable_shared_from_this<WebSocket> {
public:
    // Takes ownership of fd; closes it on destruction or close().
    explicit WebSocket(int fd);
    ~WebSocket();

    WebSocket(const WebSocket&) = delete;
    WebSocket& operator=(const WebSocket&) = delete;

    bool is_open() const noexcept { return !closed_.load(std::memory_order_acquire); }

    // Thread-safe sends. No-op after close.
    void send(const std::string& text);
    void send(const std::vector<std::byte>& binary);
    void send(const std::byte* data, std::size_t size);

    // Initiate a clean close (sends close frame, then shuts down).
    void close();

    // Register callbacks *before* calling run_read_loop().
    void on_text(std::function<void(std::string)> cb)            { on_text_   = std::move(cb); }
    void on_binary(std::function<void(std::vector<std::byte>)> cb) { on_binary_ = std::move(cb); }
    void on_close(std::function<void()> cb)                       { on_close_  = std::move(cb); }

    // Blocks the calling thread, reading frames and dispatching. Returns when
    // the peer closes, a protocol error occurs, or close() is called.
    void run_read_loop();

    // Compute Sec-WebSocket-Accept from the client's Sec-WebSocket-Key.
    // Used by the HTTP server during the upgrade handshake.
    static std::string compute_accept(const std::string& client_key);

private:
    enum Opcode : uint8_t {
        OP_CONTINUATION = 0x0,
        OP_TEXT         = 0x1,
        OP_BINARY       = 0x2,
        OP_CLOSE        = 0x8,
        OP_PING         = 0x9,
        OP_PONG         = 0xA,
    };

    bool read_exact(void* buf, std::size_t n);
    bool read_frame(uint8_t& opcode, bool& fin, std::vector<std::byte>& payload);
    bool send_frame_locked(uint8_t opcode, const void* data, std::size_t size);
    void do_close(uint16_t code, const std::string& reason);

    int fd_;
    std::atomic<bool> closed_{false};
    std::mutex write_mutex_;

    std::function<void(std::string)>             on_text_;
    std::function<void(std::vector<std::byte>)>  on_binary_;
    std::function<void()>                        on_close_;
};

}  // namespace http

#endif  // HTTP_WEBSOCKET_H
