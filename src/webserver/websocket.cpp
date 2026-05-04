/*
 * WebSocket (RFC 6455) implementation.
 */

#include "websocket.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace http {

namespace {

// Blocking send of the full buffer, retrying on EINTR/EAGAIN.
bool send_all(int fd, const void* data, std::size_t len) {
    const char* p = static_cast<const char*>(data);
    while (len > 0) {
        ssize_t n = ::send(fd, p, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p   += n;
        len -= n;
    }
    return true;
}

}  // namespace

std::string WebSocket::compute_accept(const std::string& client_key) {
    // RFC 6455: accept = base64(SHA1(key + magic))
    static constexpr char magic[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    QByteArray concat = QByteArray::fromStdString(client_key) + QByteArray(magic);
    QByteArray sha = QCryptographicHash::hash(concat, QCryptographicHash::Sha1);
    return sha.toBase64().toStdString();
}

WebSocket::WebSocket(int fd) : fd_(fd) {}

WebSocket::~WebSocket() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool WebSocket::read_exact(void* buf, std::size_t n) {
    char* p = static_cast<char*>(buf);
    while (n > 0) {
        ssize_t r = ::recv(fd_, p, n, 0);
        if (r == 0) return false;       // peer closed
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += r;
        n -= std::size_t(r);
    }
    return true;
}

bool WebSocket::read_frame(uint8_t& opcode, bool& fin, std::vector<std::byte>& payload) {
    uint8_t hdr[2];
    if (!read_exact(hdr, 2)) return false;

    fin    = (hdr[0] & 0x80) != 0;
    opcode = hdr[0] & 0x0F;

    bool masked = (hdr[1] & 0x80) != 0;
    uint64_t plen = hdr[1] & 0x7F;

    if (plen == 126) {
        uint8_t ext[2];
        if (!read_exact(ext, 2)) return false;
        plen = (uint64_t(ext[0]) << 8) | ext[1];
    } else if (plen == 127) {
        uint8_t ext[8];
        if (!read_exact(ext, 8)) return false;
        plen = 0;
        for (int i = 0; i < 8; ++i) plen = (plen << 8) | ext[i];
    }

    uint8_t mask[4] = {};
    if (masked) {
        if (!read_exact(mask, 4)) return false;
    }

    // Guard against malicious sizes. 32MB is well above a 1024x1024 PNG.
    constexpr uint64_t MAX_FRAME = 32ULL * 1024 * 1024;
    if (plen > MAX_FRAME) {
        fprintf(stderr, "[WS] Frame too large: %llu bytes — dropping connection\n",
                (unsigned long long)plen);
        return false;
    }

    payload.resize(std::size_t(plen));
    if (plen > 0) {
        if (!read_exact(payload.data(), std::size_t(plen))) return false;
        if (masked) {
            for (std::size_t i = 0; i < payload.size(); ++i) {
                payload[i] = std::byte(uint8_t(payload[i]) ^ mask[i % 4]);
            }
        }
    }
    return true;
}

bool WebSocket::send_frame_locked(uint8_t opcode, const void* data, std::size_t size) {
    // Server-to-client: never masked. Single-frame, FIN=1.
    uint8_t hdr[10];
    std::size_t hdr_len = 2;
    hdr[0] = 0x80 | (opcode & 0x0F);

    if (size < 126) {
        hdr[1] = uint8_t(size);
    } else if (size < 65536) {
        hdr[1] = 126;
        hdr[2] = uint8_t((size >> 8) & 0xFF);
        hdr[3] = uint8_t( size       & 0xFF);
        hdr_len = 4;
    } else {
        hdr[1] = 127;
        uint64_t big = size;
        for (int i = 0; i < 8; ++i) hdr[2 + i] = uint8_t((big >> (8 * (7 - i))) & 0xFF);
        hdr_len = 10;
    }

    if (!send_all(fd_, hdr, hdr_len)) return false;
    if (size > 0 && !send_all(fd_, data, size)) return false;
    return true;
}

void WebSocket::send(const std::string& text) {
    if (closed_.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (!send_frame_locked(OP_TEXT, text.data(), text.size())) {
        closed_.store(true, std::memory_order_release);
    }
}

void WebSocket::send(const std::vector<std::byte>& binary) {
    send(binary.data(), binary.size());
}

void WebSocket::send(const std::byte* data, std::size_t size) {
    if (closed_.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (!send_frame_locked(OP_BINARY, data, size)) {
        closed_.store(true, std::memory_order_release);
    }
}

void WebSocket::do_close(uint16_t code, const std::string& reason) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (closed_.exchange(true, std::memory_order_acq_rel)) return;

    // Payload: [code:big-endian u16][reason bytes]
    std::vector<uint8_t> payload;
    payload.reserve(2 + reason.size());
    payload.push_back(uint8_t(code >> 8));
    payload.push_back(uint8_t(code & 0xFF));
    payload.insert(payload.end(), reason.begin(), reason.end());
    (void)send_frame_locked(OP_CLOSE, payload.data(), payload.size());
    ::shutdown(fd_, SHUT_WR);
}

void WebSocket::close() {
    do_close(1000, "");
}

void WebSocket::run_read_loop() {
    // Reassembly state for fragmented data messages. Control frames (>=8) are
    // never fragmented and are handled immediately without touching this.
    uint8_t data_opcode = 0;
    std::vector<std::byte> data_buffer;

    while (!closed_.load(std::memory_order_acquire)) {
        uint8_t opcode;
        bool fin;
        std::vector<std::byte> payload;
        if (!read_frame(opcode, fin, payload)) break;

        switch (opcode) {
            case OP_CONTINUATION:
                if (data_opcode == 0) {
                    // Continuation without a start — protocol error
                    break;
                }
                data_buffer.insert(data_buffer.end(), payload.begin(), payload.end());
                if (fin) {
                    if (data_opcode == OP_TEXT) {
                        if (on_text_) {
                            on_text_(std::string(reinterpret_cast<const char*>(data_buffer.data()),
                                                 data_buffer.size()));
                        }
                    } else if (data_opcode == OP_BINARY) {
                        if (on_binary_) on_binary_(std::move(data_buffer));
                    }
                    data_buffer.clear();
                    data_opcode = 0;
                }
                break;

            case OP_TEXT:
            case OP_BINARY:
                if (fin) {
                    if (opcode == OP_TEXT) {
                        if (on_text_) {
                            on_text_(std::string(reinterpret_cast<const char*>(payload.data()),
                                                 payload.size()));
                        }
                    } else {
                        if (on_binary_) on_binary_(std::move(payload));
                    }
                } else {
                    data_opcode = opcode;
                    data_buffer = std::move(payload);
                }
                break;

            case OP_PING: {
                std::lock_guard<std::mutex> lock(write_mutex_);
                if (!send_frame_locked(OP_PONG, payload.data(), payload.size())) {
                    closed_.store(true, std::memory_order_release);
                }
                break;
            }

            case OP_PONG:
                // Ignore; heartbeat uses application-level ping/pong JSON.
                break;

            case OP_CLOSE: {
                uint16_t code = 1000;
                std::string reason;
                if (payload.size() >= 2) {
                    code = uint16_t(uint8_t(payload[0])) << 8 | uint8_t(payload[1]);
                    reason.assign(reinterpret_cast<const char*>(payload.data()) + 2,
                                  payload.size() - 2);
                }
                do_close(code, reason);
                break;
            }

            default:
                // Reserved opcode — RFC 6455 says drop the connection.
                fprintf(stderr, "[WS] Unknown opcode 0x%x — closing\n", opcode);
                do_close(1002, "protocol error");
                break;
        }
    }

    closed_.store(true, std::memory_order_release);
    if (on_close_) on_close_();
}

}  // namespace http
