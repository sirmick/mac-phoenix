/*
 *  supervisor.cpp — Xvfb + Firefox orchestration. See supervisor.h.
 */
#include "supervisor.h"
#include "emulator_config.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

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
#ifdef __linux__
#include <sys/prctl.h>
#endif

namespace browser {

namespace {

constexpr int kFirstDisplay     = 99;
constexpr int kLastDisplay      = 119;
constexpr int kXReadyTimeoutMs  = 5000;
/* Initial Mac viewport size — Firefox spawns at this size, the guest
 * can grow/shrink within [1, kCanvasWidth × kCanvasHeight] via
 * BR_CMD_RESIZE → xcb_configure_window on the Firefox top-level. */
constexpr int kViewportWidth    = 624;
constexpr int kViewportHeight   = 384;
constexpr int kViewportDepth    = 24;
/* Xvfb root stays a fixed canvas — never resized after spawn. We
 * pick 1024×768 to match BR_FB_MAX, so the host SHM segment, the
 * pipeline's destination, and the maximum Mac window all line up
 * without per-layer caps. Resizing happens at the Firefox window
 * level (sub-region of root), not at the X server level — that
 * sidesteps Xvfb's RandR mode-list bookkeeping (which only honours
 * pre-existing modes; runtime mode creation works for the first
 * call but fails BadMatch on subsequent adds). */
constexpr int kCanvasWidth      = 1024;
constexpr int kCanvasHeight     =  768;
/* WebDriver BiDi listens here (M4.5). Firefox accepts both BiDi and
 * the legacy CDP on the same port; we use BiDi only. */
/* BiDi base port — supervisor probes from here upward to find the
 * first free TCP slot. Hardcoded 9222 collided when two mac-phoenix
 * processes ran on the same host. */
constexpr int kBidiBasePort     = 9222;

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

/* Ask the kernel to send us SIGTERM when our parent dies. Without this,
 * a SIGKILL'd or crashed mac-phoenix leaves Xvfb + Firefox orphaned and
 * reparented to init, where they run forever. PR_SET_PDEATHSIG is set
 * on the *thread* and inherited across exec(), so Firefox / Xvfb post-
 * exec still honour it. Linux-only — no portable equivalent on macOS,
 * but we don't ship --browser there anyway (Xvfb isn't a thing).
 *
 * Race window: parent could die between our fork and our prctl. We then
 * race against that signal arriving. The window is microseconds and we
 * call prctl as the very first child action; not worth a getppid() check
 * given the rest of the failure modes. */
void install_parent_death_signal()
{
#ifdef __linux__
    prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
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
    /* Per-pid profile so two mac-phoenix processes don't fight over
     * the same Firefox profile (cookies, cache, prefs.js writes).
     * Cleaned up at stop(). */
    profile_dir_ = std::string(home) + "/.cache/mac-phoenix/firefox-profile-"
                 + std::to_string((int)getpid());
}

Supervisor::~Supervisor() { stop(); }

int Supervisor::pick_free_display()
{
    for (int d = kFirstDisplay; d <= kLastDisplay; d++) {
        if (!x_socket_present(d)) return d;
    }
    return -1;
}

int Supervisor::pick_free_bidi_port()
{
    /* BiDi (WebDriver) listens on a TCP port that Firefox binds via
     * --remote-debugging-port. Hardcoded 9222 collides between
     * concurrent mac-phoenix instances. Probe upward and pick the
     * first free one. */
    constexpr int kRange = 21;   /* 9222..9242 */
    for (int p = kBidiBasePort; p < kBidiBasePort + kRange; p++) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) return -1;
        int yes = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(p);
        bool free_ = (::bind(s, (sockaddr*)&addr, sizeof(addr)) == 0);
        ::close(s);
        if (free_) return p;
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
             "%dx%dx%d", kCanvasWidth, kCanvasHeight, kViewportDepth);

    pid_t pid = fork();
    if (pid < 0) { perror("[BrowserSup] fork(Xvfb)"); return false; }
    if (pid == 0) {
        install_parent_death_signal();
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

    /* Where browser downloads land. Inside bridge_dir means the guest
     * already sees this folder via ExtFS at Host:MacPhoenix:Downloads,
     * so there's nothing extra to plumb — Save As… in Firefox writes
     * the file, the guest's Finder sees it on the desktop folder
     * almost immediately. mkdir_p so Firefox doesn't fall back to
     * its own ~/Downloads if our path doesn't exist. */
    std::string downloads_dir;
    {
        const auto& cfg = config::EmulatorConfig::instance();
        if (!cfg.bridge_dir.empty()) {
            downloads_dir = cfg.bridge_dir + "/Downloads";
            mkdir_p(downloads_dir);
        }
    }

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
        "user_pref(\"remote.log.level\", \"Info\");\n"
        /* Belt-and-suspenders for scrollbar suppression: switch from
         * GTK overlay scrollbars (which paint a track during/after
         * scroll, leaking a 16-px gutter into the guest PixMap) to
         * classic always-on scrollbars. The CSS preload's
         * scrollbar-width:none then collapses them to zero width and
         * no gutter is reserved. (The other two prefs I tried —
         * layout.testing.overlay-scrollbars.always-visible and
         * ui.useOverlayScrollbars — turned out to be no-ops on Linux
         * Firefox; dropped.) */
        "user_pref(\"widget.gtk.overlay-scrollbars.enabled\", false);\n",
        f);

