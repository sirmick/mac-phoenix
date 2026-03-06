/*
 *  emulator_config.cpp - Unified emulator configuration implementation
 */

#include "emulator_config.h"
#include "json_utils.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <fstream>
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
    if (path.empty() || path[0] != '~') return path;
    const char* home = getenv("HOME");
    if (!home) return path;
    return std::string(home) + path.substr(1);
}

// Helper: Parse "WIDTHxHEIGHT" string
static bool parse_screen(const std::string& s, uint32_t& w, uint32_t& h) {
    size_t x = s.find('x');
    if (x == std::string::npos) return false;
    w = std::stoi(s.substr(0, x));
    h = std::stoi(s.substr(x + 1));
    return w > 0 && h > 0;
}

/*
 * Load from JSON config file
 */
static void load_from_json(EmulatorConfig& config, const char* path) {
    if (!path) return;

    std::string expanded = expand_home(path);
    if (!file_exists(expanded.c_str())) {
        fprintf(stderr, "[Config] No config file at %s, using defaults\n", expanded.c_str());
        return;
    }

    fprintf(stderr, "[Config] Loading: %s\n", expanded.c_str());

    try {
        auto j = json_utils::parse_file(expanded);

        // CPU / Memory
        if (j.contains("cpu")) {
            auto& cpu = j["cpu"];
            if (cpu.contains("type")) {
                int t = json_utils::get_int(cpu, "type");
                if (t >= 0 && t <= 4) config.cpu_type = static_cast<M68KCPUType>(t);
            }
            if (cpu.contains("fpu")) config.fpu = json_utils::get_bool(cpu, "fpu");
            if (cpu.contains("backend")) {
                std::string b = json_utils::get_string(cpu, "backend");
                if (b == "unicorn") config.cpu_backend = CPUBackend::Unicorn;
                else if (b == "dualcpu") config.cpu_backend = CPUBackend::DualCPU;
                else config.cpu_backend = CPUBackend::UAE;
            }
        }

        if (j.contains("ram")) config.ram_mb = json_utils::get_int(j, "ram");

        // ROM
        if (j.contains("rom")) config.rom_path = json_utils::get_string(j, "rom");

        // Storage
        if (j.contains("disks")) config.disk_paths = json_utils::get_string_array(j, "disks");
        if (j.contains("cdroms")) config.cdrom_paths = json_utils::get_string_array(j, "cdroms");

        // Video
        if (j.contains("screen")) {
            std::string s = json_utils::get_string(j, "screen");
            uint32_t w, h;
            if (parse_screen(s, w, h)) {
                config.screen_width = w;
                config.screen_height = h;
            }
        }
        if (j.contains("audio")) config.audio_enabled = json_utils::get_bool(j, "audio");

        // Web
        if (j.contains("web")) {
            auto& web = j["web"];
            if (web.contains("enabled")) config.enable_webrtc = json_utils::get_bool(web, "enabled");
            if (web.contains("http_port")) config.http_port = json_utils::get_int(web, "http_port");
            if (web.contains("signaling_port")) config.signaling_port = json_utils::get_int(web, "signaling_port");
            if (web.contains("storage_dir")) config.storage_dir = json_utils::get_string(web, "storage_dir");
        }

        // Legacy format support: "common", "m68k", "web" sections
        if (j.contains("common")) {
            auto& common = j["common"];
            if (common.contains("ram")) config.ram_mb = json_utils::get_int(common, "ram");
            if (common.contains("screen")) {
                std::string s = json_utils::get_string(common, "screen");
                uint32_t w, h;
                if (parse_screen(s, w, h)) {
                    config.screen_width = w;
                    config.screen_height = h;
                }
            }
            if (common.contains("sound")) config.audio_enabled = json_utils::get_bool(common, "sound");
        }
        if (j.contains("m68k")) {
            auto& m68k = j["m68k"];
            if (m68k.contains("rom")) config.rom_path = json_utils::get_string(m68k, "rom");
            if (m68k.contains("cpu")) {
                int t = json_utils::get_int(m68k, "cpu");
                if (t >= 0 && t <= 4) config.cpu_type = static_cast<M68KCPUType>(t);
            }
            if (m68k.contains("fpu")) config.fpu = json_utils::get_bool(m68k, "fpu");
            if (m68k.contains("disks")) config.disk_paths = json_utils::get_string_array(m68k, "disks");
            if (m68k.contains("cdroms")) config.cdrom_paths = json_utils::get_string_array(m68k, "cdroms");
        }

    } catch (const std::exception& e) {
        fprintf(stderr, "[Config] JSON parse error: %s\n", e.what());
    }
}

