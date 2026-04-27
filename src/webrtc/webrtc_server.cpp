/*
 * WebRTC Server Implementation
 *
 * Simplified WebRTC signaling server for mac-phoenix in-process architecture.
 * Based on web-streaming/server/server.cpp but streamlined for integration.
 */

#include "webrtc_server.h"
#include "../drivers/audio/encoders/audio_config.h"
#include "../config/json_utils.h"
#include "../webserver/http_server.h"
#include "../webserver/websocket.h"
#include "../ipc/ipc_protocol.h"
#include "../core/boot_progress.h"
#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <chrono>
#include <shared_mutex>
#include <thread>
#include <cstring>

// ADB input functions (from adb.cpp)
extern void ADBMouseMoved(int x, int y);
extern void ADBMouseDown(int button);
extern void ADBMouseUp(int button);
extern void ADBSetRelMouseMode(bool relative);
extern void ADBKeyDown(int code);
extern void ADBKeyUp(int code);

// IPC client for subprocess mode (set in main.cpp)
#include "../ipc/ipc_client.h"
extern IPCClient* g_ipc_client;

// External globals from main.cpp
namespace video {
    extern std::atomic<bool> g_request_keyframe;
}

/**
 * VP9 RTP Packetizer (RFC 9628)
 *
 * Fragments VP9 frames into MTU-sized RTP packets with VP9 payload descriptors.
 * Includes picture ID for frame tracking and scalability structure (SS) on
 * keyframes for resolution info — required by Chrome's VP9 depacketizer.
 *
 * Payload descriptor byte:  I|P|L|F|B|E|V|Z
 *   I (bit 7) = Picture ID present
 *   P (bit 6) = Inter-picture predicted (0 for keyframes)
 *   B (bit 3) = Start of frame
 *   E (bit 2) = End of frame
 *   V (bit 1) = Scalability structure present (keyframe first packet only)
 */
class VP9RtpPacketizer : public rtc::RtpPacketizer {
public:
    VP9RtpPacketizer(std::shared_ptr<rtc::RtpPacketizationConfig> rtpConfig,
                     size_t maxFragmentSize = DefaultMaxFragmentSize)
        : RtpPacketizer(std::move(rtpConfig)), maxFragmentSize_(maxFragmentSize) {}

    /// Call before sendFrame() to set metadata for the next frame
    void prepareFrame(bool is_keyframe, uint16_t width, uint16_t height) {
        is_keyframe_ = is_keyframe;
        width_ = width;
        height_ = height;
    }

private:
    std::vector<rtc::binary> fragment(rtc::binary data) override {
        std::vector<rtc::binary> fragments;

        // Always use 15-bit PID for consistency (avoids 7→15 bit transition issues)
        uint16_t pid = picture_id_;
        picture_id_ = (picture_id_ + 1) & 0x7FFF;
        int pid_bytes = 2;

        // Build scalability structure for keyframe first packet:
        // [N_S(3)|Y(1)|G(1)|RES(3)] [WIDTH_HI] [WIDTH_LO] [HEIGHT_HI] [HEIGHT_LO]
        std::vector<uint8_t> ss_data;
        if (is_keyframe_) {
            ss_data.push_back(0x10);  // N_S=0 (1 layer), Y=1 (resolution), G=0
            ss_data.push_back(static_cast<uint8_t>(width_ >> 8));
            ss_data.push_back(static_cast<uint8_t>(width_ & 0xFF));
            ss_data.push_back(static_cast<uint8_t>(height_ >> 8));
            ss_data.push_back(static_cast<uint8_t>(height_ & 0xFF));
        }

        // Base descriptor: I=1 always, P=1 for inter frames
        uint8_t base_desc = 0x80;  // I=1
        if (!is_keyframe_) base_desc |= 0x40;  // P=1

        // Header size for first packet (with SS) vs subsequent packets
        int first_header_size = 1 + pid_bytes + (int)ss_data.size();  // desc + PID + SS
        int other_header_size = 1 + pid_bytes;  // desc + PID

        size_t first_payload_max = maxFragmentSize_ - first_header_size;
        size_t other_payload_max = maxFragmentSize_ - other_header_size;

        size_t offset = 0;
        bool first = true;

        while (offset < data.size()) {
            size_t payload_max = first ? first_payload_max : other_payload_max;
            size_t chunkSize = std::min(payload_max, data.size() - offset);
            bool last = (offset + chunkSize >= data.size());
            int header_size = first ? first_header_size : other_header_size;

            rtc::binary frag(header_size + chunkSize);
            int pos = 0;

            // Byte 0: descriptor
            uint8_t descriptor = base_desc;
            if (first) descriptor |= 0x08;  // B=1
            if (last) descriptor |= 0x04;   // E=1
            if (first && is_keyframe_) descriptor |= 0x02;  // V=1
            frag[pos++] = static_cast<std::byte>(descriptor);

            // Picture ID (I=1, always 15-bit / M=1)
            frag[pos++] = static_cast<std::byte>(0x80 | ((pid >> 8) & 0x7F));  // M=1, PID[14:8]
            frag[pos++] = static_cast<std::byte>(pid & 0xFF);                   // PID[7:0]

            // Scalability structure (V=1, first packet of keyframe only)
            if (first && is_keyframe_) {
                for (uint8_t b : ss_data) {
                    frag[pos++] = static_cast<std::byte>(b);
                }
            }

            // VP9 payload data
            std::copy(data.begin() + offset,
                      data.begin() + offset + chunkSize,
                      frag.begin() + pos);

            fragments.push_back(std::move(frag));
            offset += chunkSize;
            first = false;
        }

        return fragments;
    }

