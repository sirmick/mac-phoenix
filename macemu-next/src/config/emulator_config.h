/*
 *  emulator_config.h - Unified emulator configuration
 *
 *  Single source of truth for emulator configuration, combining:
 *  - JSON config file
 *  - Command-line arguments (overrides)
 *  - Sensible defaults
 *
 *  Replaces the old prefs system for core emulation settings.
 */

#ifndef EMULATOR_CONFIG_H
#define EMULATOR_CONFIG_H

#include <string>
#include <vector>
#include <cstdint>

namespace config {

/*
 * Architecture type (future: will support PPC)
 */
enum class Architecture {
    M68K,
    PPC  // Not yet implemented
};

/*
 * M68K CPU type
 */
enum class M68KCPUType {
    M68000 = 0,
    M68010 = 1,
    M68020 = 2,
    M68030 = 3,
    M68040 = 4
};

/*
 * CPU backend selection (M68K only)
 */
enum class CPUBackend {
    UAE,      // Original interpreter
    Unicorn,  // QEMU-based JIT
    DualCPU   // Validation mode (UAE + Unicorn)
};

/*
 * Unified emulator configuration
 *
 * This structure contains all settings needed to initialize and run
 * the emulator. It's the single source of truth, populated from:
 * 1. JSON config file
 * 2. Command-line arguments (which override JSON)
 * 3. Defaults (if not specified)
 */
struct EmulatorConfig {
    // ========================================
    // Architecture
    // ========================================
    Architecture architecture = Architecture::M68K;

    // ========================================
    // Memory
    // ========================================
    uint32_t ram_mb = 32;  // Default: 32 MB

    // ========================================
    // CPU (M68K)
    // ========================================
    M68KCPUType cpu_type = M68KCPUType::M68040;
    bool fpu = true;
    CPUBackend cpu_backend = CPUBackend::UAE;

    // ========================================
    // ROM
    // ========================================
    std::string rom_path;  // Full resolved path to ROM file

    // ========================================
    // Disks
    // ========================================
    std::vector<std::string> disk_paths;  // Full resolved paths
    std::vector<std::string> cdrom_paths;

    // ========================================
    // Video
    // ========================================
    uint32_t screen_width = 1024;
    uint32_t screen_height = 768;

    // ========================================
    // Audio
    // ========================================
    bool audio_enabled = true;

    // ========================================
    // Platform Drivers
    // ========================================
    bool enable_webrtc = true;  // WebRTC video/audio drivers

    // ========================================
    // WebRTC Settings (only if enable_webrtc = true)
    // ========================================
    int http_port = 8000;
    int signaling_port = 8090;
    std::string storage_dir;  // Base directory for ROM/disk scanning

    // ========================================
    // Debug
    // ========================================
    bool debug_connection = false;
    bool debug_mode_switch = false;
    bool debug_perf = false;

    // ========================================
    // Helper Methods
    // ========================================

    // Convert CPU backend enum to string
    const char* cpu_backend_string() const {
        switch (cpu_backend) {
            case CPUBackend::UAE: return "uae";
            case CPUBackend::Unicorn: return "unicorn";
            case CPUBackend::DualCPU: return "dualcpu";
        }
        return "uae";
    }

    // Convert M68K CPU type to integer (for UAE)
    int cpu_type_int() const {
        return static_cast<int>(cpu_type);
    }

    // Convert architecture to string
    const char* architecture_string() const {
        return (architecture == Architecture::M68K) ? "m68k" : "ppc";
    }
};

/*
 * Load emulator configuration from multiple sources
 *
 * Priority order (highest to lowest):
 * 1. Command-line arguments
 * 2. JSON config file
 * 3. Defaults (in EmulatorConfig struct)
 *
 * Args:
 *   config_path: Path to JSON config file (can be NULL for defaults)
 *   argc, argv: Command-line arguments (will be modified - consumed args set to NULL)
 *
 * Returns:
 *   Populated EmulatorConfig
 *
 * CLI argument format:
 *   --config <path>         Override config file path
 *   --arch m68k|ppc         Override architecture
 *   --ram <mb>              Override RAM size (in MB)
 *   --cpu <0-4>             Override CPU type (M68K only)
 *   --fpu / --no-fpu        Override FPU setting
 *   --backend uae|unicorn|dualcpu  Override CPU backend
 *   --disk <path>           Add disk image (can be specified multiple times)
 *   --no-webserver          Disable WebRTC (headless mode)
 *   <rom-path>              ROM file path (positional argument)
 */
EmulatorConfig load_emulator_config(const char* config_path,
                                      int& argc,
                                      char** argv);

/*
 * Load from JSON config only (no CLI overrides)
 * Used internally by load_emulator_config()
 */
EmulatorConfig load_from_json(const char* config_path);

/*
 * Apply CLI argument overrides to config
 * Modifies argc/argv: consumed arguments are set to NULL
 * Returns: ROM path if found as positional argument, else NULL
 */
const char* apply_cli_overrides(EmulatorConfig& config,
                                  int& argc,
                                  char** argv);

/*
 * Resolve ROM path from config or CLI argument
 *
 * Logic:
 * - If rom_path is absolute (/... or ~/...), use as-is
 * - Otherwise, resolve relative to storage_dir/roms/
 *
 * Returns: Full resolved path
 */
std::string resolve_rom_path(const std::string& rom_filename,
                               const std::string& storage_dir);

/*
 * Print configuration to stderr (for debugging)
 */
void print_config(const EmulatorConfig& config);

}  // namespace config

#endif  // EMULATOR_CONFIG_H
