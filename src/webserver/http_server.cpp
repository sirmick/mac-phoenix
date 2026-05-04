/*
 * HTTP Server Module
 *
 * HTTP/1.1 server backed by QTcpServer/QTcpSocket. The listen accept
 * loop and per-request read/write use Qt's synchronous waitForX APIs,
 * so the server runs on a dedicated std::thread without needing a Qt
 * event loop. For long-poll stream and WebSocket routes the underlying
 * file descriptor is dup'd and handed to a worker thread that keeps
 * using POSIX send/recv on its own copy — the QTcpSocket's fd is then
 * closed by Qt as normal, but the dup'd reference keeps the TCP
 * connection alive.
 */

#include "http_server.h"
#include "websocket.h"
#include <QByteArray>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unistd.h>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <errno.h>

namespace http {

// QThread subclass that drives the accept loop. The actual loop body lives
// in Server::server_loop so it can keep direct access to private members
// without exposing them through the QObject interface.
class HttpServerThread : public QThread {
public:
    HttpServerThread(Server* owner, std::shared_ptr<std::promise<bool>> listen_result)
        : owner_(owner), listen_result_(std::move(listen_result)) {}

protected:
    void run() override { owner_->server_loop(listen_result_); }

private:
    Server* owner_;
    std::shared_ptr<std::promise<bool>> listen_result_;
};

// Response implementation — unchanged from the POSIX-backed version.
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

    auto listen_result = std::make_shared<std::promise<bool>>();
    auto listen_future = listen_result->get_future();

    running_ = true;
    thread_ = std::make_unique<HttpServerThread>(this, listen_result);
    thread_->start();

    bool ok = listen_future.get();
    if (!ok) {
        running_ = false;
        thread_->wait();
        thread_.reset();
        return false;
    }

    fprintf(stderr, "HTTP: Server on port %d\n", port);
    return true;
}

void Server::stop() {
    running_ = false;
    if (thread_) {
        thread_->wait();
        thread_.reset();
    }
}

void Server::server_loop(std::shared_ptr<std::promise<bool>> listen_result) {
    // Bypass QTcpServer for accept entirely. Qt's accept machinery
    // depends on QSocketNotifier signals dispatched via a Qt event loop;
    // without one (we run on a plain std::thread), Qt's pendingConnections
    // buffer fills up after ~5 simultaneous arrivals and waitForNewConnection
    // stops returning new sockets. Raw POSIX accept() on the listening fd
    // has no such state — it's just a kernel syscall.
    int listen_fd = ::socket(AF_INET6, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        fprintf(stderr, "HTTP: socket() failed: %s\n", strerror(errno));
        listen_result->set_value(false);
        return;
    }
    int reuse = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    int v6only = 0;
    ::setsockopt(listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_addr   = in6addr_any;
    addr.sin6_port   = htons((uint16_t)port_);
    if (::bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "HTTP: bind(%d) failed: %s\n", port_, strerror(errno));
        ::close(listen_fd);
        listen_result->set_value(false);
        return;
    }
    if (::listen(listen_fd, 128) < 0) {
        fprintf(stderr, "HTTP: listen() failed: %s\n", strerror(errno));
        ::close(listen_fd);
        listen_result->set_value(false);
        return;
    }
    listen_result->set_value(true);

    while (running_.load()) {
        // Poll for accept readiness with a short timeout so we can notice
        // running_ flipping to false and exit cleanly.
        pollfd pfd{ listen_fd, POLLIN, 0 };
        int rc = ::poll(&pfd, 1, 100);
        if (rc < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "HTTP: poll() failed: %s\n", strerror(errno));
            break;
        }
        if (rc == 0 || !(pfd.revents & POLLIN)) continue;

        sockaddr_storage peer{};
        socklen_t peer_len = sizeof(peer);
        int conn_fd = ::accept4(listen_fd, (sockaddr*)&peer, &peer_len,
                                SOCK_CLOEXEC);
        if (conn_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            fprintf(stderr, "HTTP: accept() failed: %s\n", strerror(errno));
            continue;
        }

        // Handle the connection inline on the accept thread. Serial
        // dispatch is fine: the work per request is small (build the
        // response, write it to the kernel buffer, drain). For long-
        // running paths, handle_client's stream/websocket detection
        // already spawns its own worker thread + dup-fd hand-off.
        QTcpSocket sock;
        sock.setSocketDescriptor(conn_fd, QAbstractSocket::ConnectedState);
        bool handed_off = handle_client(&sock);
        if (!handed_off) {
            constexpr int kFlushBudgetMs = 5000;
            auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(kFlushBudgetMs);
            while (sock.bytesToWrite() > 0) {
                int rem = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now()).count();
                if (rem <= 0) break;
                if (!sock.waitForBytesWritten(rem)) break;
            }
        } else {
            // Handed off — the inner handler dup'd the fd. Release Qt's
            // ownership without closing the dup'd reference.
            sock.setSocketDescriptor(-1, QAbstractSocket::UnconnectedState,
                                     QIODevice::NotOpen);
        }
        // sock dtor closes conn_fd (or no-ops if released above).
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

