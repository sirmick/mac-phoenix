/*
 *  emulator_config.cpp - Unified emulator configuration implementation
 */

#include "emulator_config.h"
#include "config_manager.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <sys/stat.h>

namespace config {

// Helper: Check if file exists
static bool file_exists(const char* path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

// Helper: Expand ~ to home directory
static std::string expand_home(const std::string& path) {
    if (path.empty() || path[0] != '~') {
        return path;
    }
    const char* home = getenv("HOME");
    if (!home) {
        return path;  // Can't expand
    }
    return std::string(home) + path.substr(1);
}

/*
 * Load from JSON config only (no CLI overrides)
 */
EmulatorConfig load_from_json(const char* config_path) {
    EmulatorConfig config;

    // If no config path, return defaults
    if (!config_path) {
        fprintf(stderr, "[Config] No config file specified, using defaults\n");
        return config;
    }

    // Check if config file exists
    std::string expanded_path = expand_home(config_path);
    if (!file_exists(expanded_path.c_str())) {
        fprintf(stderr, "[Config] Config file not found: %s (using defaults)\n",
                expanded_path.c_str());
        return config;
    }

    // Load JSON config using existing config_manager
    fprintf(stderr, "[Config] Loading config from: %s\n", expanded_path.c_str());
    MacemuConfig json_config = config::load_config(expanded_path);

    // Extract values from JSON config
    // Architecture
    if (json_config.web.emulator == "ppc") {
        config.architecture = Architecture::PPC;
    } else {
        config.architecture = Architecture::M68K;
    }

    // Memory
    config.ram_mb = json_config.common.ram;

    // CPU (use m68k section for now)
    config.cpu_type = static_cast<M68KCPUType>(json_config.m68k.cpu);
    config.fpu = json_config.m68k.fpu;

    // ROM (not resolved yet - just store filename)
    config.rom_path = json_config.m68k.rom;

    // Disks
    config.disk_paths = json_config.m68k.disks;
    config.cdrom_paths = json_config.m68k.cdroms;

    // Video
    // Parse "1024x768" format
    size_t x_pos = json_config.common.screen.find('x');
    if (x_pos != std::string::npos) {
        config.screen_width = std::stoi(json_config.common.screen.substr(0, x_pos));
        config.screen_height = std::stoi(json_config.common.screen.substr(x_pos + 1));
    }

    // Audio
    config.audio_enabled = json_config.common.sound;

    // WebRTC settings
    config.http_port = json_config.web.http_port;
    config.storage_dir = json_config.web.storage_dir;

    // Backend (default to UAE, can be overridden by CLI)
    config.cpu_backend = CPUBackend::UAE;

    fprintf(stderr, "[Config] JSON config loaded successfully\n");
    return config;
}

/*
 * Apply CLI argument overrides to config
 */
const char* apply_cli_overrides(EmulatorConfig& config,
                                  int& argc,
                                  char** argv) {
    const char* rom_path = nullptr;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;  // Skip already-consumed args

        // --config <path> (already processed by caller, just consume it)
        if (strcmp(argv[i], "--config") == 0 && i+1 < argc) {
            argv[i] = nullptr;
            argv[i+1] = nullptr;
            i++;
            continue;
        }

        // --arch m68k|ppc
        if (strcmp(argv[i], "--arch") == 0 && i+1 < argc) {
            if (strcmp(argv[i+1], "ppc") == 0) {
                config.architecture = Architecture::PPC;
            } else {
                config.architecture = Architecture::M68K;
            }
            argv[i] = nullptr;
            argv[i+1] = nullptr;
            i++;
            continue;
        }

        // --ram <mb>
        if (strcmp(argv[i], "--ram") == 0 && i+1 < argc) {
            config.ram_mb = static_cast<uint32_t>(atoi(argv[i+1]));
            argv[i] = nullptr;
            argv[i+1] = nullptr;
            i++;
            continue;
        }

        // --cpu <0-4>
        if (strcmp(argv[i], "--cpu") == 0 && i+1 < argc) {
            int cpu = atoi(argv[i+1]);
            if (cpu >= 0 && cpu <= 4) {
                config.cpu_type = static_cast<M68KCPUType>(cpu);
            }
            argv[i] = nullptr;
            argv[i+1] = nullptr;
            i++;
            continue;
        }

        // --fpu
        if (strcmp(argv[i], "--fpu") == 0) {
            config.fpu = true;
            argv[i] = nullptr;
            continue;
        }

        // --no-fpu
        if (strcmp(argv[i], "--no-fpu") == 0) {
            config.fpu = false;
            argv[i] = nullptr;
            continue;
        }

        // --backend uae|unicorn|dualcpu
        if (strcmp(argv[i], "--backend") == 0 && i+1 < argc) {
            if (strcmp(argv[i+1], "unicorn") == 0) {
                config.cpu_backend = CPUBackend::Unicorn;
            } else if (strcmp(argv[i+1], "dualcpu") == 0) {
                config.cpu_backend = CPUBackend::DualCPU;
            } else {
                config.cpu_backend = CPUBackend::UAE;
            }
            argv[i] = nullptr;
            argv[i+1] = nullptr;
            i++;
            continue;
        }

        // --no-webserver
        if (strcmp(argv[i], "--no-webserver") == 0) {
            config.enable_webrtc = false;
            argv[i] = nullptr;
            continue;
        }

        // --debug-connection
        if (strcmp(argv[i], "--debug-connection") == 0) {
            config.debug_connection = true;
            argv[i] = nullptr;
            continue;
        }

        // --debug-mode-switch
        if (strcmp(argv[i], "--debug-mode-switch") == 0) {
            config.debug_mode_switch = true;
            argv[i] = nullptr;
            continue;
        }

        // --debug-perf
        if (strcmp(argv[i], "--debug-perf") == 0) {
            config.debug_perf = true;
            argv[i] = nullptr;
            continue;
        }

        // --disk <path> (can be specified multiple times)
        if (strcmp(argv[i], "--disk") == 0 && i+1 < argc) {
            // Add to disk_paths, will be resolved later
            config.disk_paths.push_back(argv[i+1]);
            argv[i] = nullptr;
            argv[i+1] = nullptr;
            i++;
            continue;
        }

        // Positional argument: ROM path
        if (argv[i][0] != '-') {
            rom_path = argv[i];
            argv[i] = nullptr;
            continue;
        }

        // Unknown argument - leave it (might be for old prefs system)
        fprintf(stderr, "[Config] Warning: Unknown argument: %s\n", argv[i]);
    }

    return rom_path;
}

/*
 * Resolve ROM path from config or CLI argument
 */
std::string resolve_rom_path(const std::string& rom_filename,
                               const std::string& storage_dir) {
    if (rom_filename.empty()) {
        return "";
    }

    // Absolute path
    if (rom_filename[0] == '/' || rom_filename[0] == '~') {
        return expand_home(rom_filename);
    }

    // Relative path - resolve to storage_dir/roms/
    return storage_dir + "/roms/" + rom_filename;
}

/*
 * Resolve disk image path from config or CLI argument
 */
std::string resolve_disk_path(const std::string& disk_filename,
                                const std::string& storage_dir) {
    if (disk_filename.empty()) {
        return "";
    }

    // Absolute path
    if (disk_filename[0] == '/' || disk_filename[0] == '~') {
        return expand_home(disk_filename);
    }

    // Relative path - resolve to storage_dir/images/
    return storage_dir + "/images/" + disk_filename;
}

/*
 * Load emulator configuration from multiple sources
 */
EmulatorConfig load_emulator_config(const char* config_path,
                                      int& argc,
                                      char** argv) {
    fprintf(stderr, "[Config] ========================================\n");
    fprintf(stderr, "[Config] Loading emulator configuration\n");
    fprintf(stderr, "[Config] ========================================\n");

    // 1. Check for --config override in CLI args (before loading JSON)
    const char* config_override = nullptr;
    for (int i = 1; i < argc; i++) {
        if (argv[i] && strcmp(argv[i], "--config") == 0 && i+1 < argc) {
            config_override = argv[i+1];
            break;
        }
    }

    // Use CLI config path if provided, else use parameter
    const char* final_config_path = config_override ? config_override : config_path;

    // 2. Load from JSON config
    EmulatorConfig config = load_from_json(final_config_path);

    // 3. Apply CLI overrides
    const char* rom_path_from_cli = apply_cli_overrides(config, argc, argv);

    // 4. Handle ROM path
    if (rom_path_from_cli) {
        // CLI ROM path overrides JSON config
        fprintf(stderr, "[Config] ROM path from CLI: %s\n", rom_path_from_cli);
        config.rom_path = rom_path_from_cli;
    } else if (!config.rom_path.empty()) {
        // Resolve ROM path from JSON config
        std::string resolved = resolve_rom_path(config.rom_path, config.storage_dir);
        fprintf(stderr, "[Config] ROM path from JSON: %s\n", resolved.c_str());
        config.rom_path = resolved;
    } else {
        fprintf(stderr, "[Config] No ROM path specified (webserver mode)\n");
    }

    // 5. Resolve disk paths
    // Disk paths from CLI (--disk) are kept as-is (absolute or relative to cwd)
    // Disk paths from JSON config are resolved relative to storage_dir/images/
    std::vector<std::string> resolved_disks;
    for (const auto& disk : config.disk_paths) {
        if (disk.empty()) {
            continue;
        }
        // If path starts with '/' or '~', treat as absolute
        if (disk[0] == '/' || disk[0] == '~') {
            resolved_disks.push_back(expand_home(disk));
        } else {
            // Relative path - resolve to storage_dir/images/
            resolved_disks.push_back(resolve_disk_path(disk, config.storage_dir));
        }
    }
    config.disk_paths = resolved_disks;

    // 6. Apply CPU_BACKEND environment variable if not set by CLI
    const char* backend_env = getenv("CPU_BACKEND");
    if (backend_env) {
        if (strcmp(backend_env, "unicorn") == 0) {
            config.cpu_backend = CPUBackend::Unicorn;
        } else if (strcmp(backend_env, "dualcpu") == 0) {
            config.cpu_backend = CPUBackend::DualCPU;
        } else {
            config.cpu_backend = CPUBackend::UAE;
        }
        fprintf(stderr, "[Config] CPU backend from environment: %s\n",
                config.cpu_backend_string());
    }

    fprintf(stderr, "[Config] Configuration loaded successfully\n");
    fprintf(stderr, "[Config] ========================================\n");

    return config;
}

/*
 * Print configuration to stderr (for debugging)
 */
void print_config(const EmulatorConfig& config) {
    fprintf(stderr, "\n[Config] ========================================\n");
    fprintf(stderr, "[Config] Emulator Configuration\n");
    fprintf(stderr, "[Config] ========================================\n");
    fprintf(stderr, "[Config] Architecture:  %s\n", config.architecture_string());
    fprintf(stderr, "[Config] RAM:           %u MB\n", config.ram_mb);
    fprintf(stderr, "[Config] CPU Type:      680%02d\n",
            (config.cpu_type == M68KCPUType::M68000) ? 0 :
            (static_cast<int>(config.cpu_type) * 10 + 20));
    fprintf(stderr, "[Config] FPU:           %s\n", config.fpu ? "Yes" : "No");
    fprintf(stderr, "[Config] CPU Backend:   %s\n", config.cpu_backend_string());
    fprintf(stderr, "[Config] ROM Path:      %s\n",
            config.rom_path.empty() ? "(none)" : config.rom_path.c_str());
    fprintf(stderr, "[Config] Screen:        %ux%u\n",
            config.screen_width, config.screen_height);
    fprintf(stderr, "[Config] Audio:         %s\n", config.audio_enabled ? "Yes" : "No");
    fprintf(stderr, "[Config] WebRTC:        %s\n", config.enable_webrtc ? "Yes" : "No");
    if (config.enable_webrtc) {
        fprintf(stderr, "[Config] HTTP Port:     %d\n", config.http_port);
        fprintf(stderr, "[Config] Signaling:     %d\n", config.signaling_port);
        fprintf(stderr, "[Config] Storage Dir:   %s\n", config.storage_dir.c_str());
    }
    if (!config.disk_paths.empty()) {
        fprintf(stderr, "[Config] Disks:\n");
        for (const auto& disk : config.disk_paths) {
            fprintf(stderr, "[Config]   - %s\n", disk.c_str());
        }
    }
    fprintf(stderr, "[Config] ========================================\n\n");
}

}  // namespace config
