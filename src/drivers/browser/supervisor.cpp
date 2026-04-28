/*
 *  supervisor.cpp — Xvfb + Firefox orchestration. See supervisor.h.
 */
#include "supervisor.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace browser {

namespace {

constexpr int kFirstDisplay     = 99;
constexpr int kLastDisplay      = 119;
constexpr int kXReadyTimeoutMs  = 5000;
/* Match the guest MacBrowser window's viewport area. CopyBits in
 * the guest is then a 1:1 copy — no scaling, no clipping. The Mac
 * window total is 462 px tall (24 chrome + 422 viewport + 16 status). */
constexpr int kViewportWidth    = 640;
constexpr int kViewportHeight   = 422;
constexpr int kViewportDepth    = 24;
/* WebDriver BiDi listens here (M4.5). Firefox accepts both BiDi and
 * the legacy CDP on the same port; we use BiDi only. */
constexpr int kBidiPort         = 9222;

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

const char* find_firefox_binary()
{
    /* Prefer the deb/tarball-style install at /opt/firefox over the
     * Ubuntu snap stub at /usr/bin/firefox. The snap version brings
     * sandbox/profile/auto-update behaviors that don't compose with
     * Xvfb cleanly; the unbundled binary is what M4 was tested on. */
    static const char* candidates[] = {
        "/opt/firefox/firefox",
        "/opt/firefox-esr/firefox",
        "/usr/local/bin/firefox-deb",
        nullptr,
    };
    for (auto p = candidates; *p; p++) {
        if (::access(*p, X_OK) == 0) return *p;
    }
    /* Last resort: /usr/bin/firefox. Will work if it's a real deb but
     * may misbehave if it's the snap stub. */
    if (::access("/usr/bin/firefox", X_OK) == 0) return "/usr/bin/firefox";
    return nullptr;
}

/* Reset all signal handlers + signal mask in the fork child to defaults.
 * mac-phoenix's CrashHandler installs handlers for SIGSEGV/BUS/ABRT/
 * ILL/FPE; Firefox / Chromium try to install their own and trip over
 * pre-existing ones. After exec the child's binary will install
 * whatever it wants. */
void reset_signals_for_exec()
{
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, nullptr);
    struct sigaction sa = {};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    for (int sig = 1; sig < NSIG; sig++) {
        if (sig == SIGKILL || sig == SIGSTOP) continue;
        sigaction(sig, &sa, nullptr);
    }
}

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
        for (int fd = 3; fd < 1024; fd++) ::close(fd);
    }
#endif
}

void mkdir_p(const std::string& p)
{
    size_t slash = p.find('/', 1);
    while (slash != std::string::npos) {
        mkdir(p.substr(0, slash).c_str(), 0755);
        slash = p.find('/', slash + 1);
    }
    mkdir(p.c_str(), 0755);
}

}  // namespace

Supervisor::Supervisor()
{
    const char* home = getenv("HOME");
    if (!home) home = "/tmp";
    profile_dir_ = std::string(home) + "/.cache/mac-phoenix/firefox-profile";
}

Supervisor::~Supervisor() { stop(); }

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
        reset_signals_for_exec();
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
            std::chrono::milliseconds(timeout_ms)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

bool Supervisor::prepare_firefox_profile()
{
    mkdir_p(profile_dir_);

    /* user.js applied on every Firefox start — overrides matching
     * prefs.js entries. Silences the noise that polluted our last
     * test (welcome panel, "import from Chrome", what's new tour,
     * data-collection prompts, default-browser nag). */
    std::string user_js = profile_dir_ + "/user.js";
    FILE* f = fopen(user_js.c_str(), "w");
    if (!f) {
        fprintf(stderr, "[BrowserSup] couldn't write %s: %s\n",
                user_js.c_str(), strerror(errno));
        return false;
    }
    fputs(
        "// Generated by mac-phoenix BrowserModule. Hand edits will be\n"
        "// overwritten on every supervisor start.\n"
        "user_pref(\"browser.aboutwelcome.enabled\", false);\n"
        "user_pref(\"browser.shell.checkDefaultBrowser\", false);\n"
        "user_pref(\"browser.shell.defaultBrowserCheckCount\", 1);\n"
        "user_pref(\"browser.startup.homepage_override.mstone\", \"ignore\");\n"
        "user_pref(\"datareporting.policy.dataSubmissionPolicyAcceptedVersion\", 999);\n"
        "user_pref(\"datareporting.policy.firstRunURL\", \"\");\n"
        "user_pref(\"toolkit.startup.max_resumed_crashes\", -1);\n"
        "user_pref(\"browser.sessionstore.resume_from_crash\", false);\n"
        "user_pref(\"browser.tabs.warnOnClose\", false);\n"
        "user_pref(\"browser.warnOnQuit\", false);\n"
        "user_pref(\"browser.privatebrowsing.vpnpromourl\", \"\");\n"
        "user_pref(\"browser.newtabpage.activity-stream.feeds.section.topstories\", false);\n"
        "user_pref(\"browser.newtabpage.activity-stream.feeds.topsites\", false);\n"
        "user_pref(\"app.normandy.first_run\", false);\n"
        "user_pref(\"app.update.auto\", false);\n"
        "user_pref(\"app.update.enabled\", false);\n"
        "user_pref(\"extensions.autoDisableScopes\", 0);\n"
        "user_pref(\"signon.rememberSignons\", false);\n"
        /* WebDriver BiDi (M4.5). active-protocols=1 enables CDP only,
         * 2 enables BiDi only, 3 enables both. We only need BiDi. */
        "user_pref(\"remote.active-protocols\", 2);\n"
        "user_pref(\"remote.log.level\", \"Info\");\n",
        f);
    fclose(f);
    return true;
}