// Send a short response (error pages, mostly) and flush. Caller is
// responsible for socket teardown.
static void send_simple_response(QTcpSocket* socket, int code,
                                 const char* status, const char* body) {
    Response resp;
    resp.set_status(code, status);
    resp.set_body(body);
    std::string s = resp.build();
    socket->write(s.data(), static_cast<qint64>(s.size()));
    socket->waitForBytesWritten(2000);
}

bool Server::try_websocket_upgrade(const Request& req, QTcpSocket* socket) {
    auto it = websocket_routes_.find(req.path);
    if (it == websocket_routes_.end()) return false;

    auto upg_it  = req.headers.find("upgrade");
    auto conn_it = req.headers.find("connection");
    auto key_it  = req.headers.find("sec-websocket-key");
    auto ver_it  = req.headers.find("sec-websocket-version");

    if (upg_it == req.headers.end() || !iequals(upg_it->second, "websocket")) return false;
    if (conn_it == req.headers.end() ||
        (conn_it->second.find("Upgrade") == std::string::npos &&
         conn_it->second.find("upgrade") == std::string::npos)) return false;
    if (key_it == req.headers.end()) return false;
    if (ver_it == req.headers.end() || ver_it->second != "13") return false;

    std::string accept = WebSocket::compute_accept(key_it->second);

    std::string resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n"
        "\r\n";
    socket->write(resp.data(), static_cast<qint64>(resp.size()));
    socket->waitForBytesWritten(2000);

    int qt_fd = socket->socketDescriptor();
    if (qt_fd < 0) return false;
    int worker_fd = ::dup(qt_fd);
    if (worker_fd < 0) {
        fprintf(stderr, "HTTP: dup() for websocket handoff failed: %s\n", strerror(errno));
        return false;
    }
    // Qt's QTcpSocket::setSocketDescriptor sets O_NONBLOCK on the fd, and
    // dup() inherits the file status flags. The websocket worker uses
    // blocking send/recv (its send_all bails on EAGAIN, treating it as
    // an error and closing the connection — 1006 abnormal close on the
    // browser side). Force blocking mode on the dup'd fd so a transient
    // full TCP send buffer (frame burst on resolution/codec change)
    // doesn't tear down the socket.
    {
        int flags = ::fcntl(worker_fd, F_GETFL, 0);
        if (flags >= 0) ::fcntl(worker_fd, F_SETFL, flags & ~O_NONBLOCK);
    }

    auto handler = it->second;
    Request req_copy = req;
    std::thread([handler, req_copy, worker_fd]() {
        auto ws = std::make_shared<WebSocket>(worker_fd);
        handler(ws, req_copy);
        ws->run_read_loop();  // blocks until peer close or protocol error
    }).detach();

    return true;
}

