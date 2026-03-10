/*
 * WebRTC Server Implementation
 *
 * Simplified WebRTC signaling server for mac-phoenix in-process architecture.
 * Based on web-streaming/server/server.cpp but streamlined for integration.
 */

#include "webrtc_server.h"
#include "../config/json_utils.h"
#include "../webserver/keyboard_map.h"
#include "../core/boot_progress.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <chrono>
#include <thread>
#include <cstring>

// ADB input functions (from adb.cpp)
extern void ADBMouseMoved(int x, int y);
extern void ADBMouseDown(int button);
extern void ADBMouseUp(int button);
extern void ADBSetRelMouseMode(bool relative);
extern void ADBKeyDown(int code);
extern void ADBKeyUp(int code);

// Shared state for fork mode (set in main.cpp)
#include "../core/shared_state.h"
extern SharedState* g_shared_state;

// External globals from main.cpp
namespace video {
    extern std::atomic<bool> g_request_keyframe;
}

/**
 * VP9 RTP Packetizer (RFC 7741)
 *
 * Fragments VP9 frames into MTU-sized RTP packets with a 1-byte VP9 payload
 * descriptor. This is needed because libdatachannel doesn't ship a VP9
 * packetizer, and the base RtpPacketizer sends frames as single packets
 * (which exceeds MTU for keyframes).
 *
 * Payload descriptor byte:  I|P|L|F|B|E|V|Z
 *   B (bit 3) = Start of frame
 *   E (bit 2) = End of frame
 */
class VP9RtpPacketizer : public rtc::RtpPacketizer {
public:
    VP9RtpPacketizer(std::shared_ptr<rtc::RtpPacketizationConfig> rtpConfig,
                     size_t maxFragmentSize = DefaultMaxFragmentSize)
        : RtpPacketizer(std::move(rtpConfig)), maxFragmentSize_(maxFragmentSize) {}

private:
    std::vector<rtc::binary> fragment(rtc::binary data) override {
        std::vector<rtc::binary> fragments;

        if (data.size() + 1 <= maxFragmentSize_) {
            // Single packet: B=1, E=1
            rtc::binary frag(1 + data.size());
            frag[0] = std::byte{0x0C};  // B=1, E=1
            std::copy(data.begin(), data.end(), frag.begin() + 1);
            fragments.push_back(std::move(frag));
        } else {
            // Multi-packet fragmentation
            size_t payloadMax = maxFragmentSize_ - 1;  // 1 byte for descriptor
            size_t offset = 0;
            bool first = true;

            while (offset < data.size()) {
                size_t chunkSize = std::min(payloadMax, data.size() - offset);
                bool last = (offset + chunkSize >= data.size());

                rtc::binary frag(1 + chunkSize);
                uint8_t descriptor = 0;
                if (first) descriptor |= 0x08;  // B=1
                if (last) descriptor |= 0x04;   // E=1
                frag[0] = static_cast<std::byte>(descriptor);
                std::copy(data.begin() + offset,
                          data.begin() + offset + chunkSize,
                          frag.begin() + 1);

                fragments.push_back(std::move(frag));
                offset += chunkSize;
                first = false;
            }
        }

        return fragments;
    }

    size_t maxFragmentSize_;
};

/**
 * Process binary input message from browser data channel.
 * Protocol: [type:uint8] [payload...]
 *   1 = mouse move relative: dx:int16LE, dy:int16LE, ts:float64LE (13 bytes)
 *   2 = mouse button: button:uint8, down:uint8, ts:float64LE (11 bytes)
 *   3 = key: keycode:uint16LE, down:uint8, ts:float64LE (12 bytes)
 *   5 = mouse move absolute: x:uint16LE, y:uint16LE, ts:float64LE (13 bytes)
 *   6 = mouse mode change: mode:uint8 (2 bytes, 0=absolute, 1=relative)
 */
