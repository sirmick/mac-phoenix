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

namespace config {

enum class Architecture {
    M68K,
    PPC  // Not yet implemented
};

enum class M68KCPUType {
    M68000 = 0,
    M68010 = 1,
    M68020 = 2,
    M68030 = 3,
    M68040 = 4
};

enum class CPUBackend {
    UAE,      // Original interpreter
    Unicorn,  // QEMU-based JIT
    DualCPU   // Validation mode (UAE + Unicorn)
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
    // Architecture
    Architecture architecture = Architecture::M68K;

    // Memory
    uint32_t ram_mb = 32;

    // CPU
    M68KCPUType cpu_type = M68KCPUType::M68040;
    bool fpu = true;
    CPUBackend cpu_backend = CPUBackend::UAE;

    // ROM
    std::string rom_path;

    // Disks
    std::vector<std::string> disk_paths;
    std::vector<std::string> cdrom_paths;

    // Video
    uint32_t screen_width = 640;
    uint32_t screen_height = 480;
    bool screenshots = false;  // Dump /tmp/macemu_screen_*.ppm

    // Audio
    bool audio_enabled = true;

    // Platform Drivers
    bool enable_webrtc = true;

    // WebRTC/HTTP (only if enable_webrtc = true)
    int http_port = 8080;
    int signaling_port = 8090;
    std::string storage_dir;

    // Timeout (0 = no timeout)
    int timeout_seconds = 0;

    // Debug
    bool debug_connection = false;
    bool debug_mode_switch = false;
    bool debug_perf = false;

    // Helpers
    const char* cpu_backend_string() const {
        switch (cpu_backend) {
            case CPUBackend::UAE: return "uae";
            case CPUBackend::Unicorn: return "unicorn";
            case CPUBackend::DualCPU: return "dualcpu";
        }
        return "uae";
    }

    int cpu_type_int() const {
        return static_cast<int>(cpu_type);
    }

    const char* architecture_string() const {
        return (architecture == Architecture::M68K) ? "m68k" : "ppc";
    }
};

/*
 * Load emulator configuration from all sources.
 *
 * Priority: CLI args > JSON config > env vars > defaults
 *
 * CLI arguments:
 *   --config <path>         Config file path (default: ~/.config/mac-phoenix/config.json)
 *   --rom <path>            ROM file path
 *   --disk <path>           Disk image (repeatable)
 *   --cdrom <path>          CD-ROM image (repeatable)
 *   --ram <mb>              RAM size in MB
 *   --cpu <0-4>             CPU type (68000-68040)
 *   --fpu / --no-fpu        FPU emulation
 *   --backend <name>        CPU backend: uae, unicorn, dualcpu
 *   --screen <WxH>          Screen resolution (e.g. 640x480)
 *   --port <n>              HTTP port
 *   --timeout <sec>         Auto-exit after N seconds
 *   --screenshots           Enable screenshot dumping
 *   --no-webserver          Disable WebRTC (headless mode)
 *   --debug-connection      Debug WebRTC connections
 *   --debug-mode-switch     Debug video mode switches
 *   --debug-perf            Debug performance
 *   <rom-path>              Positional: ROM file path
 *
 * Environment variables (lowest priority):
 *   CPU_BACKEND             uae, unicorn, dualcpu
 *   EMULATOR_TIMEOUT        Auto-exit timeout in seconds
 *   MACEMU_SCREENSHOTS      Enable screenshot mode (any value)
 */
EmulatorConfig load_emulator_config(const char* config_path,
                                      int& argc,
                                      char** argv);

// Print configuration summary to stderr
void print_config(const EmulatorConfig& config);

}  // namespace config

#endif  // EMULATOR_CONFIG_H