bool Server::handle_client(QTcpSocket* socket) {
    std::string request;
    request.reserve(8192);

    // Read until we have complete headers (or the client gives up).
    // 30s is generous for an idle browser holding a keep-alive open.
    constexpr int kReadTimeoutMs = 30000;
    constexpr size_t kMaxHeaderBytes = 64 * 1024;

    size_t header_end = std::string::npos;
    while (header_end == std::string::npos) {
        if (socket->bytesAvailable() == 0) {
            if (!socket->waitForReadyRead(kReadTimeoutMs)) return false;
        }
        QByteArray data = socket->readAll();
        if (data.isEmpty()) return false;
        request.append(data.constData(), data.size());
        header_end = request.find("\r\n\r\n");
        if (header_end == std::string::npos && request.size() > kMaxHeaderBytes) {
            send_simple_response(socket, 431, "Request Header Fields Too Large",
                                 "Request Header Fields Too Large");
            return false;
        }
    }

    // Parse Content-Length so we know how much body to wait for. Cap at
    // 16MB so a hostile client can't OOM us with a fake Content-Length and
    // a slow trickle of bytes.
    constexpr size_t kMaxBodyBytes = 16 * 1024 * 1024;
    size_t content_length = 0;
    size_t cl_pos = request.find("Content-Length: ");
    if (cl_pos == std::string::npos)
        cl_pos = request.find("content-length: ");
    if (cl_pos != std::string::npos) {
        try {
            size_t parsed = std::stoul(request.substr(cl_pos + 16));
            if (parsed > kMaxBodyBytes) {
                send_simple_response(socket, 413, "Payload Too Large", "Payload Too Large");
                return false;
            }
            content_length = parsed;
        } catch (const std::exception &) {
            send_simple_response(socket, 400, "Bad Request", "Invalid Content-Length");
            return false;
        }
    }

    size_t body_start = header_end + 4;
    size_t body_received = request.size() - body_start;
    while (body_received < content_length) {
        if (socket->bytesAvailable() == 0) {
            if (!socket->waitForReadyRead(kReadTimeoutMs)) break;
        }
        QByteArray data = socket->readAll();
        if (data.isEmpty()) break;
        request.append(data.constData(), data.size());
        body_received += data.size();
    }

    Request req;
    if (!parse_request(request.c_str(), request.size(), req)) {
        send_simple_response(socket, 400, "Bad Request", "Bad Request");
        return false;
    }

    // WebSocket upgrade takes over the fd entirely.
    if (req.method == "GET" && try_websocket_upgrade(req, socket)) {
        return true;
    }

    // Stream routes (GET only) — send the chunked-streaming preamble, then
    // hand a dup'd fd off to the stream handler thread.
    if (req.method == "GET") {
        auto it = stream_routes_.find(req.path);
        if (it != stream_routes_.end()) {
            std::string headers =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/event-stream\r\n"
                "Transfer-Encoding: chunked\r\n"
                "Cache-Control: no-cache, no-store\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "X-Accel-Buffering: no\r\n"
                "Connection: keep-alive\r\n"
                "\r\n";
            socket->write(headers.data(), static_cast<qint64>(headers.size()));
            socket->waitForBytesWritten(2000);

            int qt_fd = socket->socketDescriptor();
            if (qt_fd < 0) return false;
            int worker_fd = ::dup(qt_fd);
            if (worker_fd < 0) {
                fprintf(stderr, "HTTP: dup() for stream handoff failed: %s\n", strerror(errno));
                return false;
            }
            // Same O_NONBLOCK fix as the websocket handoff: dup inherits
            // Qt's non-blocking flag; stream workers expect blocking I/O.
            {
                int flags = ::fcntl(worker_fd, F_GETFL, 0);
                if (flags >= 0) ::fcntl(worker_fd, F_SETFL, flags & ~O_NONBLOCK);
            }

            auto handler = it->second;
            std::thread([handler, req, worker_fd]() {
                handler(req, worker_fd);
            }).detach();

            return true;
        }
    }

    // Normal request — synchronous response.
    Response resp = handler_(req);
    std::string response_str = resp.build();
    socket->write(response_str.data(), static_cast<qint64>(response_str.size()));
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

    if (body_start != std::string::npos) {
        req.body = request.substr(body_start + 4);
    }

    return true;
}

} // namespace http