static void process_input_message(const std::byte* data, size_t size) {
    if (size < 2) return;

    uint8_t type = static_cast<uint8_t>(data[0]);

    // Fork mode: write to shared memory input queue
    if (g_shared_state) {
        switch (type) {
            case 1: { // Mouse move (relative)
                if (size < 5) return;
                int16_t dx, dy;
                std::memcpy(&dx, data + 1, 2);
                std::memcpy(&dy, data + 3, 2);
                shared_input_push(g_shared_state, SHM_INPUT_MOUSE_REL, 0, dx, dy, 0);
                break;
            }
            case 2: { // Mouse button
                if (size < 3) return;
                uint8_t button = static_cast<uint8_t>(data[1]);
                uint8_t down = static_cast<uint8_t>(data[2]);
                shared_input_push(g_shared_state, SHM_INPUT_MOUSE_BUTTON, down, 0, 0, button);
                break;
            }
            case 3: { // Key
                if (size < 4) return;
                uint16_t browser_keycode;
                std::memcpy(&browser_keycode, data + 1, 2);
                uint8_t down = static_cast<uint8_t>(data[3]);
                int mac_keycode = keyboard_map::browser_to_mac_keycode(browser_keycode);
                if (mac_keycode >= 0) {
                    shared_input_push(g_shared_state, SHM_INPUT_KEY, down, 0, 0, (uint8_t)mac_keycode);
                }
                break;
            }
            case 5: { // Mouse move (absolute)
                if (size < 5) return;
                uint16_t x, y;
                std::memcpy(&x, data + 1, 2);
                std::memcpy(&y, data + 3, 2);
                shared_input_push(g_shared_state, SHM_INPUT_MOUSE_ABS, 0, (int16_t)x, (int16_t)y, 0);
                break;
            }
            case 6: { // Mouse mode change
                if (size < 2) return;
                uint8_t mode = static_cast<uint8_t>(data[1]);
                shared_input_push(g_shared_state, SHM_INPUT_MOUSE_MODE, mode, 0, 0, 0);
                break;
            }
        }
        return;
    }

    // In-process mode (headless): call ADB directly
    switch (type) {
        case 1: { // Mouse move (relative)
            if (size < 5) return;
            int16_t dx, dy;
            std::memcpy(&dx, data + 1, 2);
            std::memcpy(&dy, data + 3, 2);
            ADBMouseMoved(dx, dy);
            break;
        }
        case 2: { // Mouse button
            if (size < 3) return;
            uint8_t button = static_cast<uint8_t>(data[1]);
            uint8_t down = static_cast<uint8_t>(data[2]);
            if (down)
                ADBMouseDown(button);
            else
                ADBMouseUp(button);
            break;
        }
        case 3: { // Key
            if (size < 4) return;
            uint16_t browser_keycode;
            std::memcpy(&browser_keycode, data + 1, 2);
            uint8_t down = static_cast<uint8_t>(data[3]);
            int mac_keycode = keyboard_map::browser_to_mac_keycode(browser_keycode);
            if (mac_keycode >= 0) {
                if (down)
                    ADBKeyDown(mac_keycode);
                else
                    ADBKeyUp(mac_keycode);
            }
            break;
        }
        case 5: { // Mouse move (absolute)
            if (size < 5) return;
            uint16_t x, y;
            std::memcpy(&x, data + 1, 2);
            std::memcpy(&y, data + 3, 2);
            ADBMouseMoved(x, y);
            break;
        }
        case 6: { // Mouse mode change
            if (size < 2) return;
            uint8_t mode = static_cast<uint8_t>(data[1]);
            ADBSetRelMouseMode(mode == 1);
            fprintf(stderr, "[WebRTC] Mouse mode changed to %s\n",
                    mode == 1 ? "relative" : "absolute");
            break;
        }
        default:
            break;
    }
}