bool Supervisor::spawn_firefox(const std::string& url)
{
    const char* firefox = find_firefox_binary();
    if (!firefox) {
        fprintf(stderr,
                "[BrowserSup] Firefox not found. Install via "
                "`tar -xJf firefox-latest.tar.xz -C /opt/` from "
                "https://www.mozilla.org/firefox/all/\n");
        return false;
    }

    char display_env[16];
    snprintf(display_env, sizeof(display_env), ":%d", display_);
    const std::string startup_url = url.empty() ? "about:blank" : url;

    pid_t pid = fork();
    if (pid < 0) { perror("[BrowserSup] fork(firefox)"); return false; }
    if (pid == 0) {
        int devnull = ::open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            ::dup2(devnull, 0);
            ::dup2(devnull, 1);
            ::dup2(devnull, 2);
            ::close(devnull);
        }
        /* Strip env to a minimal allowlist. Firefox's headless
         * detection is sensitive to many inherited vars (SSH_TTY,
         * XDG_SESSION_TYPE, GNOME_*, VSCODE_*, WAYLAND_DISPLAY) that
         * show up under SSH / VS Code sessions and force headless
         * mode regardless of DISPLAY. Pass only what Firefox needs:
         * HOME (for profile + cache), USER, LANG, PATH, DISPLAY,
         * GDK_BACKEND=x11. Everything else gets dropped. */
        const char* keep_home = getenv("HOME");
        const char* keep_user = getenv("USER");
        const char* keep_lang = getenv("LANG");
        clearenv();
        setenv("PATH", "/usr/bin:/bin", 1);
        if (keep_home) setenv("HOME", keep_home, 1);
        if (keep_user) setenv("USER", keep_user, 1);
        if (keep_lang) setenv("LANG", keep_lang, 1);
        setenv("DISPLAY", display_env, 1);
        setenv("GDK_BACKEND", "x11", 1);
        setenv("MOZ_DISABLE_GMP_SANDBOX", "1", 1);

        close_inherited_fds();
        reset_signals_for_exec();
        setpgid(0, 0);

        char port_arg[24];
        snprintf(port_arg, sizeof(port_arg), "%d", kBidiPort);

        execl(firefox, firefox,
              "--no-remote",
              "--profile", profile_dir_.c_str(),
              "--remote-debugging-port", port_arg,
              "--kiosk",
              "--new-window", startup_url.c_str(),
              (char*)nullptr);
        _exit(127);
    }
    firefox_pid_ = pid;
    fprintf(stderr, "[BrowserSup] Firefox pid=%d profile=%s url=%s\n",
            firefox_pid_, profile_dir_.c_str(), startup_url.c_str());
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

    if (!spawn_xvfb()) return false;
    if (!wait_for_x_socket(kXReadyTimeoutMs)) {
        fprintf(stderr, "[BrowserSup] Xvfb didn't expose :%d socket within %dms\n",
                display_, kXReadyTimeoutMs);
        stop();
        return false;
    }
    if (!prepare_firefox_profile()) { stop(); return false; }
    if (!spawn_firefox(initial_url))  { stop(); return false; }
    return true;
}

void Supervisor::stop()
{
    auto kill_group = [](int& pid) {
        if (pid <= 0) return;
        ::kill(-pid, SIGTERM);
        for (int i = 0; i < 20; i++) {
            int status = 0;
            if (waitpid(pid, &status, WNOHANG) == pid) { pid = 0; return; }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ::kill(-pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        pid = 0;
    };
    kill_group(firefox_pid_);
    kill_group(xvfb_pid_);
    display_ = -1;
}

}  // namespace browser
