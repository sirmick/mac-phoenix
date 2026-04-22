/*
 * WebServer Main - HTTP Server Coordinator
 *
 * Coordinates HTTP server with static file serving and API handlers.
 * Runs in a separate thread alongside video/audio encoder threads.
 */

#include "webserver_main.h"
#include "http_server.h"
#include "static_files.h"
#include "api_handlers.h"
#include "http_stream.h"
#include "../drivers/video/encoders/codec.h"
#include "../webrtc/webrtc_server.h"
#include <cstdio>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>

namespace webserver {

// Global running flag (defined in main.cpp)
extern std::atomic<bool> g_running;

// HTTP server thread main function
void http_server_main(const config::EmulatorConfig* config,
                      http::APIContext* api_context,
                      webrtc::WebRTCServer* webrtc_server)
{
    fprintf(stderr, "[WebServer] Starting HTTP server thread...\n");
    fprintf(stderr, "[WebServer] Codecs: PNG=yes H264=%s VP9=%s WebP=%s\n",
            codec_available(CodecType::H264) ? "yes" : "no",
            codec_available(CodecType::VP9)  ? "yes" : "no",
            codec_available(CodecType::WEBP) ? "yes" : "no");

    // Extract config values
    int port = config->http_port;
    std::string client_dir = config->client_dir;

    fprintf(stderr, "[WebServer] Port: %d\n", port);
    fprintf(stderr, "[WebServer] Client directory: %s\n", client_dir.c_str());

    // Create static file handler
    auto static_handler = std::make_unique<http::StaticFileHandler>(client_dir, config);

    // Create API router
    auto api_router = std::make_unique<http::APIRouter>(api_context);

    // Create HTTP server
    http::Server server;

    // Request handler lambda - routes to API or static files
    auto request_handler = [&](const http::Request& req) -> http::Response {
        // Log non-polling, non-spammy requests only
        if (req.path != "/api/status" && req.path != "/api/frame" &&
            req.path != "/api/log" && req.path != "/api/mouse") {
            fprintf(stderr, "[HTTP] %s %s\n", req.method.c_str(), req.path.c_str());
        }

        // Try API routes first
        bool handled = false;
        http::Response resp = api_router->handle(req, &handled);
        if (handled) {
            return resp;
        }

        // Try static files
        if (static_handler->handles(req.path)) {
            return static_handler->serve(req.path);
        }

        // 404 Not Found
        return http::Response::not_found();
    };

    // Register stream route (before start, since start runs the accept loop)
    server.register_stream_route("/api/stream",
        [api_context](const http::Request& req, int fd) {
            http::handle_stream(req, fd, api_context);
        });

    // Register WebRTC signaling WebSocket on /ws (shares this HTTP listener).
    if (webrtc_server) {
        webrtc_server->register_routes(server);
    }

    // Start HTTP server
    if (!server.start(port, request_handler)) {
        fprintf(stderr, "[WebServer] ERROR: Failed to start HTTP server on port %d\n", port);
        return;
    }

    fprintf(stderr, "[WebServer] HTTP server listening on http://0.0.0.0:%d\n", port);
    fprintf(stderr, "[WebServer] Open http://localhost:%d in your browser\n", port);

    // Block and wait for shutdown signal
    while (g_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    fprintf(stderr, "[WebServer] Shutting down HTTP server...\n");
}

} // namespace webserver