namespace webrtc {

WebRTCServer::~WebRTCServer() {
    shutdown();
}

bool WebRTCServer::init(int signaling_port) {
    port_ = signaling_port;
    start_time_ = std::chrono::steady_clock::now();

    // Initialize libdatachannel logging
    rtc::InitLogger(rtc::LogLevel::Warning);
    rtc::Preload();

    try {
        // Configure WebSocket server
        rtc::WebSocketServer::Configuration config;
        config.port = signaling_port;
        config.enableTls = false;

        ws_server_ = std::make_unique<rtc::WebSocketServer>(config);

        // Handle new client connections
        ws_server_->onClient([this](std::shared_ptr<rtc::WebSocket> ws) {
            fprintf(stderr, "[WebRTC] New WebSocket connection\n");

            // Store WebSocket shared_ptr
            {
                std::lock_guard<std::mutex> lock(peers_mutex_);
                ws_connections_[ws.get()] = ws;
            }

            ws->onOpen([ws]() {
                fprintf(stderr, "[WebRTC] WebSocket opened\n");
                std::string welcome = "{\"type\":\"welcome\",\"peerId\":\"server\"}";
                ws->send(welcome);
            });

            ws->onMessage([this, ws](auto data) {
                if (std::holds_alternative<std::string>(data)) {
                    process_signaling(ws.get(), std::get<std::string>(data));
                }
            });

            ws->onError([](std::string error) {
                fprintf(stderr, "[WebRTC] WebSocket error: %s\n", error.c_str());
            });

            ws->onClosed([this, ws]() {
                fprintf(stderr, "[WebRTC] WebSocket closed\n");
                std::lock_guard<std::mutex> lock(peers_mutex_);
                auto it = ws_to_peer_id_.find(ws.get());
                if (it != ws_to_peer_id_.end()) {
                    fprintf(stderr, "[WebRTC] Removing peer: %s\n", it->second.c_str());
                    peers_.erase(it->second);
                    ws_to_peer_id_.erase(it);
                    peer_count_--;
                }
                ws_connections_.erase(ws.get());
            });
        });

        initialized_ = true;
        fprintf(stderr, "[WebRTC] Signaling server listening on port %d\n", signaling_port);
        return true;

    } catch (const std::exception& e) {
        fprintf(stderr, "[WebRTC] Failed to start signaling server: %s\n", e.what());
        return false;
    }
}

void WebRTCServer::shutdown() {
    if (!initialized_) return;

    fprintf(stderr, "[WebRTC] Shutting down signaling server\n");
    std::lock_guard<std::mutex> lock(peers_mutex_);
    peers_.clear();
    ws_to_peer_id_.clear();
    ws_connections_.clear();
    ws_server_.reset();
    initialized_ = false;
}

void WebRTCServer::process_signaling(rtc::WebSocket* ws, const std::string& message) {
    try {
        auto j = nlohmann::json::parse(message);
        std::string type = json_utils::get_string(j, "type");

        fprintf(stderr, "[WebRTC] Signaling message: %s\n", type.c_str());

        if (type == "connect") {
            // Client is requesting connection - server creates offer
            std::string peer_id = json_utils::get_string(j, "peerId", "client-" + std::to_string(peer_count_.load()));
            std::string codec_str = json_utils::get_string(j, "codec", "h264");

            // Map codec string to CodecType
            CodecType codec = CodecType::H264;
            if (codec_str == "vp9") codec = CodecType::VP9;
            else if (codec_str == "png") codec = CodecType::PNG;
            else if (codec_str == "webp") codec = CodecType::WEBP;

            fprintf(stderr, "[WebRTC] Client connect request - creating peer connection for %s (codec: %s)\n",
                    peer_id.c_str(), codec_str.c_str());

            // Store WebSocket mapping FIRST (create_peer_connection needs it)
            {
                std::lock_guard<std::mutex> lock(peers_mutex_);
                ws_to_peer_id_[ws] = peer_id;
            }

            // Create peer connection (server-initiated)
            auto peer = create_peer_connection(peer_id, codec);
            if (!peer) {
                fprintf(stderr, "[WebRTC] Failed to create peer connection\n");
                // Remove mapping on failure
                std::lock_guard<std::mutex> lock(peers_mutex_);
                ws_to_peer_id_.erase(ws);
                return;
            }

            // Store peer
            {
                std::lock_guard<std::mutex> lock(peers_mutex_);
                peers_[peer_id] = peer;
                peer_count_++;
            }

            // Send "connected" acknowledgment (like web-streaming does)
            const char* codec_name = (codec == CodecType::H264) ? "h264" :
                                    (codec == CodecType::AV1) ? "av1" :
                                    (codec == CodecType::VP9) ? "vp9" :
                                    (codec == CodecType::WEBP) ? "webp" : "png";

            nlohmann::json ack;
            ack["type"] = "connected";
            ack["peer_id"] = peer_id;
            ack["codec"] = codec_name;

            {
                std::lock_guard<std::mutex> lock(peers_mutex_);
                auto it = ws_connections_.find(ws);
                if (it != ws_connections_.end() && it->second) {
                    it->second->send(ack.dump());
                    fprintf(stderr, "[WebRTC] Sent 'connected' ack to peer %s\n", peer_id.c_str());
                }
            }

            // Note: onLocalDescription callback fires automatically when tracks are added
            // No need to call setLocalDescription() explicitly

        } else if (type == "answer") {
            // Client sent answer to our offer
            std::string sdp = json_utils::get_string(j, "sdp");

            // Look up peer by WebSocket (client doesn't send peerId in answer)
            std::lock_guard<std::mutex> lock(peers_mutex_);
            auto ws_it = ws_to_peer_id_.find(ws);
            if (ws_it != ws_to_peer_id_.end()) {
                std::string peer_id = ws_it->second;
                auto peer_it = peers_.find(peer_id);
                if (peer_it != peers_.end()) {
                    fprintf(stderr, "[WebRTC] Received answer from %s (sdp length=%zu)\n",
                            peer_id.c_str(), sdp.size());
                    peer_it->second->pc->setRemoteDescription(rtc::Description(sdp, "answer"));
                    fprintf(stderr, "[WebRTC] Remote description set for %s\n", peer_id.c_str());
                }
            } else {
                fprintf(stderr, "[WebRTC] ERROR: Received answer but no peer found for WebSocket\n");
            }

        } else if (type == "offer") {
            // Extract peer info
            std::string peer_id = json_utils::get_string(j, "peerId");
            std::string sdp = json_utils::get_string(j, "sdp");
            std::string codec_str = json_utils::get_string(j, "codec", "h264");

            // Map codec string to CodecType
            CodecType codec = CodecType::H264;
            if (codec_str == "vp9") codec = CodecType::VP9;
            else if (codec_str == "png") codec = CodecType::PNG;
            else if (codec_str == "webp") codec = CodecType::WEBP;

            fprintf(stderr, "[WebRTC] Creating peer connection for %s (codec: %s)\n",
                    peer_id.c_str(), codec_str.c_str());

            // Create peer connection
            auto peer = create_peer_connection(peer_id, codec);
            if (!peer) {
                fprintf(stderr, "[WebRTC] Failed to create peer connection\n");
                return;
            }

            // Store peer and mapping
            {
                std::lock_guard<std::mutex> lock(peers_mutex_);
                peers_[peer_id] = peer;
                ws_to_peer_id_[ws] = peer_id;
                peer_count_++;
            }

            // Set remote description (offer)
            peer->pc->setRemoteDescription(rtc::Description(sdp, "offer"));

            // Create answer (will be sent via onLocalDescription callback)

        } else if (type == "candidate") {
            std::string peer_id = json_utils::get_string(j, "peerId");
            std::string candidate = json_utils::get_string(j, "candidate");
            std::string sdp_mid = json_utils::get_string(j, "sdpMid");

            std::lock_guard<std::mutex> lock(peers_mutex_);
            auto it = peers_.find(peer_id);
            if (it != peers_.end()) {
                it->second->pc->addRemoteCandidate(rtc::Candidate(candidate, sdp_mid));
            }
        }

    } catch (const std::exception& e) {
        fprintf(stderr, "[WebRTC] Signaling error: %s\n", e.what());
    }
}

std::shared_ptr<PeerConnection> WebRTCServer::create_peer_connection(const std::string& peer_id, CodecType codec) {
    auto peer = std::make_shared<PeerConnection>();
    peer->id = peer_id;
    peer->codec = codec;

    // Configure peer connection
    rtc::Configuration config;
    // Note: STUN disabled for localhost/LAN mode (matches web-streaming default)
    // config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    // Allow large video frames (up to 16MB for high-res content)
    config.maxMessageSize = 16 * 1024 * 1024;

    peer->pc = std::make_shared<rtc::PeerConnection>(config);

    // Get WebSocket for this peer (to send answer/candidates)
    rtc::WebSocket* ws = nullptr;
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        for (const auto& [ws_ptr, peer_id_mapped] : ws_to_peer_id_) {
            if (peer_id_mapped == peer_id) {
                ws = ws_ptr;
                break;
            }
        }
    }