/*
 * Apply CLI argument overrides
 */
static const char* apply_cli_overrides(EmulatorConfig& config, int& argc, char** argv) {
    const char* rom_path = nullptr;
    bool backend_set_by_cli = false;
    bool timeout_set_by_cli = false;
    bool screenshots_set_by_cli = false;

    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;

        // --config <path> (consumed, already processed)
        if (strcmp(argv[i], "--config") == 0 && i+1 < argc) {
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --rom <path>
        if (strcmp(argv[i], "--rom") == 0 && i+1 < argc) {
            config.rom_path = argv[i+1];
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --disk <path> (repeatable)
        if (strcmp(argv[i], "--disk") == 0 && i+1 < argc) {
            config.disk_paths.push_back(argv[i+1]);
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --cdrom <path> (repeatable)
        if (strcmp(argv[i], "--cdrom") == 0 && i+1 < argc) {
            config.cdrom_paths.push_back(argv[i+1]);
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --ram <mb>
        if (strcmp(argv[i], "--ram") == 0 && i+1 < argc) {
            config.ram_mb = static_cast<uint32_t>(atoi(argv[i+1]));
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --cpu <0-4>
        if (strcmp(argv[i], "--cpu") == 0 && i+1 < argc) {
            int cpu = atoi(argv[i+1]);
            if (cpu >= 0 && cpu <= 4) config.cpu_type = static_cast<M68KCPUType>(cpu);
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --fpu / --no-fpu
        if (strcmp(argv[i], "--fpu") == 0) {
            config.fpu = true; argv[i] = nullptr; continue;
        }
        if (strcmp(argv[i], "--no-fpu") == 0) {
            config.fpu = false; argv[i] = nullptr; continue;
        }

        // --backend <name>
        if (strcmp(argv[i], "--backend") == 0 && i+1 < argc) {
            if (strcmp(argv[i+1], "unicorn") == 0) config.cpu_backend = CPUBackend::Unicorn;
            else if (strcmp(argv[i+1], "dualcpu") == 0) config.cpu_backend = CPUBackend::DualCPU;
            else config.cpu_backend = CPUBackend::UAE;
            backend_set_by_cli = true;
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --screen <WxH>
        if (strcmp(argv[i], "--screen") == 0 && i+1 < argc) {
            uint32_t w, h;
            if (parse_screen(argv[i+1], w, h)) {
                config.screen_width = w;
                config.screen_height = h;
            }
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --port <n>
        if (strcmp(argv[i], "--port") == 0 && i+1 < argc) {
            config.http_port = atoi(argv[i+1]);
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --signaling-port <n>
        if (strcmp(argv[i], "--signaling-port") == 0 && i+1 < argc) {
            config.signaling_port = atoi(argv[i+1]);
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --timeout <sec>
        if (strcmp(argv[i], "--timeout") == 0 && i+1 < argc) {
            config.timeout_seconds = atoi(argv[i+1]);
            timeout_set_by_cli = true;
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --screenshots
        if (strcmp(argv[i], "--screenshots") == 0) {
            config.screenshots = true;
            screenshots_set_by_cli = true;
            argv[i] = nullptr; continue;
        }

        // --no-webserver
        if (strcmp(argv[i], "--no-webserver") == 0) {
            config.enable_webrtc = false;
            argv[i] = nullptr; continue;
        }

        // --arch m68k|ppc
        if (strcmp(argv[i], "--arch") == 0 && i+1 < argc) {
            if (strcmp(argv[i+1], "ppc") == 0) config.architecture = Architecture::PPC;
            else config.architecture = Architecture::M68K;
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // Debug flags
        if (strcmp(argv[i], "--debug-connection") == 0) {
            config.debug_connection = true; argv[i] = nullptr; continue;
        }
        if (strcmp(argv[i], "--debug-mode-switch") == 0) {
            config.debug_mode_switch = true; argv[i] = nullptr; continue;
        }
        if (strcmp(argv[i], "--debug-perf") == 0) {
            config.debug_perf = true; argv[i] = nullptr; continue;
        }

        // Positional: ROM path (last non-flag arg)
        if (argv[i][0] != '-') {
            rom_path = argv[i];
            argv[i] = nullptr; continue;
        }

        fprintf(stderr, "[Config] Unknown argument: %s\n", argv[i]);
    }

    // Apply environment variables (lowest priority — only if not set by CLI/JSON)
    if (!backend_set_by_cli) {
        const char* env = getenv("CPU_BACKEND");
        if (env) {
            if (strcmp(env, "unicorn") == 0) config.cpu_backend = CPUBackend::Unicorn;
            else if (strcmp(env, "dualcpu") == 0) config.cpu_backend = CPUBackend::DualCPU;
            else config.cpu_backend = CPUBackend::UAE;
        }
    }

    if (!timeout_set_by_cli && config.timeout_seconds == 0) {
        const char* env = getenv("EMULATOR_TIMEOUT");
        if (env) config.timeout_seconds = atoi(env);
    }

    if (!screenshots_set_by_cli && !config.screenshots) {
        if (getenv("MACEMU_SCREENSHOTS")) config.screenshots = true;
    }

    return rom_path;
}

/*
 * Load emulator configuration from all sources
 */
EmulatorConfig load_emulator_config(const char* config_path,
                                      int& argc,
                                      char** argv) {
    EmulatorConfig config;

    // 1. Check for --config override before loading JSON
    const char* config_override = nullptr;
    for (int i = 1; i < argc; i++) {
        if (argv[i] && strcmp(argv[i], "--config") == 0 && i+1 < argc) {
            config_override = argv[i+1];
            break;
        }
    }
    const char* final_path = config_override ? config_override : config_path;

    // 2. Load JSON config (sets values above defaults)
    load_from_json(config, final_path);

    // 3. Apply CLI overrides + env vars (highest priority)
    const char* rom_from_cli = apply_cli_overrides(config, argc, argv);

    // 4. Resolve ROM path
    if (rom_from_cli) {
        config.rom_path = rom_from_cli;
    }
    if (!config.rom_path.empty()) {
        config.rom_path = expand_home(config.rom_path);
        // If relative and storage_dir is set, resolve against storage_dir/roms/
        if (config.rom_path[0] != '/' && !config.storage_dir.empty()) {
            config.rom_path = config.storage_dir + "/roms/" + config.rom_path;
        }
    }

    // 5. Resolve disk/cdrom paths
    auto resolve_paths = [&](std::vector<std::string>& paths) {
        for (auto& p : paths) {
            p = expand_home(p);
            if (p[0] != '/' && !config.storage_dir.empty()) {
                p = config.storage_dir + "/images/" + p;
            }
        }
    };
    resolve_paths(config.disk_paths);
    resolve_paths(config.cdrom_paths);

    return config;
}

/*
 * Print configuration summary
 */
void print_config(const EmulatorConfig& config) {
    fprintf(stderr, "[Config] Architecture: %s\n", config.architecture_string());
    fprintf(stderr, "[Config] RAM: %u MB\n", config.ram_mb);
    fprintf(stderr, "[Config] CPU: 680%d0, FPU: %s, Backend: %s\n",
            config.cpu_type_int(), config.fpu ? "yes" : "no",
            config.cpu_backend_string());
    fprintf(stderr, "[Config] ROM: %s\n",
            config.rom_path.empty() ? "(none)" : config.rom_path.c_str());
    fprintf(stderr, "[Config] Screen: %ux%u\n", config.screen_width, config.screen_height);
    for (const auto& d : config.disk_paths)
        fprintf(stderr, "[Config] Disk: %s\n", d.c_str());
    if (config.enable_webrtc)
        fprintf(stderr, "[Config] WebRTC: port %d, signaling %d\n",
                config.http_port, config.signaling_port);
    else
        fprintf(stderr, "[Config] WebRTC: disabled\n");
    if (config.timeout_seconds > 0)
        fprintf(stderr, "[Config] Timeout: %d seconds\n", config.timeout_seconds);
    if (config.screenshots)
        fprintf(stderr, "[Config] Screenshots: enabled\n");
}

}  // namespace config
