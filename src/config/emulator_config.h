/*
 *  emulator_config.h - Unified emulator configuration
 *
 *  Single source of truth for emulator configuration, combining:
 *  - JSON config file
 *  - Command-line arguments (overrides)
 *  - Sensible defaults
 */

#ifndef EMULATOR_CONFIG_H
#define EMULATOR_CONFIG_H

#include <string>
#include <vector>
#include <cstdint>
#include <QJsonObject>

namespace config {

// CPU architecture. Not part of the JSON wire format — derived from the
// chosen backend. Used internally by CPUContext to track which code paths
// are active at runtime.
enum class Architecture {
    M68K,
    PPC
};

// CPU backend. Each token uniquely determines the CPU architecture, so
// there is no separate "architecture" axis in the config.
enum class Backend {
    UAE,           // m68k, hand-tuned interpreter (+optional JIT)
    KPX,           // ppc,  KPX translator (+optional PPC JIT, +optional 68k JIT)
    EXECUTOR       // m68k, no ROM: Executor's C++ Toolbox on the UAE core
};

enum class NetworkMode {
    None,     // No networking (null driver)
    Socket    // Unix socket to net-bridge (smoltcp NAT + HTTPS proxy)
};

/*
 * Unified emulator configuration
 *
 * Priority order (highest to lowest):
 * 1. Command-line arguments
 * 2. JSON config file
 * 3. Defaults (below)
 */
struct EmulatorConfig {
    // Singleton access
    static EmulatorConfig& instance() {
        static EmulatorConfig s_instance;
        return s_instance;
    }

    // CPU
    Backend backend = Backend::UAE;
    bool jit = false;        // enable backend's primary JIT (uae, kpx)
    bool jit68k = true;      // enable 68k-on-PPC DR JIT (kpx only)
    bool idlewait = true;    // pause CPU when guest idle (m68k rsrc patch + ppc SynchIdleTime)

    // UAE JIT internals (only consulted when backend=uae && jit=true).
    // Defaults are sensible; rarely tuned. Exposed via JSON for advanced users.
    bool jit_fpu = true;
    bool jit_debug = false;
    int  jit_cache_size = 8192;
    bool jit_lazy_flush = true;
    bool jit_inline = true;
    std::string jit_blacklist;

    // Memory
    uint32_t ram_mb = 64;

    // ROM & disks
    std::string rom_path;
    std::vector<std::string> disk_paths;
    std::vector<std::string> cdrom_paths;
    std::vector<std::string> extfs_paths;

    // Video — boot resolution (the mode the guest defaults to at boot)
    uint32_t screen_width = 640;
    uint32_t screen_height = 480;
    // Max resolution cap — if non-zero, modes whose width OR height exceed
    // these are filtered out of the published list (so guest never sees
    // them in Monitors cdev / can't switch up to them). 0 = unlimited.
    uint32_t max_screen_width = 0;
    uint32_t max_screen_height = 0;
    bool screenshots = false;          // CLI-only: dump PPM frames to /tmp

    // Audio
    bool audio_enabled = false;        // opt-in via --audio
    std::string audio_dump_path;       // --audio-dump: raw S16LE 48 kHz stereo of what goes out

    // Boot
    int bootdriver = 0;                // 0=any, -62=CDROM

    // Streaming
    std::string codec = "vp9";
    std::string mousemode = "absolute";

    // Keyboard remap. Drives the modifier mapping the JS client applies to
    // KeyboardEvent.code → Mac scancode lookup. Server doesn't otherwise
    // consult these — they're round-tripped to the client via /api/config.
    // Values are canonical Mac modifier names: command, control, option,
    // shift, or off (= disabled, key produces no Mac event). Empty string
    // means "no preference saved" — the client picks a platform-appropriate
    // default (Mac users get identity mapping; PC/Linux users get the
    // shortcut-habit swap of Ctrl→⌘ + Win→⌃).
    std::string kb_ctrl;               // empty → JS picks per platform
    std::string kb_alt;
    std::string kb_meta;
    std::string kb_fn;
    bool kb_release_on_blur = true;    // synth keyup for held keys when window loses focus