    if (!ws) {
        fprintf(stderr, "[WebRTC] Could not find WebSocket for peer\n");
        return nullptr;
    }

    // Capture ws_connections_ reference to keep WebSocket alive
    auto ws_connections = &ws_connections_;
    std::mutex* peers_mutex = &peers_mutex_;

    // Send local description (offer or answer) to browser
    peer->pc->onLocalDescription([ws, ws_connections, peers_mutex, peer_id](rtc::Description desc) {
        fprintf(stderr, "[WebRTC] !!! onLocalDescription FIRED for %s !!!\n", peer_id.c_str());
        nlohmann::json msg;
        msg["type"] = desc.typeString();  // "offer" or "answer"
        msg["sdp"] = std::string(desc);

        std::lock_guard<std::mutex> lock(*peers_mutex);
        auto ws_it = ws_connections->find(ws);
        if (ws_it != ws_connections->end() && ws_it->second) {
            ws_it->second->send(msg.dump());
            fprintf(stderr, "[WebRTC] Sent %s to %s (sdp length=%zu)\n",
                    desc.typeString().c_str(), peer_id.c_str(), std::string(desc).size());
        } else {
            fprintf(stderr, "[WebRTC] ERROR: WebSocket not found when sending %s\n", desc.typeString().c_str());
        }
    });

    // Send ICE candidates to browser
    peer->pc->onLocalCandidate([ws, ws_connections, peers_mutex, peer_id](rtc::Candidate cand) {
        nlohmann::json candidate;
        candidate["type"] = "candidate";
        candidate["candidate"] = std::string(cand);
        candidate["mid"] = cand.mid();  // Client expects "mid" not "sdpMid"

        std::lock_guard<std::mutex> lock(*peers_mutex);
        auto ws_it = ws_connections->find(ws);
        if (ws_it != ws_connections->end() && ws_it->second) {
            ws_it->second->send(candidate.dump());
            fprintf(stderr, "[WebRTC] Sent ICE candidate to %s (mid=%s)\n",
                    peer_id.c_str(), cand.mid().c_str());
        }
    });

