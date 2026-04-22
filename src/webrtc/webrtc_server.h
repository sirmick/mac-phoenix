/*
 * WebRTC Server Module
 *
 * Manages WebSocket signaling server and WebRTC peer connections.
 * This is the 5th thread in the mac-phoenix architecture.
 */

#ifndef WEBRTC_SERVER_H
#define WEBRTC_SERVER_H

#include <memory>
#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <chrono>
#include "../drivers/video/encoders/codec.h"

// Forward declarations — full rtc headers are only included in webrtc_server.cpp
// so that dependents of this header don't need libdatachannel's include path.
namespace rtc {
class PeerConnection;
class Track;
class RtpPacketizer;
}

namespace http {
class Server;
class Request;
class WebSocket;
}

namespace webrtc {

// Forward declarations
class VideoOutput;
class AudioOutput;

/**
 * Peer Connection State
 */
struct PeerConnection {
    std::shared_ptr<rtc::PeerConnection> pc;          // Null for PNG/WebP peers (WS-only)
    std::shared_ptr<rtc::Track> video_track;          // Null for PNG/WebP peers
    std::shared_ptr<rtc::Track> audio_track;          // Null for PNG/WebP peers
    std::weak_ptr<http::WebSocket> ws;                // Signaling/input/frame transport
    std::shared_ptr<rtc::RtpPacketizer> vp9_packetizer;  // For prepareFrame() on VP9
    std::string id;
    CodecType codec = CodecType::H264;
    bool ready = false;
    bool is_webrtc = false;         // true for H.264/VP9; false for PNG/WebP (WS-only)
    bool needs_keyframe = true;     // Don't send P-frames until first keyframe delivered
    bool needs_first_frame = true;  // Still-image codecs need full first frame
};

/**
 * WebRTC Server
 *
 * Registers a /ws WebSocket route on the shared HTTP server. Signaling,
 * client input, and PNG/WebP frame delivery all ride that single socket.
 * Manages peer connections and routes video/audio frames to connected peers.
 */
class WebRTCServer {
public:
    WebRTCServer() = default;
    ~WebRTCServer();

    /**
     * Initialize libdatachannel. Call register_routes() separately once an
     * http::Server is available.
     */
    bool init();

    /**
     * Register the /ws WebSocket route on the given HTTP server. Must be
     * called after init() and before the server starts accepting clients.
     */
    void register_routes(http::Server& http_server);

    /**
     * Shutdown server and close all peer connections
     */
    void shutdown();

    /**
     * Check if server is initialized
     */
    bool is_initialized() const { return initialized_; }

    /**
     * Get number of connected peers
     */
    int get_peer_count() const { return peer_count_.load(); }

    /**
     * Send encoded video frame to all connected peers
     * (Frame type depends on codec - H.264, VP9, AV1, PNG, WebP)
     */
    void send_video_frame(const uint8_t* data, size_t size, bool is_keyframe,
                          int width = 0, int height = 0,
                          int dirty_x = 0, int dirty_y = 0,
                          int dirty_width = 0, int dirty_height = 0,
                          int frame_width = 0, int frame_height = 0);

    /**
     * Send encoded audio frame to all connected peers (Opus)
     */
    void send_audio_frame(const uint8_t* data, size_t size);

    /**
     * Reset peer state after emulator restart (forces keyframe delivery)
     */
    void reset_peer_state();

    /**
     * Request codec change (notifies all peers to reconnect)
     */
    void notify_codec_change(CodecType new_codec);

private:
    /**
     * Process a signaling text message from a connected browser.
     * peer_id_slot is the per-connection slot that identifies this WS; it's
     * written on "connect"/"offer" so later close/candidate messages can find
     * the peer by string key.
     */
    void process_signaling(std::shared_ptr<http::WebSocket> ws, const std::string& message,
                           std::shared_ptr<std::string> peer_id_slot);

    /**
     * Create a new peer connection for a client.
     * For PNG/WebP codecs, no PeerConnection is created — the returned peer is
     * WS-only (frames + input ride the signaling socket).
     */
    std::shared_ptr<PeerConnection> create_peer_connection(const std::string& peer_id, CodecType codec,
                                                           std::shared_ptr<http::WebSocket> ws);

    // Remove peer by id (called from WS on_close)
    void remove_peer(const std::string& peer_id);

    // Active peer connections (peer_id -> PeerConnection)
    std::map<std::string, std::shared_ptr<PeerConnection>> peers_;

    // Thread safety
    std::mutex peers_mutex_;

    // State
    std::atomic<bool> initialized_{false};
    std::atomic<int> peer_count_{0};

    // Timing for RTP timestamps
    std::chrono::steady_clock::time_point start_time_;
};

/**
 * WebRTC server main thread entry point
 * @param server WebRTCServer instance (must remain valid for thread lifetime)
 * @param running Atomic flag to control thread execution
 */
void webrtc_server_main(WebRTCServer* server, std::atomic<bool>* running);

// Global WebRTC server instance (set in main.cpp)
// Used by encoder threads to send video/audio frames to connected peers
extern WebRTCServer* g_server;

} // namespace webrtc

#endif // WEBRTC_SERVER_H
