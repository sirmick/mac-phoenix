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
#include <rtc/rtc.hpp>
#include "../drivers/video/encoders/codec.h"

namespace webrtc {

// Forward declarations
class VideoOutput;
class AudioOutput;

/**
 * Peer Connection State
 */
struct PeerConnection {
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::Track> video_track;
    std::shared_ptr<rtc::Track> audio_track;
    std::shared_ptr<rtc::DataChannel> data_channel;
    std::shared_ptr<rtc::RtpPacketizer> vp9_packetizer;  // For prepareFrame() on VP9
    std::string id;
    CodecType codec = CodecType::H264;
    bool ready = false;
    bool needs_keyframe = true;     // Don't send P-frames until first keyframe delivered
    bool needs_first_frame = true;  // Still-image codecs need full first frame
};

/**
 * WebRTC Server
 *
 * Runs WebSocket server on signaling_port (default 8090) for WebRTC signaling.
 * Manages peer connections and routes video/audio frames to connected peers.
 */
class WebRTCServer {
public:
    WebRTCServer() = default;
    ~WebRTCServer();

    /**
     * Initialize WebRTC server with signaling port
     * @param signaling_port WebSocket port for WebRTC signaling (default 8090)
     * @return true if successful
     */
    bool init(int signaling_port = 8090);

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
     * Request codec change (notifies all peers to reconnect)
     */
    void notify_codec_change(CodecType new_codec);

private:
    /**
     * Process WebRTC signaling message from browser
     */
    void process_signaling(rtc::WebSocket* ws, const std::string& message);

    /**
     * Create a new peer connection for a client
     */
    std::shared_ptr<PeerConnection> create_peer_connection(const std::string& peer_id, CodecType codec);

    // WebSocket server
    std::unique_ptr<rtc::WebSocketServer> ws_server_;

    // Active peer connections (peer_id -> PeerConnection)
    std::map<std::string, std::shared_ptr<PeerConnection>> peers_;

    // WebSocket to peer ID mapping (for disconnect handling)
    std::map<rtc::WebSocket*, std::string> ws_to_peer_id_;

    // WebSocket connections (to keep shared_ptr alive)
    std::map<rtc::WebSocket*, std::shared_ptr<rtc::WebSocket>> ws_connections_;

    // Thread safety
    std::mutex peers_mutex_;

    // State
    std::atomic<bool> initialized_{false};
    std::atomic<int> peer_count_{0};
    int port_ = 8090;

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