    peer->pc->onStateChange([peer_id](rtc::PeerConnection::State state) {
        const char* state_str = "unknown";
        switch (state) {
            case rtc::PeerConnection::State::New: state_str = "New"; break;
            case rtc::PeerConnection::State::Connecting: state_str = "Connecting"; break;
            case rtc::PeerConnection::State::Connected: state_str = "Connected"; break;
            case rtc::PeerConnection::State::Disconnected: state_str = "Disconnected"; break;
            case rtc::PeerConnection::State::Failed: state_str = "Failed"; break;
            case rtc::PeerConnection::State::Closed: state_str = "Closed"; break;
        }
        fprintf(stderr, "[WebRTC] Peer %s state: %s\n", peer_id.c_str(), state_str);
    });

    peer->pc->onIceStateChange([peer_id](rtc::PeerConnection::IceState state) {
        const char* state_str = "unknown";
        switch (state) {
            case rtc::PeerConnection::IceState::New: state_str = "New"; break;
            case rtc::PeerConnection::IceState::Checking: state_str = "Checking"; break;
            case rtc::PeerConnection::IceState::Connected: state_str = "Connected"; break;
            case rtc::PeerConnection::IceState::Completed: state_str = "Completed"; break;
            case rtc::PeerConnection::IceState::Disconnected: state_str = "Disconnected"; break;
            case rtc::PeerConnection::IceState::Failed: state_str = "Failed"; break;
            case rtc::PeerConnection::IceState::Closed: state_str = "Closed"; break;
        }
        fprintf(stderr, "[WebRTC] Peer %s ICE state: %s\n", peer_id.c_str(), state_str);
    });

    peer->pc->onGatheringStateChange([peer_id](rtc::PeerConnection::GatheringState state) {
        const char* state_str = "unknown";
        switch (state) {
            case rtc::PeerConnection::GatheringState::New: state_str = "New"; break;
            case rtc::PeerConnection::GatheringState::InProgress: state_str = "InProgress"; break;
            case rtc::PeerConnection::GatheringState::Complete: state_str = "Complete"; break;
        }
        fprintf(stderr, "[WebRTC] Peer %s gathering state: %s\n", peer_id.c_str(), state_str);
    });

    // SSRC for RTP streams — increment per peer to avoid browser RTP state confusion
    // when switching between codecs (e.g., H264 → VP9) on the same SSRC
    static uint32_t ssrc_counter = 42;
    uint32_t ssrc = ssrc_counter;
    ssrc_counter += 2;  // +2 because audio uses ssrc+1

    fprintf(stderr, "[WebRTC] About to add tracks for peer %s\n", peer_id.c_str());

