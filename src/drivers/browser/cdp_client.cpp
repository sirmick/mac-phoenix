/*
 *  cdp_client.cpp — see cdp_client.h.
 */
#include "cdp_client.h"

#include <rtc/websocket.hpp>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace browser {

namespace {

// Synchronous HTTP/1.1 GET with Connection: close. Used only for CDP
// discovery; chunked encoding isn't expected (DevTools uses
// Content-Length). Reads until the server closes the connection or we
// have header + Content-Length bytes of body.
std::string http_get(const char* host, uint16_t port, const char* path)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return {};

    // 1s recv timeout — if the server stalls we want to retry, not
    // hang the discovery polling loop forever.
    timeval tv{1, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (::inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        ::close(fd);
        return {};
    }
    if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(fd);
        return {};
    }

    char req[256];
    int  n = std::snprintf(req, sizeof(req),
                           "GET %s HTTP/1.1\r\n"
                           "Host: %s:%u\r\n"
                           "Connection: close\r\n"
                           "Accept: */*\r\n\r\n",
                           path, host, (unsigned)port);
    if (::send(fd, req, (size_t)n, 0) != n) {
        ::close(fd);
        return {};
    }

    std::string buf;
    char        chunk[4096];
    size_t      header_end = std::string::npos;
    long        content_length = -1;
    for (;;) {
        ssize_t r = ::recv(fd, chunk, sizeof(chunk), 0);
        if (r <= 0) break;
        buf.append(chunk, (size_t)r);
        if (buf.size() > 1 << 20) break;

        // Once we've parsed headers, stop after Content-Length bytes
        // of body — DevTools keeps the connection open between
        // requests despite Connection: close, so waiting for EOF
        // hangs until our recv timeout.
        if (header_end == std::string::npos) {
            auto p = buf.find("\r\n\r\n");
            if (p != std::string::npos) {
                header_end = p + 4;
                // Case-insensitive search for Content-Length.
                std::string head_lower(buf, 0, p);
                for (auto& c : head_lower) c = (char)std::tolower((unsigned char)c);
                auto cl = head_lower.find("content-length:");
                if (cl != std::string::npos) {
                    cl += 15;
                    while (cl < head_lower.size() && head_lower[cl] == ' ') cl++;
                    content_length = std::strtol(buf.c_str() + cl, nullptr, 10);
                }
            }
        }
        if (header_end != std::string::npos && content_length >= 0 &&
            buf.size() >= header_end + (size_t)content_length) {
            break;
        }
    }
    ::close(fd);

    if (header_end == std::string::npos) return {};
    if (content_length >= 0)
        return buf.substr(header_end, (size_t)content_length);
    return buf.substr(header_end);
}

// CDP target list shape (Chromium /json/list):
//   [{ "id":"...", "type":"page", "url":"...",
//      "webSocketDebuggerUrl":"ws://127.0.0.1:PORT/devtools/page/..." }]
// Pick the first page-type target.
std::string find_page_ws_url(uint16_t port)
{
    std::string body = http_get("127.0.0.1", port, "/json/list");
    if (body.empty()) return {};
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(body));
    if (!doc.isArray()) return {};
    for (const QJsonValue& v : doc.array()) {
        QJsonObject obj = v.toObject();
        if (obj.value("type").toString() == "page") {
            return obj.value("webSocketDebuggerUrl").toString().toStdString();
        }
    }
    return {};
}

}  // namespace

CdpClient::CdpClient(uint16_t debug_port)
    : debug_port_(debug_port)
{
}

CdpClient::~CdpClient()
{
    stopping_.store(true, std::memory_order_release);
    if (ws_) {
        try { ws_->close(); } catch (...) {}
    }
    if (worker_.joinable()) worker_.join();
}

void CdpClient::start()
{
    if (worker_.joinable()) return;
    worker_ = std::thread([this]() { worker_main(); });
}

