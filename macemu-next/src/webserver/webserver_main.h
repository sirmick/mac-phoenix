/*
 * WebServer Main - HTTP Server Coordinator Header
 *
 * Entry point for HTTP server thread.
 */

#ifndef WEBSERVER_MAIN_H
#define WEBSERVER_MAIN_H

#include "../config/config_manager.h"
#include "api_handlers.h"

namespace webserver {

/**
 * HTTP Server Thread Main Function
 *
 * Coordinates HTTP server with static file serving and API routing.
 * This function blocks until the server is stopped.
 *
 * @param config Configuration (for http_port and client_dir)
 * @param api_context API context (for API handlers)
 */
void http_server_main(const config::MacemuConfig* config,
                      http::APIContext* api_context);

} // namespace webserver

#endif // WEBSERVER_MAIN_H