    // Add video track (H.264 and VP9 use RTP; PNG/WEBP use DataChannel)
    if (codec == CodecType::H264 || codec == CodecType::VP9) {
        auto video = rtc::Description::Video("video-stream", rtc::Description::Direction::SendOnly);

        if (codec == CodecType::H264) {
            fprintf(stderr, "[WebRTC] Creating H.264 video track\n");
            video.addH264Codec(96, "profile-level-id=42e01f;packetization-mode=1;level-asymmetry-allowed=1");
        } else {
            fprintf(stderr, "[WebRTC] Creating VP9 video track\n");
            video.addVP9Codec(96);
        }

        video.addSSRC(ssrc, "video-stream", "stream1", "video-stream");
        peer->video_track = peer->pc->addTrack(video);

        // Set up RTP packetizer
        auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
            ssrc, "video-stream", 96, rtc::RtpPacketizer::VideoClockRate
        );
        // Add RTCP SR (Sender Report) for timestamp synchronization
        auto videoSrReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfig);
        // Add RTCP NACK responder for packet loss recovery
        auto videoNackResponder = std::make_shared<rtc::RtcpNackResponder>();

        if (codec == CodecType::H264) {
            auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
                rtc::H264RtpPacketizer::Separator::LongStartSequence,
                rtpConfig
            );
            packetizer->addToChain(videoSrReporter);
            packetizer->addToChain(videoNackResponder);
            peer->video_track->setMediaHandler(packetizer);
        } else {
            // VP9: Use custom packetizer with RFC 7741 payload descriptor
            auto packetizer = std::make_shared<VP9RtpPacketizer>(rtpConfig);
            packetizer->addToChain(videoSrReporter);
            packetizer->addToChain(videoNackResponder);
            peer->video_track->setMediaHandler(packetizer);
        }

        // CRITICAL: Only set ready=true when track is actually open!
        peer->video_track->onOpen([this, peer_id]() {
            fprintf(stderr, "[WebRTC] Video track OPEN for %s - ready to send frames!\n", peer_id.c_str());

            std::lock_guard<std::mutex> lock(peers_mutex_);
            auto it = peers_.find(peer_id);
            if (it != peers_.end()) {
                it->second->ready = true;
                video::g_request_keyframe.store(true, std::memory_order_release);
            }
        });

        peer->video_track->onClosed([peer_id]() {
            fprintf(stderr, "[WebRTC] Video track CLOSED for %s\n", peer_id.c_str());
        });

        peer->video_track->onError([peer_id](std::string error) {
            fprintf(stderr, "[WebRTC] Video track ERROR for %s: %s\n", peer_id.c_str(), error.c_str());
        });

        fprintf(stderr, "[WebRTC] Added video track with RTP packetizer (codec: %s)\n",
                codec == CodecType::H264 ? "H.264" : "VP9");
    }
    // PNG/WEBP use DataChannel for video (ready flag set in DC onOpen below)

    // Add audio track (Opus) with proper RTP packetizer
    fprintf(stderr, "[WebRTC] Creating Opus audio track\n");
    auto audio = rtc::Description::Audio("audio-stream", rtc::Description::Direction::SendOnly);
    audio.addOpusCodec(111);
    audio.addSSRC(ssrc + 1, "audio-stream", "stream1", "audio-stream");
    fprintf(stderr, "[WebRTC] Calling addTrack for audio\n");
    peer->audio_track = peer->pc->addTrack(audio);
    fprintf(stderr, "[WebRTC] addTrack returned for audio, setting media handler\n");

    // Set up Opus RTP packetizer (following web-streaming pattern)
    auto rtpConfigAudio = std::make_shared<rtc::RtpPacketizationConfig>(
        ssrc + 1, "audio-stream", 111, rtc::OpusRtpPacketizer::DefaultClockRate
    );
    auto opusPacketizer = std::make_shared<rtc::OpusRtpPacketizer>(rtpConfigAudio);

    // Add RTCP SR (Sender Report) for proper timestamp synchronization
    // This is CRITICAL for browsers to correctly sync and play audio
    auto srReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfigAudio);
    opusPacketizer->addToChain(srReporter);

    // Add RTCP NACK (Negative Acknowledgement) responder for packet loss recovery
    // Improves audio quality on lossy networks
    auto nackResponder = std::make_shared<rtc::RtcpNackResponder>();
    opusPacketizer->addToChain(nackResponder);

    peer->audio_track->setMediaHandler(opusPacketizer);
    fprintf(stderr, "[WebRTC] Media handler set for audio\n");

    peer->audio_track->onOpen([peer_id]() {
        fprintf(stderr, "[WebRTC] Audio track opened for %s\n", peer_id.c_str());
    });

    fprintf(stderr, "[WebRTC] Added audio track with Opus RTP packetizer\n");

    // Add data channel (for PNG/WebP frames or H.264/VP9 metadata)
    rtc::DataChannelInit dc_init;
    if (codec == CodecType::PNG || codec == CodecType::WEBP) {
        // Unreliable mode for still-image codecs: drop stale frames instead of retransmitting
        dc_init.reliability.unordered = true;
        dc_init.reliability.maxRetransmits = 0;
    }
    peer->data_channel = peer->pc->createDataChannel("metadata", dc_init);
    fprintf(stderr, "[WebRTC] Created data channel%s\n",
            (codec == CodecType::PNG || codec == CodecType::WEBP) ? " (unreliable)" : "");

    auto dc_ptr = peer->data_channel;
    peer->data_channel->onOpen([this, peer_id, codec, dc_ptr]() {
        fprintf(stderr, "[WebRTC] Data channel opened for peer %s (maxMessageSize=%zu)\n",
                peer_id.c_str(), dc_ptr->maxMessageSize());
        // For PNG/WEBP (no video track), mark peer ready when DataChannel opens
        if (codec == CodecType::PNG || codec == CodecType::WEBP) {
            std::lock_guard<std::mutex> lock(peers_mutex_);
            auto it = peers_.find(peer_id);
            if (it != peers_.end()) {
                it->second->ready = true;
                video::g_request_keyframe.store(true, std::memory_order_release);
                fprintf(stderr, "[WebRTC] DataChannel peer %s marked ready (codec: %d)\n", peer_id.c_str(), (int)codec);
            }
        }
    });

    peer->data_channel->onMessage([peer_id](auto data) {
        if (std::holds_alternative<rtc::binary>(data)) {
            const auto& bin = std::get<rtc::binary>(data);
            process_input_message(bin.data(), bin.size());
        }
        // Ignore text messages (legacy JSON commands)
    });

    // NOTE: Don't set peer->ready here! It's set in video_track->onOpen() callback
    // This ensures we only send frames after the track is actually open
    peer->ready = false;  // Will be set to true in onOpen callback

    fprintf(stderr, "[WebRTC] Sent 'connected' ack to peer %s\n", peer_id.c_str());
    return peer;
}

