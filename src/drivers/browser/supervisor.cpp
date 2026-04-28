/*
 *  supervisor.cpp — headless Chromium orchestration. See supervisor.h.
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

constexpr int kFirstDisplay      = 99;
constexpr int kLastDisplay       = 119;
constexpr int kFirstCdpPort      = 9222;
constexpr int kLastCdpPort       = 9322;
constexpr int kXReadyTimeoutMs   = 5000;
constexpr int kCdpReadyTimeoutMs = 10000;
constexpr int kViewportWidth     = 1024;
constexpr int kViewportHeight    = 768;
constexpr int kViewportDepth     = 24;

bool x_socket_present(int display)
{
    char path[64];
    snprintf(path, sizeof(path), "/tmp/.X11-unix/X%d", display);
    struct stat st;
    return ::stat(path, &st) == 0;
}

const char* find_xvfb_binary()
{
    if (::access("/usr/bin/Xvfb", X_OK) == 0) return "/usr/bin/Xvfb";
    if (::access("/usr/local/bin/Xvfb", X_OK) == 0) return "/usr/local/bin/Xvfb";
    return nullptr;
}

bool tcp_port_listening(int port)
{
    int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(0x7F000001);
    bool ok = ::connect(s, (sockaddr*)&addr, sizeof(addr)) == 0;
    ::close(s);
    return ok;
}

/* GET path on 127.0.0.1:port. Returns full HTTP response (headers +
 * body) or empty on error. Hand-rolled because curl is overkill for
 * one request to a local port. Sends HTTP/1.1 with Host:port — both
 * are required: chromium's CDP HTTP server only speaks 1.1, and it
 * echoes Host into webSocketDebuggerUrl, so missing :port produces a
 * URL without the port and the WS connect fails. */
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
        if (out.size() > 64 * 1024) break;
    }
    ::close(s);
    return out;
}

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
    /* Playwright's bundled "Chrome for Testing" is preferred — pinned
     * by the e2e test suite, version-locked, free of distro packaging
     * weirdness. Fall back to deb chrome / chromium if absent. Snap
     * chromium is deliberately not supported (sandbox + singleton-lock
     * foot-guns aren't worth the maintenance). */
    static thread_local std::string resolved;
    if (!resolved.empty()) return resolved.c_str();

    const char* home = getenv("HOME");
    if (home) {
        std::string dir = std::string(home) + "/.cache/ms-playwright";
        DIR* d = opendir(dir.c_str());
        if (d) {
            int best_rev = -1;
            std::string best;
            struct dirent* ent;
            while ((ent = readdir(d)) != nullptr) {
                if (strncmp(ent->d_name, "chromium-", 9) != 0) continue;
                int rev = atoi(ent->d_name + 9);
                if (rev <= best_rev) continue;
                std::string p = dir + "/" + ent->d_name +
                                "/chrome-linux64/chrome";
                if (::access(p.c_str(), X_OK) == 0) {
                    best_rev = rev;
                    best = p;
                }
            }
            closedir(d);
            if (!best.empty()) {
                resolved = best;
                return resolved.c_str();
            }
        }
    }

    static const char* candidates[] = {
        "/usr/bin/google-chrome",
        "/opt/google/chrome/google-chrome",
        "/usr/bin/chromium",
        nullptr
    };
    for (auto p = candidates; *p; p++) {
        if (::access(*p, X_OK) == 0) {
            resolved = *p;
            return resolved.c_str();
        }
    }
    return nullptr;
}

void close_inherited_fds()
{
    /* mac-phoenix is multi-threaded with HTTP/WebRTC/IPC sockets and
     * eventfds open. Without closing them in the fork child, chromium
     * inherits them — and worse, keeps the parent's TCP listeners
     * alive after mac-phoenix exits (port 19500 stuck for the next
     * run). closefrom(3) is glibc-2.34+. */
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
        for (int fd = 3; fd < 1024; fd++) ::close(fd);
    }
#endif
}

}  // namespace

Supervisor::Supervisor() {
    const char* home = getenv("HOME");
    if (!home) home = "/tmp";
    user_data_dir_ = std::string(home) + "/.cache/mac-phoenix/chromium-profile";
}