    // Web/Network
    bool enable_webserver = true;
    bool headless_http = false;        // serve HTTP API in headless mode (no WebRTC/video/audio)
    bool exit_with_guest = false;      // runtime only: the parent exits when its guest powers off
    bool bridge_enabled = false;       // automation bridge (INIT injection + file-based commands)
    std::string bridge_dir;            // temp directory for bridge file I/O (auto-created)
    bool browser_enabled = false;      // MacBrowser: allocate BrowserShm region (host-side spike + guest Browser.app)
    std::string macbrowser_dir;        // <extfs>/macbrowser/, persistent home for bookmarks.txt + Downloads/ (set when --browser is on)
    int http_port = 11000;
    std::string client_dir = "./client";
    std::string storage_dir = "~/storage";
    // Executor backend: its data directory (System Folder etc.). Empty =
    // <storage_dir>/executor. tools/macdecode/sideload_system.py fills one
    // with Apple's System file.
    std::string executor_data_dir;
    // Application Executor starts instead of its Browser (a host path,
    // e.g. "<data_dir>/System Folder/Finder"). Empty: Browser.
    std::string executor_app;
    // The usual way to pick the above (src/core/executor_systems.h): a
    // System under <storage>/executor-systems/<name>, what to start in it
    // ("finder", "browser" or a path inside it), and whether each run
    // starts from its clean copy. executor_data_dir/executor_app override.
    std::string executor_system;
    std::string executor_start = "finder";
    bool executor_fresh = true;
    bool executor_logtraps = false;    // Executor logs every trap call (args, results) to stderr
    int executor_logtraps_nesting = 1; // ... and calls nested this deep in Toolbox callbacks
    bool executor_writable_images = false; // Executor mounts disk images read-write (test copies)
    std::string cpu_core = "uae";      // CPU core for the backend's architecture: 68k "uae" or "musashi"

    // System
    bool zappram = false;
    bool dismiss_shutdown_dialog = true;

    // Networking
    NetworkMode network = NetworkMode::None;
    std::string network_if;            // Socket path when network=socket

    // Serial ports — empty = port disabled (driver Open returns openErr).
    // Non-empty values: "pty" allocates a fresh pseudo-tty (slave path
    // logged to stderr); anything else is treated as a device path
    // (/dev/ttyUSB0, /tmp/socat-link, /dev/pts/N) and opened directly.
    std::string serial_a;
    std::string serial_b;

    // IPC child mode (--ipc flag, used for PPC subprocess)
    bool ipc_mode = false;

    // Timeout (0 = no timeout)
    int timeout_seconds = 0;

    // Logging & Debug
    int log_level = 0;                 // 0=milestones, 1=important, 2=all ops, 3=+registers
    bool debug_connection = false;
    bool debug_mode_switch = false;
    bool debug_perf = false;
    bool debug_network = false;
    bool trace_atraps = false;         // record (A-trap, caller) pairs; dumped with snapshots
    bool deterministic = false;        // virtual clock: time counted in instructions (vclock.h)
    std::string snapshot_at;           // "NAME@SECONDS": snapshot that long after the traced app starts

    // Internal (not serialized)
    std::string config_path;
    QJsonObject file_config_;          // tracks what's persisted on disk (UI changes only)

    // Serialization
    QJsonObject to_json() const;
    void merge_json(const QJsonObject& j);      // file/startup → runtime only
    void merge_ui_json(const QJsonObject& j);   // UI changes → runtime + file_config_
    bool save() const;

    // Helpers
    std::string screen_string() const {
        return std::to_string(screen_width) + "x" + std::to_string(screen_height);
    }

    const char* backend_string() const {
        switch (backend) {
            case Backend::UAE:         return "uae";
            case Backend::KPX:         return "kpx";
            case Backend::EXECUTOR:    return "executor";
        }
        return "uae";
    }

    bool is_ppc() const {
        return backend == Backend::KPX;
    }

    // Backends that boot a real Mac ROM. Executor reimplements the Toolbox
    // and needs no ROM file.
    bool needs_rom() const {
        return backend != Backend::EXECUTOR;
    }

    Architecture architecture() const {
        return is_ppc() ? Architecture::PPC : Architecture::M68K;
    }

    const char* arch_string() const {
        return is_ppc() ? "ppc" : "m68k";
    }

    const char* network_string() const {
        switch (network) {
            case NetworkMode::Socket: return "socket";
            default: return "none";
        }
    }
};

/*
 * Load emulator configuration from all sources.
 *
 * Priority: CLI args > JSON config > defaults
 */
EmulatorConfig load_emulator_config(const char* config_path,
                                      int& argc,
                                      char** argv);

// Print configuration summary to stderr
void print_config(const EmulatorConfig& config);

}  // namespace config

#endif  // EMULATOR_CONFIG_H