void WebRTCServer::send_video_frame(const uint8_t* data, size_t size, bool is_keyframe,
                                    int width, int height,
                                    int dirty_x, int dirty_y,
                                    int dirty_width, int dirty_height,
                                    int frame_width, int frame_height) {
    static bool debug_frames = (getenv("MACEMU_DEBUG_FRAMES") != nullptr);
    static int send_count = 0;

    if (!initialized_ || peer_count_ == 0 || !data || size == 0) {
        if (debug_frames && send_count == 0) {
            fprintf(stderr, "[WebRTC] send_video_frame blocked: init=%d peers=%d data=%p size=%zu\n",
                    (int)initialized_, (int)peer_count_.load(), (void*)data, size);
        }
        return;
    }

    std::lock_guard<std::mutex> lock(peers_mutex_);

    int sent_to = 0;
    int skipped_not_ready = 0;
    int skipped_no_track = 0;
    int skipped_not_open = 0;

    // Build metadata for data channel
    // Format: [cursor_x:2][cursor_y:2][cursor_visible:1]
    // Total: 5 bytes
    uint8_t metadata[5] = {0};
    int mx = 0, my = 0;
    if (g_shared_state) {
        // Fork mode: read cursor from shared memory (child writes at 60Hz)
        mx = g_shared_state->cursor_x.load(std::memory_order_relaxed);
        my = g_shared_state->cursor_y.load(std::memory_order_relaxed);
    } else {
        // Legacy mode: read directly from Mac low-memory globals
        boot_progress_get_mouse(&mx, &my);
    }
    uint16_t cx = static_cast<uint16_t>(mx);
    uint16_t cy = static_cast<uint16_t>(my);
    std::memcpy(metadata + 0, &cx, 2);
    std::memcpy(metadata + 2, &cy, 2);
    metadata[4] = (mx != 0 || my != 0) ? 1 : 0;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - start_time_);
    rtc::FrameInfo frameInfo(elapsed);

    for (const auto& [peer_id, peer] : peers_) {
        if (!peer->ready) {
            skipped_not_ready++;
            continue;
        }

        try {
            if (peer->codec == CodecType::PNG || peer->codec == CodecType::WEBP) {
                // PNG/WEBP: send frame via datachannel with 45-byte metadata header
                if (peer->data_channel && peer->data_channel->isOpen()) {
                    // Build header: [t1:8][x:4][y:4][w:4][h:4][fw:4][fh:4][t4:8][cursor:5]
                    std::vector<uint8_t> frame_with_header(45 + size);
                    // Leave t1 (0-7) as zero
                    uint32_t dx = static_cast<uint32_t>(dirty_x);
                    uint32_t dy = static_cast<uint32_t>(dirty_y);
                    uint32_t dw = static_cast<uint32_t>(dirty_width > 0 ? dirty_width : width);
                    uint32_t dh = static_cast<uint32_t>(dirty_height > 0 ? dirty_height : height);
                    uint32_t fw = static_cast<uint32_t>(frame_width > 0 ? frame_width : width);
                    uint32_t fh = static_cast<uint32_t>(frame_height > 0 ? frame_height : height);
                    std::memcpy(frame_with_header.data() + 8, &dx, 4);     // dirty rect x
                    std::memcpy(frame_with_header.data() + 12, &dy, 4);    // dirty rect y
                    std::memcpy(frame_with_header.data() + 16, &dw, 4);    // dirty rect width
                    std::memcpy(frame_with_header.data() + 20, &dh, 4);    // dirty rect height
                    std::memcpy(frame_with_header.data() + 24, &fw, 4);    // frame width
                    std::memcpy(frame_with_header.data() + 28, &fh, 4);    // frame height
                    // t4 (offsets 32-39) left as zero
                    // Cursor (offsets 40-44)
                    std::memcpy(frame_with_header.data() + 40, &cx, 2);
                    std::memcpy(frame_with_header.data() + 42, &cy, 2);
                    frame_with_header[44] = metadata[4];  // cursor_visible
                    // Copy frame data after header
                    std::memcpy(frame_with_header.data() + 45, data, size);

                    // Check message size before sending
                    size_t max_msg = peer->data_channel->maxMessageSize();
                    if (max_msg > 0 && frame_with_header.size() > max_msg) {
                        static int dc_size_warn_count = 0;
                        if (dc_size_warn_count < 5) {
                            fprintf(stderr, "[WebRTC] Frame too large for DataChannel: %zu bytes > %zu limit (%dx%d %s). "
                                    "Consider using H.264 or VP9 codec for high resolutions.\n",
                                    frame_with_header.size(), max_msg, width, height,
                                    peer->codec == CodecType::PNG ? "PNG" : "WebP");
                            dc_size_warn_count++;
                            if (dc_size_warn_count == 5)
                                fprintf(stderr, "[WebRTC] (suppressing further DataChannel size warnings)\n");
                        }
                        continue;  // Skip this frame
                    }

                    // Send full frame as single message
                    peer->data_channel->send(
                        reinterpret_cast<const std::byte*>(frame_with_header.data()),
                        frame_with_header.size());
                    sent_to++;
                } else {
                    skipped_not_open++;
                }
            } else {
                // H.264/VP9: send via RTP video track
                if (!peer->video_track || !peer->video_track->isOpen()) {
                    skipped_not_open++;
                    continue;
                }

                // Don't send P-frames before first keyframe (VP9 decoders may not recover)
                if (peer->needs_keyframe && !is_keyframe) {
                    video::g_request_keyframe.store(true, std::memory_order_release);
                    skipped_not_ready++;
                    continue;
                }
                if (is_keyframe) {
                    peer->needs_keyframe = false;
                }

                peer->video_track->sendFrame(
                    reinterpret_cast<const std::byte*>(data),
                    size,
                    frameInfo
                );

                // Send metadata via data channel
                if (peer->data_channel && peer->data_channel->isOpen()) {
                    peer->data_channel->send(reinterpret_cast<const std::byte*>(metadata), sizeof(metadata));
                }

                sent_to++;
            }
        } catch (const std::exception& e) {
            fprintf(stderr, "[WebRTC] Error sending video frame: %s\n", e.what());
        }
    }

    send_count++;
    if (debug_frames && (send_count % 60 == 0 || is_keyframe || sent_to == 0)) {
        fprintf(stderr, "[WebRTC] Frame #%d: sent=%d skipped(notready=%d notopen=%d) kf=%d size=%zu\n",
                send_count, sent_to, skipped_not_ready, skipped_not_open, is_keyframe, size);
    }
}

