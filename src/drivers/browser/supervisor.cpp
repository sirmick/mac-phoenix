/*
 *  supervisor.cpp — Xvfb + Chromium child process orchestration.
 *  See supervisor.h for the contract.
 */
#include "supervisor.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <nlohmann/json.hpp>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace browser {

namespace {

constexpr int    kFirstDisplay     = 99;
constexpr int    kLastDisplay      = 119;
constexpr int    kFirstCdpPort     = 9222;
constexpr int    kLastCdpPort      = 9322;
constexpr int    kXvfbWidth        = 1024;
constexpr int    kXvfbHeight       = 768;
constexpr int    kXvfbDepth        = 24;
constexpr int    kXReadyTimeoutMs  = 5000;
constexpr int    kCdpReadyTimeoutMs = 8000;

bool x_socket_present(int display)
{
    char path[64];
    snprintf(path, sizeof(path), "/tmp/.X11-unix/X%d", display);
    struct stat st;
    return ::stat(path, &st) == 0;
}

bool tcp_port_listening(int port)
{
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(0x7F000001);  /* 127.0.0.1 */
    bool ok = ::connect(s, (sockaddr*)&addr, sizeof(addr)) == 0;
    ::close(s);
    return ok;
}

/* HTTP GET /json/version on 127.0.0.1:port; returns the body or empty
 * string. Hand-rolled because adding libcurl just for this is silly. */
std::string http_get_localhost(int port, const char* path, int timeout_ms)
{
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return {};
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(0x7F000001);
    timeval tv{ timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (::connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
        ::close(s);
        return {};
    }
    /* Chromium's CDP HTTP server only speaks HTTP/1.1 — sending 1.0
     * gets us a stream of "Cannot handle request with protocol: HTTP/1.0"
     * errors. Add Connection: close so the server returns a single
     * complete response and EOFs.
     *
     * The Host header MUST include the port — Chromium echoes it back
     * verbatim into the webSocketDebuggerUrl it returns. Without the
     * port, the WS URL is truncated to ws://127.0.0.1/... and the
     * client can't connect. */
    char req[256];
    int n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n"
                     "Connection: close\r\n\r\n", path, port);
    if (::send(s, req, n, 0) != n) { ::close(s); return {}; }

    std::string out;
    char buf[4096];
    while (true) {
        ssize_t r = ::recv(s, buf, sizeof(buf), 0);
        if (r <= 0) break;
        out.append(buf, (size_t)r);
        if (out.size() > 64 * 1024) break;  /* sanity */
    }
    ::close(s);
    return out;
}

/* Parse CDP /json/version response body for the webSocketDebuggerUrl.
 * Body is HTTP/1.1: <status>\r\n<headers>\r\n\r\n<json>. */
std::string extract_ws_url(const std::string& body)
{
    size_t p = body.find("\r\n\r\n");
    if (p == std::string::npos) return {};
    p += 4;
    try {
        auto j = nlohmann::json::parse(body.begin() + p, body.end());
        if (j.contains("webSocketDebuggerUrl") &&
            j["webSocketDebuggerUrl"].is_string()) {
            return j["webSocketDebuggerUrl"].get<std::string>();
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "[BrowserSup] /json/version JSON parse failed: %s\n",
                e.what());
    }
    return {};
}

const char* find_chromium_binary()
{
    /* Snap path takes priority — that's what's installed on dev boxes;
     * /usr/bin/chromium-browser is just a snap launcher stub anyway. */
    static const char* candidates[] = {
        "/snap/bin/chromium",
        "/usr/bin/chromium",
        "/usr/bin/chromium-browser",
        "/usr/bin/google-chrome",
        "/opt/google/chrome/google-chrome",
        nullptr
    };
    for (auto p = candidates; *p; p++) {
        if (::access(*p, X_OK) == 0) return *p;
    }
    return nullptr;
}

const char* find_xvfb_binary()
{
    if (::access("/usr/bin/Xvfb", X_OK) == 0) return "/usr/bin/Xvfb";
    if (::access("/usr/local/bin/Xvfb", X_OK) == 0) return "/usr/local/bin/Xvfb";
    return nullptr;
}

/* Close every fd ≥ 3 the child doesn't need. mac-phoenix is multi-
 * threaded; the parent has the HTTP listen socket, the WebRTC sockets,
 * the bridge IPC pipes, and a bunch of internal eventfds open. Without
 * this, Xvfb/Chromium inherit them — and worse, they keep the parent's
 * TCP listeners alive after mac-phoenix exits, blocking the next run
 * from rebinding port 19500. closefrom(3) is the BSD/glibc-2.34+ way;
 * fall back to a /proc/self/fd walk on older systems. */
void close_inherited_fds()
{
#if defined(__GLIBC__) && \
    ((__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
    closefrom(3);
#else
    DIR* d = opendir("/proc/self/fd");
    if (d) {
        struct dirent* ent;
        while ((ent = readdir(d)) != nullptr) {
            int fd = atoi(ent->d_name);
            if (fd >= 3 && fd != dirfd(d)) ::close(fd);
        }
        closedir(d);
    } else {
        /* Last-resort: fixed range. */
        for (int fd = 3; fd < 1024; fd++) ::close(fd);
    }
#endif
}

}  // namespace

Supervisor::Supervisor() {
    /* Profile location is constrained when chromium ships as a snap:
     * the snap sandbox forbids writes outside ~/snap/chromium/, so a
     * profile in ~/.cache/ would fail at SingletonLock creation. Pick
     * the snap-friendly location if it exists, fall back to ~/.cache
     * for non-snap chromium installs.
     *
     * Profile is persistent (not /tmp) so logins survive restarts. */
    const char* home = getenv("HOME");
    if (!home) home = "/tmp";

    std::string snap_dir = std::string(home) + "/snap/chromium/common";
    struct stat st;
    if (::stat(snap_dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        user_data_dir_ = snap_dir + "/mac-phoenix-profile";
    } else {
        user_data_dir_ = std::string(home) +
                         "/.cache/mac-phoenix/chromium-profile";
    }
}

Supervisor::~Supervisor() { stop(); }

int Supervisor::pick_free_display()
{
    for (int d = kFirstDisplay; d <= kLastDisplay; d++) {
        if (!x_socket_present(d)) return d;
    }
    return -1;
}

int Supervisor::pick_free_port()
{
    for (int p = kFirstCdpPort; p <= kLastCdpPort; p++) {
        if (!tcp_port_listening(p)) return p;
    }
    return 0;
}

bool Supervisor::spawn_xvfb()
{
    const char* xvfb = find_xvfb_binary();
    if (!xvfb) {
        fprintf(stderr, "[BrowserSup] Xvfb not found — `apt install xvfb`\n");
        return false;
    }

    char display_arg[16];
    snprintf(display_arg, sizeof(display_arg), ":%d", display_);
    char screen_arg[64];
    snprintf(screen_arg, sizeof(screen_arg),
             "%dx%dx%d", kXvfbWidth, kXvfbHeight, kXvfbDepth);

    pid_t pid = fork();
    if (pid < 0) { perror("[BrowserSup] fork(Xvfb)"); return false; }
    if (pid == 0) {
        /* Child: detach stdio, drop inherited fds, exec Xvfb. */
        int devnull = ::open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            ::dup2(devnull, 0);
            ::dup2(devnull, 1);
            ::dup2(devnull, 2);
            ::close(devnull);
        }
        close_inherited_fds();
        execl(xvfb, "Xvfb",
              display_arg,
              "-screen", "0", screen_arg,
              "-nolisten", "tcp",
              "-noreset",
              (char*)nullptr);
        _exit(127);
    }
    xvfb_pid_ = pid;
    fprintf(stderr, "[BrowserSup] Xvfb pid=%d on :%d (%s)\n",
            xvfb_pid_, display_, screen_arg);
    return true;
}

bool Supervisor::wait_for_x_socket(int timeout_ms)
{
    auto start = std::chrono::steady_clock::now();
    while (true) {
        if (x_socket_present(display_)) return true;
        /* Did the child die? */
        int status = 0;
        if (waitpid(xvfb_pid_, &status, WNOHANG) == xvfb_pid_) {
            fprintf(stderr, "[BrowserSup] Xvfb exited before socket appeared "
                    "(status=0x%x)\n", status);
            xvfb_pid_ = 0;
            return false;
        }
        if (std::chrono::steady_clock::now() - start >
            std::chrono::milliseconds(timeout_ms)) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

bool Supervisor::spawn_chromium()
{
    const char* chrome = find_chromium_binary();
    if (!chrome) {
        fprintf(stderr,
                "[BrowserSup] Chromium not found — install via "
                "`sudo apt install chromium` (snap version is fine)\n");
        return false;
    }

    /* Make sure user_data_dir exists. */
    {
        std::string p = user_data_dir_;
        size_t slash = p.find('/', 1);
        while (slash != std::string::npos) {
            mkdir(p.substr(0, slash).c_str(), 0755);
            slash = p.find('/', slash + 1);
        }
        mkdir(p.c_str(), 0755);
    }

    char display_env[32];
    snprintf(display_env, sizeof(display_env), "DISPLAY=:%d", display_);
    char port_arg[64];
    snprintf(port_arg, sizeof(port_arg),
             "--remote-debugging-port=%d", cdp_port_);
    char user_data_arg[1024];
    snprintf(user_data_arg, sizeof(user_data_arg),
             "--user-data-dir=%s", user_data_dir_.c_str());

    pid_t pid = fork();
    if (pid < 0) { perror("[BrowserSup] fork(chromium)"); return false; }
    if (pid == 0) {
        int devnull = ::open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            ::dup2(devnull, 0);
            ::dup2(devnull, 1);
            ::dup2(devnull, 2);
            ::close(devnull);
        }
        close_inherited_fds();
        /* Snap chromium reads DISPLAY from env, not argv. */
        setenv("DISPLAY", display_env + 8, 1);
        char window_size_arg[32];
        snprintf(window_size_arg, sizeof(window_size_arg),
                 "--window-size=%d,%d", kXvfbWidth, kXvfbHeight);
        /* --kiosk gives us chromeless full-Xvfb-display rendering —
         * single window, no toolbar, tabs, or address bar. We tried
         * --app=<url> first but snap Chromium treats it as
         * "open URL in existing instance," exits 21 without ever
         * binding the CDP port. --kiosk plays cleanly with the snap
         * sandbox and yields the same edge-to-edge content area. */
        execl(chrome, chrome,
              port_arg,
              "--kiosk",
              window_size_arg,
              "--window-position=0,0",
              "--no-first-run",
              "--no-default-browser-check",
              "--disable-dev-shm-usage",
              "--disable-gpu",
              "--no-sandbox",                  /* snap chromium under Xvfb */
              user_data_arg,
              "about:blank",
              (char*)nullptr);
        _exit(127);
    }
    chromium_pid_ = pid;
    fprintf(stderr, "[BrowserSup] Chromium pid=%d cdp=%d profile=%s\n",
            chromium_pid_, cdp_port_, user_data_dir_.c_str());
    return true;
}

bool Supervisor::wait_for_cdp(int timeout_ms)
{
    auto start = std::chrono::steady_clock::now();
    while (true) {
        if (tcp_port_listening(cdp_port_)) {
            std::string body = http_get_localhost(cdp_port_, "/json/version", 1000);
            if (!body.empty()) return true;
        }
        int status = 0;
        if (waitpid(chromium_pid_, &status, WNOHANG) == chromium_pid_) {
            fprintf(stderr, "[BrowserSup] Chromium exited before CDP came up "
                    "(status=0x%x)\n", status);
            chromium_pid_ = 0;
            return false;
        }
        if (std::chrono::steady_clock::now() - start >
            std::chrono::milliseconds(timeout_ms)) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool Supervisor::fetch_browser_ws_url()
{
    std::string body = http_get_localhost(cdp_port_, "/json/version", 2000);
    if (body.empty()) {
        fprintf(stderr, "[BrowserSup] /json/version request failed\n");
        return false;
    }
    browser_ws_url_ = extract_ws_url(body);
    if (browser_ws_url_.empty()) {
        fprintf(stderr, "[BrowserSup] could not parse webSocketDebuggerUrl\n");
        return false;
    }
    fprintf(stderr, "[BrowserSup] CDP browser WS: %s\n",
            browser_ws_url_.c_str());
    return true;
}

bool Supervisor::start()
{
    display_ = pick_free_display();
    if (display_ < 0) {
        fprintf(stderr, "[BrowserSup] no free X display in :%d..:%d\n",
                kFirstDisplay, kLastDisplay);
        return false;
    }
    cdp_port_ = pick_free_port();
    if (cdp_port_ == 0) {
        fprintf(stderr, "[BrowserSup] no free CDP port in %d..%d\n",
                kFirstCdpPort, kLastCdpPort);
        return false;
    }

    if (!spawn_xvfb()) return false;
    if (!wait_for_x_socket(kXReadyTimeoutMs)) {
        fprintf(stderr, "[BrowserSup] Xvfb didn't expose :%d socket within %dms\n",
                display_, kXReadyTimeoutMs);
        stop();
        return false;
    }
    if (!spawn_chromium()) { stop(); return false; }
    if (!wait_for_cdp(kCdpReadyTimeoutMs)) {
        fprintf(stderr, "[BrowserSup] CDP didn't come up on port %d within %dms\n",
                cdp_port_, kCdpReadyTimeoutMs);
        stop();
        return false;
    }
    if (!fetch_browser_ws_url()) { stop(); return false; }
    return true;
}

void Supervisor::stop()
{
    if (chromium_pid_ > 0) {
        ::kill(chromium_pid_, SIGTERM);
        /* Give it 1s to flush profile, then kill -9 if still around. */
        for (int i = 0; i < 10; i++) {
            int status = 0;
            if (waitpid(chromium_pid_, &status, WNOHANG) == chromium_pid_) {
                chromium_pid_ = 0;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (chromium_pid_ > 0) {
            ::kill(chromium_pid_, SIGKILL);
            waitpid(chromium_pid_, nullptr, 0);
            chromium_pid_ = 0;
        }
    }
    if (xvfb_pid_ > 0) {
        ::kill(xvfb_pid_, SIGTERM);
        for (int i = 0; i < 10; i++) {
            int status = 0;
            if (waitpid(xvfb_pid_, &status, WNOHANG) == xvfb_pid_) {
                xvfb_pid_ = 0;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (xvfb_pid_ > 0) {
            ::kill(xvfb_pid_, SIGKILL);
            waitpid(xvfb_pid_, nullptr, 0);
            xvfb_pid_ = 0;
        }
    }
    browser_ws_url_.clear();
}

}  // namespace browser