Supervisor::~Supervisor() { stop(); }

int Supervisor::pick_free_port()
{
    for (int p = kFirstCdpPort; p <= kLastCdpPort; p++) {
        if (!tcp_port_listening(p)) return p;
    }
    return 0;
}

int Supervisor::pick_free_display()
{
    for (int d = kFirstDisplay; d <= kLastDisplay; d++) {
        if (!x_socket_present(d)) return d;
    }
    return -1;
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
             "%dx%dx%d", kViewportWidth, kViewportHeight, kViewportDepth);

    pid_t pid = fork();
    if (pid < 0) { perror("[BrowserSup] fork(Xvfb)"); return false; }
    if (pid == 0) {
        int devnull = ::open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            ::dup2(devnull, 0);
            ::dup2(devnull, 1);
            ::dup2(devnull, 2);
            ::close(devnull);
        }
        close_inherited_fds();
        setpgid(0, 0);
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
        int status = 0;
        if (waitpid(xvfb_pid_, &status, WNOHANG) == xvfb_pid_) {
            fprintf(stderr, "[BrowserSup] Xvfb exited before socket "
                    "appeared (status=0x%x)\n", status);
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

bool Supervisor::spawn_chromium(const std::string& url)
{
    const char* chrome = find_chromium_binary();
    if (!chrome) {
        fprintf(stderr,
                "[BrowserSup] Chromium not found. Install via "
                "`npx playwright install chromium` (preferred) "
                "or `apt install google-chrome-stable`.\n");
        return false;
    }

    {  /* mkdir -p user_data_dir_ */
        std::string p = user_data_dir_;
        size_t slash = p.find('/', 1);
        while (slash != std::string::npos) {
            mkdir(p.substr(0, slash).c_str(), 0755);
            slash = p.find('/', slash + 1);
        }
        mkdir(p.c_str(), 0755);
    }

    /* Stale SingletonLock from a previous chromium that crashed or
     * was SIGKILLed will make this launch silently exit before
     * binding CDP. We own this profile, delete unconditionally. */
    ::unlink((user_data_dir_ + "/SingletonLock").c_str());
    ::unlink((user_data_dir_ + "/SingletonSocket").c_str());
    ::unlink((user_data_dir_ + "/SingletonCookie").c_str());

    char port_arg[64];
    snprintf(port_arg, sizeof(port_arg),
             "--remote-debugging-port=%d", cdp_port_);
    char window_size_arg[32];
    snprintf(window_size_arg, sizeof(window_size_arg),
             "--window-size=%d,%d", kViewportWidth, kViewportHeight);
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
        setpgid(0, 0);

        char display_env[16];
        snprintf(display_env, sizeof(display_env), ":%d", display_);
        setenv("DISPLAY", display_env, 1);

        /* --kiosk = chromeless full-display rendering. CDP
         * Page.startScreencast captures from chromium's compositor
         * directly, so Xvfb just needs to be there for chromium to
         * agree to run headed; the X drawable contents are unused.
         * --no-sandbox: Ubuntu 23.10+ AppArmor user-namespace block. */
        const char* startup_url = url.empty() ? "about:blank" : url.c_str();
        execl(chrome, chrome,
              port_arg,
              "--kiosk",
              window_size_arg,
              "--window-position=0,0",
              "--hide-scrollbars",
              "--no-first-run",
              "--no-default-browser-check",
              "--disable-dev-shm-usage",
              "--no-sandbox",
              user_data_arg,
              startup_url,
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

bool Supervisor::start(const std::string& initial_url)
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
    if (!spawn_chromium(initial_url)) { stop(); return false; }
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
    auto kill_group = [](int& pid) {
        if (pid <= 0) return;
        ::kill(-pid, SIGTERM);
        for (int i = 0; i < 10; i++) {
            int status = 0;
            if (waitpid(pid, &status, WNOHANG) == pid) {
                pid = 0;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ::kill(-pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        pid = 0;
    };
    /* Chromium first so it doesn't notice X going away mid-cleanup. */
    kill_group(chromium_pid_);
    kill_group(xvfb_pid_);
    browser_ws_url_.clear();
    display_ = -1;
}

}  // namespace browser
