/*
 * HTTP Server Module
 *
 * Simple HTTP/1.1 server implementation
 */

#include "http_server.h"
#include "websocket.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <errno.h>

namespace http {

// Response implementation
Response::Response()
    : status_code_(200)
    , status_message_("OK")
    , content_type_("text/plain")
{}

void Response::set_status(int code, const std::string& message) {
    status_code_ = code;
    if (!message.empty()) {
        status_message_ = message;
    } else {
        // Default status messages
        switch (code) {
            case 200: status_message_ = "OK"; break;
            case 404: status_message_ = "Not Found"; break;
            case 500: status_message_ = "Internal Server Error"; break;
            default: status_message_ = "Unknown"; break;
        }
    }
}

void Response::set_content_type(const std::string& content_type) {
    content_type_ = content_type;
}

void Response::set_body(const std::string& body) {
    body_ = body;
}

void Response::add_header(const std::string& name, const std::string& value) {
    extra_headers_ += name + ": " + value + "\r\n";
}

std::string Response::build() const {
    std::string response = "HTTP/1.1 " + std::to_string(status_code_) + " " + status_message_ + "\r\n";
    response += "Content-Type: " + content_type_ + "\r\n";
    response += "Content-Length: " + std::to_string(body_.size()) + "\r\n";
    response += "Connection: close\r\n";
    if (!extra_headers_.empty()) {
        response += extra_headers_;
    }
    response += "\r\n";
    response += body_;
    return response;
}

Response Response::json(const std::string& json_body) {
    Response resp;
    resp.set_content_type("application/json");
    resp.set_body(json_body);
    return resp;
}

Response Response::text(const std::string& text) {
    Response resp;
    resp.set_content_type("text/plain");
    resp.set_body(text);
    return resp;
}

Response Response::html(const std::string& html) {
    Response resp;
    resp.set_content_type("text/html");
    resp.set_body(html);
    return resp;
}

Response Response::not_found() {
    Response resp;
    resp.set_status(404);
    resp.set_content_type("text/plain");
    resp.set_body("Not Found");
    return resp;
}

// Server implementation
Server::Server()
    : port_(0)
    , server_fd_(-1)
    , running_(false)
{}

Server::~Server() {
    stop();
}

bool Server::start(int port, RequestHandler handler) {
    if (running_) {
        fprintf(stderr, "HTTP: Server already running\n");
        return false;
    }

    port_ = port;
    handler_ = handler;

    // Create socket
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        fprintf(stderr, "HTTP: Failed to create socket\n");
        return false;
    }

    // Set SO_REUSEADDR
    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        fprintf(stderr, "HTTP: Warning: Failed to set SO_REUSEADDR: %s\n", strerror(errno));
    }

    // Set non-blocking
    int flags = fcntl(server_fd_, F_GETFL, 0);
    if (flags < 0) {
        fprintf(stderr, "HTTP: Failed to get socket flags: %s\n", strerror(errno));
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }
    if (fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        fprintf(stderr, "HTTP: Failed to set non-blocking mode: %s\n", strerror(errno));
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    // Bind
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "HTTP: Failed to bind port %d: %s\n", port, strerror(errno));
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    // Listen
    if (listen(server_fd_, 10) < 0) {
        fprintf(stderr, "HTTP: Failed to listen: %s\n", strerror(errno));
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    // Start thread
    running_ = true;
    thread_ = std::thread(&Server::run, this);

    fprintf(stderr, "HTTP: Server on port %d\n", port);
    return true;
}

void Server::stop() {
    running_ = false;
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Server::run() {
    while (running_) {
        struct pollfd pfd;
        pfd.fd = server_fd_;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 100);
        if (ret <= 0) continue;

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;

        // handle_client returns true if fd was handed off to a stream handler
        // (caller must NOT close it in that case)
        if (!handle_client(client_fd)) {
            close(client_fd);
        }
    }
}

void Server::register_stream_route(const std::string& path, StreamHandler handler) {
    stream_routes_[path] = std::move(handler);
}

void Server::register_websocket_route(const std::string& path, WebSocketHandler handler) {
    websocket_routes_[path] = std::move(handler);
}

