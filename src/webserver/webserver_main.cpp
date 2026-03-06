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
#include "../config/config_manager.h"
#include <cstdio>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>

namespace webserver {

// Global running flag (defined in main.cpp)
extern std::atomic<bool> g_running;

// HTTP server thread main function
void http_server_main(const config::MacemuConfig* config,
                      http::APIContext* api_context)
{
    fprintf(stderr, "[WebServer] Starting HTTP server thread...\n");

    // Extract config values
    int port = config->web.http_port;
    std::string client_dir = config->web.client_dir;

    fprintf(stderr, "[WebServer] Port: %d\n", port);
    fprintf(stderr, "[WebServer] Client directory: %s\n", client_dir.c_str());

    // Create static file handler (with config path for template injection)
    auto static_handler = std::make_unique<http::StaticFileHandler>(client_dir, api_context->prefs_path);

    // Create API router
    auto api_router = std::make_unique<http::APIRouter>(api_context);

    // Create HTTP server
    http::Server server;

    // Request handler lambda - routes to API or static files
    auto request_handler = [&](const http::Request& req) -> http::Response {
        fprintf(stderr, "[HTTP] %s %s\n", req.method.c_str(), req.path.c_str());

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

    // Start HTTP server
    if (!server.start(port, request_handler)) {
        fprintf(stderr, "[WebServer] ERROR: Failed to start HTTP server on port %d\n", port);
        return;
    }

    fprintf(stderr, "[WebServer] HTTP server listening on http://0.0.0.0:%d\n", port);
    fprintf(stderr, "[WebServer] Open http://localhost:%d in your browser\n", port);

    // Block and wait for shutdown signal (server runs in its own thread)
    while (g_running.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    fprintf(stderr, "[WebServer] Shutting down HTTP server...\n");
    // Server destructor will stop the server when this function exits
}

} // namespace webserver