    size_t maxFragmentSize_;
    uint16_t picture_id_ = 0;
    bool is_keyframe_ = false;
    uint16_t width_ = 0;
    uint16_t height_ = 0;
};

/**
 * Process binary input message from browser data channel.
 * Protocol: [type:uint8] [payload...]
 *   1 = mouse move relative: dx:int16LE, dy:int16LE, ts:float64LE (13 bytes)
 *   2 = mouse button: button:uint8, down:uint8, ts:float64LE (11 bytes)
 *   3 = key: mac_keycode:uint16LE, down:uint8, ts:float64LE (12 bytes)
 *       — keycode is a Mac ADB virtual keycode (0x00–0x7F), pre-mapped by
 *         the browser from KeyboardEvent.code (see EVENT_CODE_TO_MAC in
 *         client.js). Only the low byte is meaningful; the upper byte is
 *         reserved.
 *   5 = mouse move absolute: x:uint16LE, y:uint16LE, ts:float64LE (13 bytes)
 *   6 = mouse mode change: mode:uint8 (2 bytes, 0=absolute, 1=relative)
 */
static void process_input_message(const std::byte* data, size_t size) {
    if (size < 2) return;

    uint8_t type = static_cast<uint8_t>(data[0]);

    // PPC subprocess mode: send via IPC socket
    if (g_ipc_client && g_ipc_client->is_connected()) {
        // Track button state so move events carry correct button mask
        static uint8_t ipc_buttons = 0;

        switch (type) {
            case 1: { // Mouse move (relative)
                if (size < 5) return;
                int16_t dx, dy;
                std::memcpy(&dx, data + 1, 2);
                std::memcpy(&dy, data + 3, 2);
                g_ipc_client->send_mouse(dx, dy, ipc_buttons, false);
                break;
            }
            case 2: { // Mouse button
                if (size < 3) return;
                uint8_t button = static_cast<uint8_t>(data[1]);
                uint8_t down = static_cast<uint8_t>(data[2]);
                uint8_t mask = (button == 0) ? IPC_MOUSE_LEFT : IPC_MOUSE_RIGHT;
                if (down) ipc_buttons |= mask;
                else      ipc_buttons &= ~mask;
                g_ipc_client->send_mouse(0, 0, ipc_buttons, false);
                break;
            }
            case 3: { // Key
                if (size < 4) return;
                uint16_t wire;
                std::memcpy(&wire, data + 1, 2);
                uint8_t mac_keycode = wire & 0x7F;  // Mac ADB scancode range 0x00–0x7F
                uint8_t down = static_cast<uint8_t>(data[3]);
                g_ipc_client->send_key(mac_keycode, down != 0);
                break;
            }
            case 5: { // Mouse move (absolute)
                if (size < 5) return;
                uint16_t x, y;
                std::memcpy(&x, data + 1, 2);
                std::memcpy(&y, data + 3, 2);
                g_ipc_client->send_mouse(x, y, ipc_buttons, true);
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
            uint16_t wire;
            std::memcpy(&wire, data + 1, 2);
            uint8_t mac_keycode = wire & 0x7F;  // Mac ADB scancode range 0x00–0x7F
            uint8_t down = static_cast<uint8_t>(data[3]);
            if (down)
                ADBKeyDown(mac_keycode);
            else
                ADBKeyUp(mac_keycode);
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

bool WebRTCServer::init() {
    start_time_ = std::chrono::steady_clock::now();

    // Initialize libdatachannel logging (used for PeerConnection only — we
    // don't use rtc::WebSocketServer anymore; signaling rides the HTTP
    // server's in-process WebSocket implementation).
    rtc::InitLogger(rtc::LogLevel::Warning);
    rtc::Preload();

    initialized_ = true;
    return true;
}

void WebRTCServer::register_routes(http::Server& http_server) {
    http_server.register_websocket_route("/ws", [this](std::shared_ptr<http::WebSocket> ws,
                                                       const http::Request& /*req*/) {
        // Per-connection peer id slot. Written when the client's "connect" or
        // "offer" message arrives; read by on_close for cleanup.
        auto peer_id_slot = std::make_shared<std::string>();

        ws->on_text([this, ws, peer_id_slot](std::string msg) {
            process_signaling(ws, std::move(msg), peer_id_slot);
        });

        ws->on_binary([](std::vector<std::byte> data) {
            process_input_message(data.data(), data.size());
        });

        ws->on_close([this, peer_id_slot]() {
            if (!peer_id_slot->empty()) {
                remove_peer(*peer_id_slot);
            }
        });

        // Fire synchronous welcome (matches the old libdatachannel behavior).
        ws->send(std::string("{\"type\":\"welcome\",\"peerId\":\"server\"}"));
    });

    fprintf(stderr, "[WebRTC] WebSocket signaling registered on /ws (shared HTTP port)\n");
}

void WebRTCServer::shutdown() {
    if (!initialized_) return;

    fprintf(stderr, "[WebRTC] Shutting down WebRTC server\n");
    std::lock_guard<std::mutex> lock(peers_mutex_);
    peers_.clear();
    initialized_ = false;
}

void WebRTCServer::remove_peer(const std::string& peer_id) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    auto it = peers_.find(peer_id);
    if (it != peers_.end()) {
        peers_.erase(it);
        peer_count_--;
    }
}

static CodecType parse_codec(const std::string& codec_str) {
    if (codec_str == "vp9") return CodecType::VP9;
    if (codec_str == "png") return CodecType::PNG;
    if (codec_str == "webp") return CodecType::WEBP;
    return CodecType::H264;
}

static const char* codec_name(CodecType c) {
    switch (c) {
        case CodecType::H264: return "h264";
        case CodecType::VP9:  return "vp9";
        case CodecType::AV1:  return "av1";
        case CodecType::WEBP: return "webp";
        case CodecType::PNG:  return "png";
    }
    return "h264";
}

void WebRTCServer::process_signaling(std::shared_ptr<http::WebSocket> ws, const std::string& message,
                                     std::shared_ptr<std::string> peer_id_slot) {
    try {
        auto j = nlohmann::json::parse(message);
        std::string type = json_utils::get_string(j, "type");

        if (type == "ping") {
            // Heartbeat: reply with pong to keep the connection alive and
            // let the client detect dead sockets (nginx idle-timeout, etc.).
            ws->send(std::string("{\"type\":\"pong\"}"));
            return;
        }

        if (type == "connect") {
            // Client is requesting connection - server creates offer
            std::string peer_id = json_utils::get_string(j, "peerId", "client-" + std::to_string(peer_count_.load()));
            CodecType codec = parse_codec(json_utils::get_string(j, "codec", "h264"));

            auto peer = create_peer_connection(peer_id, codec, ws);
            if (!peer) {
                fprintf(stderr, "[WebRTC] Failed to create peer connection\n");
                return;
            }

            {
                std::lock_guard<std::mutex> lock(peers_mutex_);
                peers_[peer_id] = peer;
                peer_count_++;
            }
            *peer_id_slot = peer_id;

            // Send "connected" acknowledgment
            nlohmann::json ack;
            ack["type"] = "connected";
            ack["peer_id"] = peer_id;
            ack["codec"] = codec_name(codec);
            ws->send(ack.dump());

            // For H.264/VP9: create_peer_connection() calls setLocalDescription()
            // after addTrack, which drives onLocalDescription → "offer" over ws.
            // PNG/WebP peers have no PC, so no offer — frames start flowing as
            // soon as the encoder has one ready.

        } else if (type == "answer") {
            std::string sdp = json_utils::get_string(j, "sdp");

            std::lock_guard<std::mutex> lock(peers_mutex_);
            if (peer_id_slot->empty()) {
                fprintf(stderr, "[WebRTC] Received answer but no peer associated with this WebSocket\n");
                return;
            }
            auto peer_it = peers_.find(*peer_id_slot);
            if (peer_it != peers_.end() && peer_it->second->pc) {
                peer_it->second->pc->setRemoteDescription(rtc::Description(sdp, "answer"));
            }

        } else if (type == "offer") {
            std::string peer_id = json_utils::get_string(j, "peerId");
            std::string sdp = json_utils::get_string(j, "sdp");
            CodecType codec = parse_codec(json_utils::get_string(j, "codec", "h264"));

            auto peer = create_peer_connection(peer_id, codec, ws);
            if (!peer || !peer->pc) {
                fprintf(stderr, "[WebRTC] Offer received but peer has no PeerConnection (codec=%s)\n",
                        codec_name(codec));
                return;
            }

            {
                std::lock_guard<std::mutex> lock(peers_mutex_);
                peers_[peer_id] = peer;
                peer_count_++;
            }
            *peer_id_slot = peer_id;

            peer->pc->setRemoteDescription(rtc::Description(sdp, "offer"));

        } else if (type == "candidate") {
            std::string peer_id = json_utils::get_string(j, "peerId");
            std::string candidate = json_utils::get_string(j, "candidate");
            std::string sdp_mid = json_utils::get_string(j, "sdpMid");

            std::lock_guard<std::mutex> lock(peers_mutex_);
            auto it = peers_.find(peer_id);
            if (it != peers_.end() && it->second->pc) {
                it->second->pc->addRemoteCandidate(rtc::Candidate(candidate, sdp_mid));
            }
        }

    } catch (const std::exception& e) {
        fprintf(stderr, "[WebRTC] Signaling error: %s\n", e.what());
    }
}

std::shared_ptr<PeerConnection> WebRTCServer::create_peer_connection(const std::string& peer_id, CodecType codec,
                                                                     std::shared_ptr<http::WebSocket> ws_shared) {
    auto peer = std::make_shared<PeerConnection>();
    peer->id = peer_id;
    peer->codec = codec;
    peer->is_webrtc = (codec == CodecType::H264 || codec == CodecType::VP9);
    peer->ws = ws_shared;  // weak_ptr; the /ws handler closures own the shared_ptr

    if (!peer->is_webrtc) {
        // PNG/WebP: WS-only peer. No PeerConnection, no ICE, no RTP tracks.
        // Frames and input ride the signaling WebSocket.
        peer->ready = true;
        video::g_request_keyframe.store(true, std::memory_order_release);
        return peer;
    }

    // Configure peer connection (H.264/VP9 only)
    rtc::Configuration config;
    // Note: STUN disabled for localhost/LAN mode (matches web-streaming default)
    // config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    // Allow large video frames (up to 16MB for high-res content)
    config.maxMessageSize = 16 * 1024 * 1024;

    peer->pc = std::make_shared<rtc::PeerConnection>(config);

    // Capture ws as weak_ptr so closures don't keep the socket alive past close.
    std::weak_ptr<http::WebSocket> ws_weak = ws_shared;

    // Send local description (offer or answer) to browser
    peer->pc->onLocalDescription([ws_weak, peer_id](rtc::Description desc) {
        auto ws = ws_weak.lock();
        if (!ws) {
            fprintf(stderr, "[WebRTC] WebSocket gone when sending %s for %s\n",
                    desc.typeString().c_str(), peer_id.c_str());
            return;
        }
        nlohmann::json msg;
        msg["type"] = desc.typeString();
        msg["sdp"] = std::string(desc);
        ws->send(msg.dump());
    });

    // Send ICE candidates to browser
    peer->pc->onLocalCandidate([ws_weak](rtc::Candidate cand) {
        auto ws = ws_weak.lock();
        if (!ws) return;
        nlohmann::json candidate;
        candidate["type"] = "candidate";
        candidate["candidate"] = std::string(cand);
        candidate["mid"] = cand.mid();
        ws->send(candidate.dump());
    });

    peer->pc->onStateChange([peer_id](rtc::PeerConnection::State state) {
        if (state == rtc::PeerConnection::State::Connected)
            fprintf(stderr, "[WebRTC] Peer %s connected\n", peer_id.c_str());
        else if (state == rtc::PeerConnection::State::Failed)
            fprintf(stderr, "[WebRTC] Peer %s connection failed\n", peer_id.c_str());
    });

    peer->pc->onIceStateChange([](rtc::PeerConnection::IceState) {});
    peer->pc->onGatheringStateChange([](rtc::PeerConnection::GatheringState) {});

    // SSRC for RTP streams — increment per peer to avoid browser RTP state confusion
    // when switching between codecs (e.g., H264 → VP9) on the same SSRC
    static std::atomic<uint32_t> ssrc_counter{42};
    uint32_t ssrc = ssrc_counter.fetch_add(2, std::memory_order_relaxed);  // +2 because audio uses ssrc+1


    // Add video track (H.264 and VP9 use RTP)
    {
        auto video = rtc::Description::Video("video-stream", rtc::Description::Direction::SendOnly);

        if (codec == CodecType::H264)
            video.addH264Codec(96, "profile-level-id=42e01f;packetization-mode=1;level-asymmetry-allowed=1");
        else
            video.addVP9Codec(96);

        video.addSSRC(ssrc, "video-stream", "stream1", "video-stream");
        peer->video_track = peer->pc->addTrack(video);

        // Set up RTP packetizer
        auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
            ssrc, "video-stream", 96, rtc::RtpPacketizer::VideoClockRate
        );
        auto videoSrReporter = std::make_shared<rtc::RtcpSrReporter>(rtpConfig);
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
            // VP9: Use custom packetizer with RFC 9628 payload descriptor
            auto packetizer = std::make_shared<VP9RtpPacketizer>(rtpConfig);
            packetizer->addToChain(videoSrReporter);
            packetizer->addToChain(videoNackResponder);
            peer->video_track->setMediaHandler(packetizer);
            peer->vp9_packetizer = packetizer;
        }

        // CRITICAL: Only set ready=true when track is actually open!
        peer->video_track->onOpen([this, peer_id]() {
            std::lock_guard<std::mutex> lock(peers_mutex_);
            auto it = peers_.find(peer_id);
            if (it != peers_.end()) {
                it->second->ready = true;
                video::g_request_keyframe.store(true, std::memory_order_release);
            }
        });

        peer->video_track->onClosed([](){ });

        peer->video_track->onError([peer_id](std::string error) {
            fprintf(stderr, "[WebRTC] Video track ERROR for %s: %s\n", peer_id.c_str(), error.c_str());
        });
    }

    // Add audio track (Opus) with proper RTP packetizer
    auto audio = rtc::Description::Audio("audio-stream", rtc::Description::Direction::SendOnly);
    audio.addOpusCodec(OPUS_PAYLOAD_TYPE, WEBRTC_OPUS_PROFILE);
    audio.addSSRC(ssrc + 1, "audio-stream", "stream1", "audio-stream");
    peer->audio_track = peer->pc->addTrack(audio);

    // Set up Opus RTP packetizer (following web-streaming pattern)
    auto rtpConfigAudio = std::make_shared<rtc::RtpPacketizationConfig>(
        ssrc + 1, "audio-stream", OPUS_PAYLOAD_TYPE, rtc::OpusRtpPacketizer::DefaultClockRate
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

    peer->audio_track->onOpen([]() {});

    // Input events and cursor metadata ride the signaling WebSocket (see onClient
    // binary-dispatch handler and send_video_frame cursor path).

    // Kick negotiation. addTrack() alone does not trigger it in libdatachannel;
    // the old code got this for free from createDataChannel("metadata", ...),
    // which was removed with the single-port refactor.
    peer->pc->setLocalDescription();

    // NOTE: Don't set peer->ready here! It's set in video_track->onOpen() callback
    // This ensures we only send frames after the track is actually open
    peer->ready = false;  // Will be set to true in onOpen callback

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
    [[maybe_unused]] int skipped_no_track = 0;
    int skipped_not_open = 0;

    // Build metadata for data channel
    // Format: [cursor_x:2][cursor_y:2][cursor_visible:1]
    // Total: 5 bytes
    uint8_t metadata[5] = {0};
    int mx = 0, my = 0;
    if (g_ipc_client) {
        // Subprocess mode: read cursor from IPC SHM (if still connected).
        // Shared lock prevents a concurrent stop()/restart from munmapping
        // the page underneath us.
        std::shared_lock<std::shared_mutex> shm_lock(g_ipc_shm_mutex);
        if (g_ipc_client->is_connected() && g_ipc_client->shm()) {
            const IPCBuffer* buf = g_ipc_client->shm();
            mx = IPC_ATOMIC_LOAD(buf->shm_cursor_x);
            my = IPC_ATOMIC_LOAD(buf->shm_cursor_y);
        }
        // else: subprocess disconnected, use zeros
    } else {
        // In-process mode: read directly from Mac low-memory globals
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
                // PNG/WEBP: send frame via WebSocket with 45-byte metadata header
                auto ws = peer->ws.lock();
                if (!ws || !ws->is_open()) {
                    skipped_not_open++;
                    continue;
                }

                // Build header: [t1:8][x:4][y:4][w:4][h:4][fw:4][fh:4][t4:8][cursor:5]
                std::vector<std::byte> frame_with_header(45 + size);
                uint8_t* buf = reinterpret_cast<uint8_t*>(frame_with_header.data());
                std::memset(buf, 0, 8);  // t1
                uint32_t dx = static_cast<uint32_t>(dirty_x);
                uint32_t dy = static_cast<uint32_t>(dirty_y);
                uint32_t dw = static_cast<uint32_t>(dirty_width > 0 ? dirty_width : width);
                uint32_t dh = static_cast<uint32_t>(dirty_height > 0 ? dirty_height : height);
                uint32_t fw = static_cast<uint32_t>(frame_width > 0 ? frame_width : width);
                uint32_t fh = static_cast<uint32_t>(frame_height > 0 ? frame_height : height);
                std::memcpy(buf + 8, &dx, 4);
                std::memcpy(buf + 12, &dy, 4);
                std::memcpy(buf + 16, &dw, 4);
                std::memcpy(buf + 20, &dh, 4);
                std::memcpy(buf + 24, &fw, 4);
                std::memcpy(buf + 28, &fh, 4);
                std::memset(buf + 32, 0, 8);  // t4
                std::memcpy(buf + 40, &cx, 2);
                std::memcpy(buf + 42, &cy, 2);
                buf[44] = metadata[4];
                std::memcpy(buf + 45, data, size);

                ws->send(frame_with_header);
                sent_to++;
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

                // VP9: set keyframe/resolution metadata before sending
                if (peer->vp9_packetizer) {
                    static_cast<VP9RtpPacketizer*>(peer->vp9_packetizer.get())
                        ->prepareFrame(is_keyframe,
                            static_cast<uint16_t>(width), static_cast<uint16_t>(height));
                }

                peer->video_track->sendFrame(
                    reinterpret_cast<const std::byte*>(data),
                    size,
                    frameInfo
                );

                // Send cursor metadata over WS (was data-channel; now same transport as signaling)
                if (auto ws = peer->ws.lock(); ws && ws->is_open()) {
                    nlohmann::json cursor_msg;
                    cursor_msg["type"] = "cursor";
                    cursor_msg["x"] = cx;
                    cursor_msg["y"] = cy;
                    cursor_msg["visible"] = metadata[4] != 0;
                    ws->send(cursor_msg.dump());
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

    // Frame-based timing (like legacy): count frames, not wall clock
    static uint64_t audio_frame_count = 0;
    auto elapsed = std::chrono::duration<double>(audio_frame_count * 0.020); // 20ms per frame
    rtc::FrameInfo frameInfo(elapsed);
    audio_frame_count++;

    std::lock_guard<std::mutex> lock(peers_mutex_);
    for (const auto& [peer_id, peer] : peers_) {
        if (!peer->ready || !peer->audio_track) continue;
        if (!peer->audio_track->isOpen()) continue;

        try {
            peer->audio_track->sendFrame(
                reinterpret_cast<const std::byte*>(data),
                size,
                frameInfo);
        } catch (const std::exception& e) {
            fprintf(stderr, "[WebRTC] Error sending audio frame: %s\n", e.what());
        }
    }
}

void reset_webrtc_peers() {
    if (g_server) {
        g_server->reset_peer_state();
    }
}

void WebRTCServer::reset_peer_state() {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    for (const auto& [peer_id, peer] : peers_) {
        peer->needs_keyframe = true;
        peer->needs_first_frame = true;
    }
    fprintf(stderr, "[WebRTC] Reset peer state for %zu peers (will request keyframe)\n", peers_.size());
}

void WebRTCServer::notify_codec_change(CodecType new_codec) {
    const char* name = codec_name(new_codec);
    fprintf(stderr, "[WebRTC] Codec change requested: %s\n", name);

    nlohmann::json msg;
    msg["type"] = "reconnect";
    msg["reason"] = "codec_change";
    msg["codec"] = name;
    std::string msg_str = msg.dump();

    std::lock_guard<std::mutex> lock(peers_mutex_);

    for (const auto& [peer_id, peer] : peers_) {
        if (auto ws = peer->ws.lock()) {
            try { ws->send(msg_str); }
            catch (const std::exception& e) {
                fprintf(stderr, "[WebRTC] Error sending reconnect to peer %s: %s\n",
                        peer_id.c_str(), e.what());
            }
        }
        // Close any WebRTC PeerConnections so the browser gets a clean
        // teardown; the reconnect message tells it to re-send "connect".
        if (peer->pc) {
            peer->pc->close();
        }
    }
}

//
// WebRTC Server Thread Main
//

void webrtc_server_main(WebRTCServer* /*server*/, std::atomic<bool>* running) {
    fprintf(stderr, "[WebRTC] Server thread starting\n");

    // Server is already initialized by main thread before launching this thread
    // Just keep thread alive until shutdown signal
    while (running->load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    fprintf(stderr, "[WebRTC] Server thread shutting down\n");
}

} // namespace webrtc
