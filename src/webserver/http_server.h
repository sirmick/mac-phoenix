/*
 * HTTP Server Module
 *
 * Simple HTTP/1.1 server for serving static files and JSON APIs.
 * Built on QTcpServer/QTcpSocket so the listener and per-request I/O
 * are cross-platform; long-poll stream handlers and WebSocket upgrades
 * detach the underlying file descriptor (via dup()) and keep using
 * POSIX-style send/recv on it.
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <string>
#include <thread>
#include <atomic>
#include <future>
#include <functional>
#include <memory>
#include <unordered_map>

class QTcpSocket;

namespace http {

class WebSocket;

/**
 * HTTP Request Information
 */
struct Request {
    std::string method;      // GET, POST, etc.
    std::string path;        // URL path without query string
    std::string query;       // Query string (after ?)
    std::string body;        // Request body content

    // Lowercase-keyed headers. Used for WebSocket upgrade detection.
    std::unordered_map<std::string, std::string> headers;
};

/**
 * HTTP Response Builder
 */
class Response {
public:
    Response();

    void set_status(int code, const std::string& message = "");
    void set_content_type(const std::string& content_type);
    void set_body(const std::string& body);
    void add_header(const std::string& name, const std::string& value);

    const std::string& body() const { return body_; }

    std::string build() const;

    // Convenience methods
    static Response json(const std::string& json_body);
    static Response text(const std::string& text);
    static Response html(const std::string& html);
    static Response not_found();

private:
    int status_code_;
    std::string status_message_;
    std::string content_type_;
    std::string body_;
    std::string extra_headers_;
};

/**
 * HTTP Server
 *
 * Listens on a port and handles HTTP requests.
 * Uses a callback for request routing/handling.
 */
class Server {
public:
    // Request handler callback: receives request, returns response
    using RequestHandler = std::function<Response(const Request&)>;

    // Stream handler callback: takes over the client socket for long-lived streaming.
    // The handler runs on a detached thread, owns the fd, and must close it when done.
    using StreamHandler = std::function<void(const Request& req, int client_fd)>;

    // WebSocket handler: the HTTP server performs the RFC 6455 upgrade handshake,
    // then runs this callback on a detached thread with a live WebSocket. The
    // handler should register callbacks on the socket and then call
    // ws->run_read_loop() (blocks until the peer closes). The fd is owned by
    // the WebSocket.
    using WebSocketHandler = std::function<void(std::shared_ptr<WebSocket> ws, const Request& req)>;

    Server();
    ~Server();

    // Start server on specified port
    bool start(int port, RequestHandler handler);

    // Register a stream route that takes over the client socket.
    // When a GET request matches this path, HTTP headers are sent and the
    // StreamHandler is called on a new thread with ownership of the fd.
    void register_stream_route(const std::string& path, StreamHandler handler);

    // Register a WebSocket route. When a GET request matching this path
    // arrives with Upgrade: websocket, the server completes the handshake and
    // dispatches to the handler on a new thread.
    void register_websocket_route(const std::string& path, WebSocketHandler handler);

    // Stop server and wait for thread to join
    void stop();

    // Check if server is running
    bool is_running() const { return running_; }

private:
    // run() owns the QTcpServer for its lifetime and signals listen() success
    // back through the promise so start() can return false on bind failure.
    void run(std::shared_ptr<std::promise<bool>> listen_result);
    bool handle_client(QTcpSocket* socket);  // returns true if fd was handed off
    bool parse_request(const char* buffer, size_t length, Request& req);
    bool try_websocket_upgrade(const Request& req, QTcpSocket* socket);  // true = handed off

    int port_;
    std::atomic<bool> running_;
    std::thread thread_;
    RequestHandler handler_;

    // Stream routes: path -> handler
    std::unordered_map<std::string, StreamHandler> stream_routes_;
    // WebSocket routes: path -> handler
    std::unordered_map<std::string, WebSocketHandler> websocket_routes_;
};

} // namespace http

#endif // HTTP_SERVER_H
