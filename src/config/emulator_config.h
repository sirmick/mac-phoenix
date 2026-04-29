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
#include <nlohmann/json.hpp>

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
    UnicornM68K,   // m68k, Unicorn TCG
    UnicornPPC,    // ppc,  Unicorn TCG
    KPX,           // ppc,  KPX translator (+optional PPC JIT, +optional 68k JIT)
    DualCPU        // m68k, UAE + Unicorn lockstep validation
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

    // Video
    uint32_t screen_width = 640;
    uint32_t screen_height = 480;
    bool screenshots = false;          // CLI-only: dump PPM frames to /tmp

    // Audio
    bool audio_enabled = false;        // opt-in via --audio

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
    bool bridge_enabled = false;       // automation bridge (INIT injection + file-based commands)
    std::string bridge_dir;            // temp directory for bridge file I/O (auto-created)
    bool browser_enabled = false;      // MacBrowser: allocate BrowserShm region (host-side spike + guest Browser.app)
    int http_port = 11000;
    std::string client_dir = "./client";
    std::string storage_dir = "~/storage";

    // System
    bool zappram = false;
    bool dismiss_shutdown_dialog = true;

    // Networking
    NetworkMode network = NetworkMode::None;
    std::string network_if;            // Socket path when network=socket

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

    // Internal (not serialized)
    std::string config_path;
    nlohmann::json file_config_;       // tracks what's persisted on disk (UI changes only)

    // Serialization
    nlohmann::json to_json() const;
    void merge_json(const nlohmann::json& j);      // file/startup → runtime only
    void merge_ui_json(const nlohmann::json& j);   // UI changes → runtime + file_config_
    bool save() const;

    // Helpers
    std::string screen_string() const {
        return std::to_string(screen_width) + "x" + std::to_string(screen_height);
    }

    const char* backend_string() const {
        switch (backend) {
            case Backend::UAE:         return "uae";
            case Backend::UnicornM68K: return "unicorn-m68k";
            case Backend::UnicornPPC:  return "unicorn-ppc";
            case Backend::KPX:         return "kpx";
            case Backend::DualCPU:     return "dualcpu";
        }
        return "uae";
    }

    bool is_ppc() const {
        return backend == Backend::UnicornPPC || backend == Backend::KPX;
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