void WebRTCServer::send_audio_frame(const uint8_t* data, size_t size) {
    if (!initialized_ || peer_count_ == 0 || !data || size == 0) return;

    std::lock_guard<std::mutex> lock(peers_mutex_);
    for (const auto& [peer_id, peer] : peers_) {
        if (!peer->ready || !peer->audio_track) continue;

        // Check if track is open before sending
        if (!peer->audio_track->isOpen()) continue;

        try {
            // Send frame with timestamp
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration<double>(now - start_time_);
            rtc::FrameInfo frameInfo(elapsed);

            // Create byte vector from raw pointer
            std::vector<std::byte> frame_data(size);
            std::memcpy(frame_data.data(), data, size);

            peer->audio_track->send(frame_data);

        } catch (const std::exception& e) {
            fprintf(stderr, "[WebRTC] Error sending audio frame: %s\n", e.what());
        }
    }
}

void WebRTCServer::notify_codec_change(CodecType new_codec) {
    const char* codec_name = (new_codec == CodecType::H264) ? "h264" :
                             (new_codec == CodecType::AV1) ? "av1" :
                             (new_codec == CodecType::VP9) ? "vp9" :
                             (new_codec == CodecType::WEBP) ? "webp" : "png";
    fprintf(stderr, "[WebRTC] Codec change requested: %s\n", codec_name);

    std::lock_guard<std::mutex> lock(peers_mutex_);

    // Send reconnect message to all peers via WebSocket so they know to reconnect
    nlohmann::json msg;
    msg["type"] = "reconnect";
    msg["reason"] = "codec_change";
    msg["codec"] = codec_name;
    std::string msg_str = msg.dump();

    for (const auto& [ws_ptr, ws_shared] : ws_connections_) {
        if (ws_shared) {
            try {
                ws_shared->send(msg_str);
            } catch (const std::exception& e) {
                fprintf(stderr, "[WebRTC] Error sending reconnect to peer: %s\n", e.what());
            }
        }
    }

    // Close all peer connections (browser will reconnect with new codec)
    for (const auto& [peer_id, peer] : peers_) {
        if (peer->pc) {
            peer->pc->close();
        }
    }
}

//
// WebRTC Server Thread Main
//

void webrtc_server_main(WebRTCServer* server, std::atomic<bool>* running) {
    fprintf(stderr, "[WebRTC] Server thread starting\n");

    // Server is already initialized by main thread before launching this thread
    // Just keep thread alive until shutdown signal
    while (running->load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    fprintf(stderr, "[WebRTC] Server thread shutting down\n");
}

} // namespace webrtc
