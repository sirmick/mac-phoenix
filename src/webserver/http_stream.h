/*
 * HTTP Video Stream Handler
 *
 * Proxy-friendly video streaming over plain HTTP chunked transfer encoding.
 * Each chunk contains a length-prefixed frame with dirty rect metadata,
 * using the same 45-byte header format as the WebSocket path.
 *
 * No WebRTC, no WebSocket — works through any HTTP proxy, CDN, or reverse proxy.
 */

#ifndef HTTP_STREAM_H
#define HTTP_STREAM_H

#include "http_server.h"
#include "api_handlers.h"

namespace http {

/**
 * Handle a streaming video connection.
 *
 * Called on a dedicated thread per client. Owns the client_fd and closes it on exit.
 * Polls VideoOutput for new frames, computes dirty rects, encodes as PNG,
 * and sends length-prefixed frames as HTTP chunks.
 *
 * @param req The original HTTP request (for query params like ?fps=N)
 * @param client_fd Raw TCP socket fd (caller must not close it)
 * @param ctx API context providing access to video output
 */
void handle_stream(const Request& req, int client_fd, APIContext* ctx);

} // namespace http

#endif // HTTP_STREAM_H
