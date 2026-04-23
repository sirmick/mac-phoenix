/*
 *  emulator_config.h - Unified emulator configuration
 *
 *  Single source of truth for emulator configuration, combining:
 *  - JSON config file
 *  - Command-line arguments (overrides)
 *  - Environment variables (lowest priority overrides)
 *  - Sensible defaults
 */

#ifndef EMULATOR_CONFIG_H
#define EMULATOR_CONFIG_H

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace config {

enum class Architecture {
    M68K,
    PPC  // Not yet implemented
};

enum class CPUBackend {
    UAE,      // Original interpreter
    Unicorn,  // QEMU-based JIT
    DualCPU,  // Validation mode (UAE + Unicorn)
    KPX       // Kheperix PPC interpreter
};

enum class NetworkMode {
    None,     // No networking (null driver)
    Socket    // Unix socket to net-bridge (smoltcp NAT + HTTPS proxy)
};

struct M68KConfig {
    bool fpu = true;
    bool jitexperimental = false;
    bool jitfpu = true;
    bool jitdebug = false;
    int jitcachesize = 8192;
    bool jitlazyflush = true;
    bool jitinline = true;
    std::string jitblacklist;
    bool idlewait = true;
    bool ignoresegv = true;
    bool swap_opt_cmd = true;
    int keyboardtype = 5;
};

struct PPCConfig {
    bool fpu = true;
    bool jit = false;
    bool jit68k = true;
    bool idlewait = true;
    bool ignoresegv = true;
    bool ignoreillegal = true;
    int keyboardtype = 5;
};

/*
 * Unified emulator configuration
 *
 * Priority order (highest to lowest):
 * 1. Command-line arguments
 * 2. JSON config file
 * 3. Environment variables
 * 4. Defaults (below)
 */
struct EmulatorConfig {
    // Singleton access
    static EmulatorConfig& instance() {
        static EmulatorConfig s_instance;
        return s_instance;
    }

    // Architecture & CPU
    Architecture architecture = Architecture::M68K;
    CPUBackend cpu_backend = CPUBackend::UAE;
    std::string emulator = "quadra";   // "se", "quadra", or "ppc"

    // Memory
    uint32_t ram_mb = 64;

    // ROM & disks
    std::string rom_path;
    std::vector<std::string> disk_paths;
    std::vector<std::string> cdrom_paths;
    std::vector<std::string> floppy_paths;
    std::vector<std::string> extfs_paths;

    // Video
    uint32_t screen_width = 640;
    uint32_t screen_height = 480;
    bool screenshots = false;

    // Audio (opt-in — enable with --audio CLI flag)
    bool audio_enabled = false;

    // Boot
    int bootdrive = 0;
    int bootdriver = 0;  // 0=any, -62=CDROM

    // Streaming
    std::string codec = "vp9";
    std::string mousemode = "absolute";

    // Web/Network
    bool enable_webserver = true;
    bool headless_http = false;  // serve HTTP API in headless mode (no WebRTC/video/audio)
    bool bridge_enabled = false; // enable automation bridge (INIT injection + file-based commands)
    std::string bridge_dir;      // temp directory for bridge file I/O (auto-created)
    int http_port = 11000;
    std::string client_dir = "./client";
    std::string storage_dir = "~/storage";

    // System
    bool nosound = false;
    bool zappram = false;
    bool dismiss_shutdown_dialog = true;
    std::string auto_launch_app;  // Mac path launched after desktop reached
    int frameskip = 6;
    int yearofs = 0;
    int dayofs = 0;
    bool udptunnel = false;
    int udpport = 6066;

    // Networking
    NetworkMode network = NetworkMode::None;
    std::string network_if;  // Interface name for raw mode (e.g. "eth0")

    // MITM TLS proxy (net-bridge): terminates modern TLS on the host side
    // and re-encrypts to the guest using classic-Mac-compatible SSLv3/TLS1.0
    // + weak-RSA ciphers. Opt-in only; one-time CA import required in guest.
    bool mitm_tls = false;
    std::string mitm_ports;    // comma-separated, e.g. "443,993"; empty → default (443)
    std::string mitm_ca_dir;   // where the local MITM CA lives; empty → net-bridge default

    // IPC child mode (--ipc flag, used for PPC subprocess)
    bool ipc_mode = false;

    // Timeout (0 = no timeout)
    int timeout_seconds = 0;

    // Logging & Debug
    int log_level = 0;             // 0=milestones, 1=important, 2=all ops, 3=+registers
    bool debug_connection = false;
    bool debug_mode_switch = false;
    bool debug_perf = false;
    bool debug_network = false;

    // Internal (not serialized to JSON)
    std::string config_path;       // where to save back
    nlohmann::json file_config_;   // tracks what's persisted on disk (UI changes only)

    // Arch sub-structs
    M68KConfig m68k;
    PPCConfig ppc;

    // Serialization
    nlohmann::json to_json() const;
    void merge_json(const nlohmann::json& j);      // file/startup → runtime only
    void merge_ui_json(const nlohmann::json& j);   // UI changes → runtime + file_config_
    bool save() const;

    // Helpers
    std::string screen_string() const {
        return std::to_string(screen_width) + "x" + std::to_string(screen_height);
    }

    const char* cpu_backend_string() const {
        switch (cpu_backend) {
            case CPUBackend::UAE: return "uae";
            case CPUBackend::Unicorn: return "unicorn";
            case CPUBackend::DualCPU: return "dualcpu";
            case CPUBackend::KPX: return "kpx";
        }
        return "uae";
    }

    bool fpu() const {
        return m68k.fpu;
    }

    const char* architecture_string() const {
        return (architecture == Architecture::M68K) ? "m68k" : "ppc";
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