static bool iequals(const std::string& a, const char* b) {
    size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

bool Server::try_websocket_upgrade(const Request& req, int client_fd) {
    auto it = websocket_routes_.find(req.path);
    if (it == websocket_routes_.end()) return false;

    auto upg_it  = req.headers.find("upgrade");
    auto conn_it = req.headers.find("connection");
    auto key_it  = req.headers.find("sec-websocket-key");
    auto ver_it  = req.headers.find("sec-websocket-version");

    if (upg_it == req.headers.end() || !iequals(upg_it->second, "websocket")) return false;
    if (conn_it == req.headers.end() ||
        conn_it->second.find("Upgrade") == std::string::npos &&
        conn_it->second.find("upgrade") == std::string::npos) return false;
    if (key_it == req.headers.end()) return false;
    if (ver_it == req.headers.end() || ver_it->second != "13") return false;

    std::string accept = WebSocket::compute_accept(key_it->second);

    std::string resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n"
        "\r\n";
    ::send(client_fd, resp.data(), resp.size(), 0);

    // Hand off fd to a detached thread that owns the WebSocket object.
    auto handler = it->second;
    Request req_copy = req;
    std::thread([handler, req_copy, client_fd]() {
        auto ws = std::make_shared<WebSocket>(client_fd);
        handler(ws, req_copy);
        ws->run_read_loop();  // blocks until close
    }).detach();

    return true;
}

bool Server::handle_client(int client_fd) {
    std::string request;
    request.reserve(8192);
    char buffer[4096];

    // Read until we have complete headers
    size_t header_end = std::string::npos;
    while (header_end == std::string::npos) {
        ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);
        if (n <= 0) return false;
        request.append(buffer, n);
        header_end = request.find("\r\n\r\n");
    }

    // Check Content-Length and read remaining body if needed.
    // Cap to a sane maximum so a hostile client can't OOM us with
    // `Content-Length: 4000000000` and a slow trickle of bytes.
    constexpr size_t kMaxBodyBytes = 16 * 1024 * 1024;
    size_t content_length = 0;
    size_t cl_pos = request.find("Content-Length: ");
    if (cl_pos == std::string::npos)
        cl_pos = request.find("content-length: ");
    if (cl_pos != std::string::npos) {
        try {
            size_t parsed = std::stoul(request.substr(cl_pos + 16));
            if (parsed > kMaxBodyBytes) {
                Response resp;
                resp.set_status(413, "Payload Too Large");
                resp.set_body("Payload Too Large");
                std::string response_str = resp.build();
                ::send(client_fd, response_str.c_str(), response_str.size(), 0);
                return false;
            }
            content_length = parsed;
        } catch (const std::exception &) {
            Response resp;
            resp.set_status(400, "Bad Request");
            resp.set_body("Invalid Content-Length");
            std::string response_str = resp.build();
            ::send(client_fd, response_str.c_str(), response_str.size(), 0);
            return false;
        }
    }

    size_t body_start = header_end + 4;
    size_t body_received = request.size() - body_start;
    while (body_received < content_length) {
        ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        request.append(buffer, n);
        body_received += n;
    }

    Request req;
    if (!parse_request(request.c_str(), request.size(), req)) {
        Response resp;
        resp.set_status(400, "Bad Request");
        resp.set_body("Bad Request");
        std::string response_str = resp.build();
        send(client_fd, response_str.c_str(), response_str.size(), 0);
        return false;
    }

    // WebSocket upgrade takes over the fd entirely.
    if (req.method == "GET" && try_websocket_upgrade(req, client_fd)) {
        return true;
    }

    // Check stream routes first (GET only)
    if (req.method == "GET") {
        auto it = stream_routes_.find(req.path);
        if (it != stream_routes_.end()) {
            // Send HTTP headers for chunked streaming.
            // Content-Type: text/event-stream tricks proxies into not buffering.
            // X-Accel-Buffering: no is an nginx-specific directive for the same.
            // The client uses fetch() not EventSource, so the MIME type is irrelevant.
            std::string headers =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/event-stream\r\n"
                "Transfer-Encoding: chunked\r\n"
                "Cache-Control: no-cache, no-store\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "X-Accel-Buffering: no\r\n"
                "Connection: keep-alive\r\n"
                "\r\n";
            send(client_fd, headers.c_str(), headers.size(), 0);

            // Hand off fd to stream handler on a detached thread.
            // The handler owns the fd and must close it.
            auto handler = it->second;
            std::thread([handler, req, client_fd]() {
                handler(req, client_fd);
            }).detach();

            // Return true — fd handed off, caller must not close it
            return true;
        }
    }

    // Call normal handler
    Response resp = handler_(req);
    std::string response_str = resp.build();
    send(client_fd, response_str.c_str(), response_str.size(), 0);
    return false;
}

bool Server::parse_request(const char* buffer, size_t length, Request& req) {
    std::string request(buffer, length);

    // Parse request line: METHOD PATH HTTP/1.1
    size_t method_end = request.find(' ');
    if (method_end == std::string::npos) return false;

    req.method = request.substr(0, method_end);

    size_t path_start = method_end + 1;
    size_t path_end = request.find(' ', path_start);
    if (path_end == std::string::npos) return false;

    req.path = request.substr(path_start, path_end - path_start);

    // Strip query string from path, preserve in req.query
    size_t query_pos = req.path.find('?');
    if (query_pos != std::string::npos) {
        req.query = req.path.substr(query_pos + 1);
        req.path = req.path.substr(0, query_pos);
    }

    // Parse headers between the request line and the empty line.
    size_t header_start = request.find("\r\n");
    size_t body_start = request.find("\r\n\r\n");
    if (header_start != std::string::npos && body_start != std::string::npos) {
        header_start += 2;
        size_t pos = header_start;
        while (pos < body_start) {
            size_t eol = request.find("\r\n", pos);
            if (eol == std::string::npos || eol > body_start) break;
            size_t colon = request.find(':', pos);
            if (colon != std::string::npos && colon < eol) {
                std::string name = request.substr(pos, colon - pos);
                // Lowercase name for case-insensitive lookup
                for (auto& c : name) c = std::tolower(static_cast<unsigned char>(c));
                size_t val_start = colon + 1;
                while (val_start < eol && std::isspace(static_cast<unsigned char>(request[val_start])))
                    ++val_start;
                size_t val_end = eol;
                while (val_end > val_start && std::isspace(static_cast<unsigned char>(request[val_end - 1])))
                    --val_end;
                req.headers[name] = request.substr(val_start, val_end - val_start);
            }
            pos = eol + 2;
        }
    }

    // Extract body (after \r\n\r\n)
    if (body_start != std::string::npos) {
        req.body = request.substr(body_start + 4);
    }

    return true;
}

} // namespace http
