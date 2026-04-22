/*
 * WebServer Main - HTTP Server Coordinator Header
 *
 * Entry point for HTTP server thread.
 */

#ifndef WEBSERVER_MAIN_H
#define WEBSERVER_MAIN_H

#include "../config/emulator_config.h"
#include "api_handlers.h"

namespace webrtc { class WebRTCServer; }

namespace webserver {

/**
 * HTTP Server Thread Main Function
 *
 * Coordinates HTTP server with static file serving, API routing, and the
 * WebRTC signaling WebSocket on /ws. This function blocks until the server
 * is stopped.
 *
 * @param config Configuration (for http_port and client_dir)
 * @param api_context API context (for API handlers)
 * @param webrtc_server Optional: if non-null, registers the /ws WebSocket route
 */
void http_server_main(const config::EmulatorConfig* config,
                      http::APIContext* api_context,
                      webrtc::WebRTCServer* webrtc_server = nullptr);

} // namespace webserver

#endif // WEBSERVER_MAIN_H