    /* Downloads → bridge_dir/Downloads. folderList=2 means "use the
     * dir specified in browser.download.dir" (0 = desktop, 1 = system
     * default Downloads). useDownloadDir=true skips the Save As…
     * sheet for known mime types. always_ask… on a per-mime basis
     * stays at default — Firefox still asks before opening a file
     * with a helper app, just doesn't ask *where* to save. */
    if (!downloads_dir.empty()) {
        std::string esc;
        esc.reserve(downloads_dir.size());
        for (char c : downloads_dir) {
            if (c == '\\' || c == '"') esc.push_back('\\');
            esc.push_back(c);
        }
        fprintf(f, "user_pref(\"browser.download.folderList\", 2);\n");
        fprintf(f, "user_pref(\"browser.download.dir\", \"%s\");\n",
                esc.c_str());
        fprintf(f, "user_pref(\"browser.download.useDownloadDir\", true);\n");
        fprintf(f, "user_pref(\"browser.download.alwaysOpenPanel\", false);\n");
        fprintf(f, "user_pref(\"browser.download.always_ask_before_handling_new_types\", false);\n");
        fprintf(f, "user_pref(\"browser.download.manager.showWhenStarting\", false);\n");
        fprintf(stderr, "[BrowserSup] downloads → %s\n",
                downloads_dir.c_str());
    }

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
        install_parent_death_signal();
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
        snprintf(port_arg, sizeof(port_arg), "%d", bidi_port_);
        char width_arg[16], height_arg[16];
        snprintf(width_arg,  sizeof(width_arg),  "%d", kViewportWidth);
        snprintf(height_arg, sizeof(height_arg), "%d", kViewportHeight);

        /* --kiosk hides chrome but on Linux/Xvfb doesn't reliably
         * fullscreen the window (Mozilla bug 1848677). --width /
         * --height force the initial window to match the Xvfb screen
         * exactly so Firefox actually fills the root pixmap. */
        execl(firefox, firefox,
              "--no-remote",
              "--profile", profile_dir_.c_str(),
              "--remote-debugging-port", port_arg,
              "--width",  width_arg,
              "--height", height_arg,
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
    bidi_port_ = pick_free_bidi_port();
    if (bidi_port_ < 0) {
        fprintf(stderr, "[BrowserSup] no free BiDi port near %d\n",
                kBidiBasePort);
        return false;
    }
    fprintf(stderr, "[BrowserSup] picked display=:%d  bidi=%d\n",
            display_, bidi_port_);

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
    int killed_display = display_;
    kill_group(firefox_pid_);
    kill_group(xvfb_pid_);

    /* Xvfb deletes its own socket + lock cleanly on SIGTERM, but a SIGKILL
     * fallback (or an Xvfb that was already wedged) leaves them behind.
     * Stale `/tmp/.X<n>-lock` makes a fresh Xvfb on the same display
     * refuse to start ("Server is already active"); stale
     * `/tmp/.X11-unix/X<n>` makes pick_free_display() skip the slot.
     * Best-effort unlink — harmless if Xvfb already cleaned up. */
    if (killed_display >= 0) {
        char p[64];
        snprintf(p, sizeof(p), "/tmp/.X%d-lock", killed_display);
        ::unlink(p);
        snprintf(p, sizeof(p), "/tmp/.X11-unix/X%d", killed_display);
        ::unlink(p);
    }

    display_   = -1;
    bidi_port_ = -1;

    /* Best-effort cleanup of the per-pid Firefox profile so we don't
     * accumulate ~/.cache/mac-phoenix/firefox-profile-12345/ directories
     * after every run. Skipped if profile_dir_ doesn't look like ours
     * (defensive — never delete a hand-picked profile). */
    if (!profile_dir_.empty() &&
        profile_dir_.find("/firefox-profile-") != std::string::npos) {
        std::string cmd = "rm -rf -- '" + profile_dir_ + "'";
        if (system(cmd.c_str()) != 0) {
            fprintf(stderr, "[BrowserSup] note: profile cleanup left "
                    "%s on disk\n", profile_dir_.c_str());
        }
    }
}

}  // namespace browser