void CdpClient::worker_main()
{
    // Wait for Chromium's DevTools server to come up. QtWebEngine
    // brings it up shortly after QApplication::exec() begins —
    // typically <500ms on localhost. Cap the wait at 2s; if not up
    // by then, something is wrong (env var unset before QApplication,
    // port already in use, Chromium failed to spawn) and we want the
    // operator to see it loudly. CDP is the only control plane for
    // input + navigation, so failure is fatal: abort the IPC
    // subprocess. The parent reports "Failed to connect to child".
    std::string ws_url;
    for (int attempt = 0; attempt < 20; attempt++) {  // 20 × 100ms = 2s
        if (stopping_.load(std::memory_order_acquire)) return;
        ws_url = find_page_ws_url(debug_port_);
        if (!ws_url.empty()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (ws_url.empty()) {
        fprintf(stderr,
                "[CDP] FATAL: devtools discovery failed after 2s on port %u.\n"
                "[CDP] Check QTWEBENGINE_REMOTE_DEBUGGING is set before the "
                "first QApplication ctor.\n",
                (unsigned)debug_port_);
        std::abort();
    }
    fprintf(stderr, "[CDP] connecting to %s\n", ws_url.c_str());

    auto ws = std::make_shared<rtc::WebSocket>();
    ws->onOpen([this]() {
        open_.store(true, std::memory_order_release);
        fprintf(stderr, "[CDP] connected\n");
        // Hide Chromium native scrollbars — the Mac chrome renders
        // its own. setScrollbarsHidden persists across navigations,
        // so a single call at connect time covers every page.
        set_scrollbars_hidden(true);
    });
    ws->onClosed([this]() {
        open_.store(false, std::memory_order_release);
        fprintf(stderr, "[CDP] closed\n");
    });
    ws->onError([](std::string err) {
        fprintf(stderr, "[CDP] error: %s\n", err.c_str());
    });
    ws->onMessage([this](rtc::message_variant msg) {
        if (auto* s = std::get_if<std::string>(&msg)) {
            QJsonDocument d = QJsonDocument::fromJson(
                QByteArray::fromStdString(*s));
            on_message_locked(d.object());
        }
    });
    ws_ = ws;
    try { ws->open(ws_url); }
    catch (const std::exception& e) {
        fprintf(stderr, "[CDP] FATAL: WebSocket::open(%s) threw: %s\n",
                ws_url.c_str(), e.what());
        std::abort();
    }

    // Localhost WS handshake should complete in <50ms. Cap at 1s.
    for (int i = 0; i < 20; i++) {
        if (stopping_.load(std::memory_order_acquire)) return;
        if (open_.load(std::memory_order_acquire)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    fprintf(stderr,
            "[CDP] FATAL: WebSocket open() did not signal onOpen within 1s.\n");
    std::abort();
}

void CdpClient::send(const std::string& method, const QJsonObject& params)
{
    if (!is_connected() || !ws_) return;
    QJsonObject msg;
    msg["id"]     = next_id_.fetch_add(1, std::memory_order_relaxed);
    msg["method"] = QString::fromStdString(method);
    msg["params"] = params;
    QByteArray bytes = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    try { ws_->send(bytes.toStdString()); }
    catch (...) { /* socket already gone — drop */ }
}

void CdpClient::send_with_reply(const std::string& method,
                                const QJsonObject& params,
                                std::function<void(const QJsonObject&)> cb)
{
    if (!is_connected() || !ws_) return;
    int id = next_id_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(pending_mutex_);
        pending_.emplace(id, std::move(cb));
    }
    QJsonObject msg;
    msg["id"]     = id;
    msg["method"] = QString::fromStdString(method);
    msg["params"] = params;
    QByteArray bytes = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    try { ws_->send(bytes.toStdString()); }
    catch (...) {
        std::lock_guard<std::mutex> lk(pending_mutex_);
        pending_.erase(id);
    }
}

void CdpClient::on_message_locked(const QJsonObject& msg)
{
    // Reply: has "id" + "result" or "error".
    if (msg.contains("id")) {
        int id = msg.value("id").toInt();
        std::function<void(const QJsonObject&)> cb;
        {
            std::lock_guard<std::mutex> lk(pending_mutex_);
            auto it = pending_.find(id);
            if (it != pending_.end()) {
                cb = std::move(it->second);
                pending_.erase(it);
            }
        }
        if (msg.contains("error")) {
            fprintf(stderr, "[CDP] reply id=%d error: %s\n", id,
                    QJsonDocument(msg.value("error").toObject())
                        .toJson(QJsonDocument::Compact).constData());
            return;
        }
        if (cb) cb(msg.value("result").toObject());
    }
    // Event (no "id"): currently unused; subscribe later if we move
    // status/title/history off Qt signals.
}

void CdpClient::key_event(const std::string& type,
                          int windows_vk,
                          const std::string& key,
                          const std::string& code,
                          const std::string& text,
                          int modifiers)
{
    QJsonObject p;
    p["type"]      = QString::fromStdString(type);
    p["modifiers"] = modifiers;
    if (windows_vk) {
        p["windowsVirtualKeyCode"] = windows_vk;
        p["nativeVirtualKeyCode"]  = windows_vk;
    }
    if (!key.empty())  p["key"]                = QString::fromStdString(key);
    if (!code.empty()) p["code"]               = QString::fromStdString(code);
    if (!text.empty()) {
        p["text"]                = QString::fromStdString(text);
        p["unmodifiedText"]      = QString::fromStdString(text);
    }
    send("Input.dispatchKeyEvent", p);
}

void CdpClient::type_text(const std::string& text)
{
    // Per the CDP spec, type="char" inserts text directly without
    // generating an extra keydown — Chromium dispatches an `input`
    // event with the text, which is what input/textarea/contenteditable
    // listeners react to. This is the right path for printable input.
    QJsonObject p;
    p["type"] = QStringLiteral("char");
    p["text"] = QString::fromStdString(text);
    p["unmodifiedText"] = QString::fromStdString(text);
    send("Input.dispatchKeyEvent", p);
}

void CdpClient::mouse_event(const std::string& type,
                            int x, int y,
                            const std::string& button,
                            int click_count,
                            int modifiers)
{
    QJsonObject p;
    p["type"]       = QString::fromStdString(type);
    p["x"]          = x;
    p["y"]          = y;
    p["button"]     = QString::fromStdString(button);
    p["clickCount"] = click_count;
    p["modifiers"]  = modifiers;
    send("Input.dispatchMouseEvent", p);
}

void CdpClient::mouse_wheel(int x, int y, int delta_x, int delta_y)
{
    QJsonObject p;
    p["type"]    = QStringLiteral("mouseWheel");
    p["x"]       = x;
    p["y"]       = y;
    p["deltaX"]  = delta_x;
    p["deltaY"]  = delta_y;
    p["button"]  = QStringLiteral("none");
    p["modifiers"] = 0;
    send("Input.dispatchMouseEvent", p);
}

void CdpClient::scroll_by(int dx, int dy)
{
    char expr[128];
    std::snprintf(expr, sizeof(expr), "window.scrollBy(%d,%d)", dx, dy);
    QJsonObject p;
    p["expression"]    = QString::fromLatin1(expr);
    p["returnByValue"] = true;
    send("Runtime.evaluate", p);
}

void CdpClient::set_scrollbars_hidden(bool hidden)
{
    QJsonObject p;
    p["hidden"] = hidden;
    send("Emulation.setScrollbarsHidden", p);
}

// ── Navigation ─────────────────────────────────────────────────

void CdpClient::navigate(const std::string& url)
{
    QJsonObject p;
    p["url"] = QString::fromStdString(url);
    send("Page.navigate", p);
}

void CdpClient::reload()
{
    QJsonObject p;
    p["ignoreCache"] = false;
    send("Page.reload", p);
}

void CdpClient::stop()
{
    send("Page.stopLoading", QJsonObject{});
}

void CdpClient::back()
{
    // CDP requires fetching the history first, then jumping by entryId.
    // Pipeline both as one logical op — the response handler picks the
    // entry one before currentIndex (if any) and fires the nav.
    send_with_reply("Page.getNavigationHistory", QJsonObject{},
                    [this](const QJsonObject& result) {
        int idx = result.value("currentIndex").toInt(-1);
        QJsonArray entries = result.value("entries").toArray();
        if (idx <= 0 || idx >= entries.size()) return;
        int entry_id = entries.at(idx - 1).toObject().value("id").toInt(-1);
        if (entry_id < 0) return;
        QJsonObject p;
        p["entryId"] = entry_id;
        send("Page.navigateToHistoryEntry", p);
    });
}

void CdpClient::forward()
{
    send_with_reply("Page.getNavigationHistory", QJsonObject{},
                    [this](const QJsonObject& result) {
        int idx = result.value("currentIndex").toInt(-1);
        QJsonArray entries = result.value("entries").toArray();
        if (idx < 0 || idx + 1 >= entries.size()) return;
        int entry_id = entries.at(idx + 1).toObject().value("id").toInt(-1);
        if (entry_id < 0) return;
        QJsonObject p;
        p["entryId"] = entry_id;
        send("Page.navigateToHistoryEntry", p);
    });
}

// ── Selection / clipboard ─────────────────────────────────────

void CdpClient::set_selection_cb(std::function<void(std::string)> cb)
{
    selection_cb_ = std::move(cb);
}

void CdpClient::get_selection()
{
    QJsonObject p;
    p["expression"] = QStringLiteral(
        "(window.getSelection&&window.getSelection().toString())||''");
    p["returnByValue"] = true;
    send_with_reply("Runtime.evaluate", p,
                    [this](const QJsonObject& result) {
        QJsonObject r = result.value("result").toObject();
        std::string text = r.value("value").toString().toStdString();
        if (selection_cb_) selection_cb_(std::move(text));
    });
}

}  // namespace browser
