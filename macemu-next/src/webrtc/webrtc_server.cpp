/*
 * WebRTC Server Implementation
 *
 * Simplified WebRTC signaling server for macemu-next in-process architecture.
 * Based on web-streaming/server/server.cpp but streamlined for integration.
 */

#include "webrtc_server.h"
#include "../config/json_utils.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <chrono>
#include <thread>
#include <cstring>

// External globals from main.cpp
namespace video {
    extern std::atomic<bool> g_request_keyframe;
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
            else if (codec_str == "av1") codec = CodecType::AV1;
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
            else if (codec_str == "av1") codec = CodecType::AV1;
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

    // SSRC for RTP streams (static for now, could be per-peer)
    static uint32_t ssrc = 42;  // Arbitrary but consistent

    fprintf(stderr, "[WebRTC] About to add tracks for peer %s\n", peer_id.c_str());

    // Add video track (H.264, VP9, or AV1) with proper RTP packetizer
    if (codec == CodecType::H264 || codec == CodecType::VP9 || codec == CodecType::AV1) {
        auto video = rtc::Description::Video("video-stream", rtc::Description::Direction::SendOnly);

        if (codec == CodecType::H264) {
            fprintf(stderr, "[WebRTC] Creating H.264 video track\n");
            video.addH264Codec(96, "profile-level-id=42e01f;packetization-mode=1;level-asymmetry-allowed=1");
            video.addSSRC(ssrc, "video-stream", "stream1", "video-stream");
            fprintf(stderr, "[WebRTC] Calling addTrack for video\n");
            peer->video_track = peer->pc->addTrack(video);
            fprintf(stderr, "[WebRTC] addTrack returned, setting media handler\n");

            // Set up H.264 RTP packetizer
            auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
                ssrc, "video-stream", 96, rtc::H264RtpPacketizer::ClockRate
            );
            auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
                rtc::H264RtpPacketizer::Separator::LongStartSequence,
                rtpConfig
            );
            peer->video_track->setMediaHandler(packetizer);
            fprintf(stderr, "[WebRTC] Media handler set for video\n");

        } else if (codec == CodecType::VP9) {
            video.addVP9Codec(97);
            video.addSSRC(ssrc, "video-stream", "stream1", "video-stream");
            peer->video_track = peer->pc->addTrack(video);
            // VP9 packetizer would go here (if needed)

        } else if (codec == CodecType::AV1) {
            video.addAV1Codec(98);
            video.addSSRC(ssrc, "video-stream", "stream1", "video-stream");
            peer->video_track = peer->pc->addTrack(video);
            // AV1 packetizer would go here (if needed)
        }

        // CRITICAL: Only set ready=true when track is actually open!
        // This matches web-streaming behavior (server.cpp:1445-1452)
        peer->video_track->onOpen([this, peer_id]() {
            fprintf(stderr, "[WebRTC] Video track OPEN for %s - ready to send frames!\n", peer_id.c_str());

            // Find peer and set ready flag
            std::lock_guard<std::mutex> lock(peers_mutex_);
            auto it = peers_.find(peer_id);
            if (it != peers_.end()) {
                it->second->ready = true;
                // Request keyframe so new peer gets a complete picture
                video::g_request_keyframe.store(true, std::memory_order_release);
            }
        });

        peer->video_track->onClosed([peer_id]() {
            fprintf(stderr, "[WebRTC] Video track CLOSED for %s\n", peer_id.c_str());
        });

        peer->video_track->onError([peer_id](std::string error) {
            fprintf(stderr, "[WebRTC] Video track ERROR for %s: %s\n", peer_id.c_str(), error.c_str());
        });

        fprintf(stderr, "[WebRTC] Added video track with RTP packetizer (codec: %d)\n", (int)codec);
    }

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
        ssrc + 1, "audio-stream", 111, rtc::OpusRtpPacketizer::defaultClockRate
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

    // Add data channel (for PNG/WebP or metadata)
    peer->data_channel = peer->pc->createDataChannel("metadata");
    fprintf(stderr, "[WebRTC] Created data channel\n");

    peer->data_channel->onOpen([peer_id]() {
        fprintf(stderr, "[WebRTC] Data channel opened for peer %s\n", peer_id.c_str());
    });

    // NOTE: Don't set peer->ready here! It's set in video_track->onOpen() callback
    // This ensures we only send frames after the track is actually open
    peer->ready = false;  // Will be set to true in onOpen callback

    fprintf(stderr, "[WebRTC] Sent 'connected' ack to peer %s\n", peer_id.c_str());
    return peer;
}

void WebRTCServer::send_video_frame(const uint8_t* data, size_t size, bool is_keyframe) {
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

    for (const auto& [peer_id, peer] : peers_) {
        if (!peer->ready) {
            skipped_not_ready++;
            continue;
        }

        if (!peer->video_track) {
            skipped_no_track++;
            continue;
        }

        // Check if track is open before sending
        if (!peer->video_track->isOpen()) {
            skipped_not_open++;
            continue;
        }

        try {
            // Send frame with timestamp (CRITICAL for H.264 RTP)
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration<double>(now - start_time_);
            rtc::FrameInfo frameInfo(elapsed);

            // Use sendFrame() with FrameInfo for proper RTP timestamps
            // This is required for H.264/VP9/AV1 - without it, browser won't decode!
            peer->video_track->sendFrame(
                reinterpret_cast<const std::byte*>(data),
                size,
                frameInfo
            );

            // Send metadata via data channel (REQUIRED for web-streaming client)
            // Format: [cursor_x:2][cursor_y:2][cursor_visible:1][ping_seq:4][timestamps:56]
            // Total: 65 bytes
            if (peer->data_channel && peer->data_channel->isOpen()) {
                uint8_t metadata[65] = {0};
                // Cursor position (dummy for now - TODO: get real cursor from Mac)
                metadata[0] = 0; metadata[1] = 0;  // cursor_x
                metadata[2] = 0; metadata[3] = 0;  // cursor_y
                metadata[4] = 0;  // cursor_visible
                // Rest is timestamps (zeros for now)
                peer->data_channel->send(reinterpret_cast<const std::byte*>(metadata), sizeof(metadata));
            }

            sent_to++;

        } catch (const std::exception& e) {
            fprintf(stderr, "[WebRTC] Error sending video frame: %s\n", e.what());
        }
    }

    send_count++;
    if (debug_frames && (send_count % 60 == 0 || is_keyframe || sent_to == 0)) {
        fprintf(stderr, "[WebRTC] Frame #%d: sent=%d skipped(notready=%d nottrack=%d notopen=%d) kf=%d size=%zu\n",
                send_count, sent_to, skipped_not_ready, skipped_no_track, skipped_not_open, is_keyframe, size);
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
    fprintf(stderr, "[WebRTC] Codec change requested: %d\n", (int)new_codec);
    // TODO: Send reconnect message to all peers via WebSocket
    // For now, just close all peer connections (browser will reconnect)
    std::lock_guard<std::mutex> lock(peers_mutex_);
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
