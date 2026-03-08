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

// Helper: Ensure parent directory exists
static void ensure_parent_dir(const std::string& path) {
    size_t slash = path.rfind('/');
    if (slash == std::string::npos) return;
    std::string dir = path.substr(0, slash);
    // Simple recursive mkdir (only one level deep usually)
    struct stat st;
    if (stat(dir.c_str(), &st) != 0) {
        // Try parent first
        size_t parent_slash = dir.rfind('/');
        if (parent_slash != std::string::npos) {
            std::string parent = dir.substr(0, parent_slash);
            if (stat(parent.c_str(), &st) != 0) {
                mkdir(parent.c_str(), 0755);
            }
        }
        mkdir(dir.c_str(), 0755);
    }
}

/*
 * Convert config to JSON
 */
nlohmann::json EmulatorConfig::to_json() const {
    nlohmann::json j;

    // Strip storage_dir prefix to save portable relative paths
    auto strip_roms = [&](const std::string& p) -> std::string {
        std::string prefix = storage_dir + "/roms/";
        if (!storage_dir.empty() && p.substr(0, prefix.size()) == prefix)
            return p.substr(prefix.size());
        return p;
    };
    auto strip_images = [&](const std::vector<std::string>& paths) -> std::vector<std::string> {
        std::string prefix = storage_dir + "/images/";
        std::vector<std::string> out;
        for (auto& p : paths) {
            if (!storage_dir.empty() && p.substr(0, prefix.size()) == prefix)
                out.push_back(p.substr(prefix.size()));
            else
                out.push_back(p);
        }
        return out;
    };

    j["architecture"] = architecture_string();
    j["cpu_backend"] = cpu_backend_string();
    j["ram_mb"] = ram_mb;
    j["screen"] = screen_string();
    j["audio"] = audio_enabled;
    j["rom"] = strip_roms(rom_path);
    j["disks"] = strip_images(disk_paths);
    j["cdroms"] = strip_images(cdrom_paths);
    j["floppies"] = strip_images(floppy_paths);
    j["extfs"] = extfs;
    j["bootdrive"] = bootdrive;
    j["bootdriver"] = bootdriver;
    j["codec"] = codec;
    j["mousemode"] = mousemode;
    j["http_port"] = http_port;
    j["signaling_port"] = signaling_port;
    j["client_dir"] = client_dir;
    j["storage_dir"] = storage_dir;
    j["log_level"] = log_level;
    j["nocdrom"] = nocdrom;
    j["nosound"] = nosound;
    j["zappram"] = zappram;
    j["frameskip"] = frameskip;
    j["yearofs"] = yearofs;
    j["dayofs"] = dayofs;
    j["udptunnel"] = udptunnel;
    j["udpport"] = udpport;
    j["debug_connection"] = debug_connection;
    j["debug_mode_switch"] = debug_mode_switch;
    j["debug_perf"] = debug_perf;

    j["m68k"]["cpu_type"] = m68k.cpu_type;
    j["m68k"]["fpu"] = m68k.fpu;
    j["m68k"]["modelid"] = m68k.modelid;
    j["m68k"]["jit"] = m68k.jit;
    j["m68k"]["idlewait"] = m68k.idlewait;
    j["m68k"]["ignoresegv"] = m68k.ignoresegv;
    j["m68k"]["jitfpu"] = m68k.jitfpu;
    j["m68k"]["jitdebug"] = m68k.jitdebug;
    j["m68k"]["jitcachesize"] = m68k.jitcachesize;
    j["m68k"]["jitlazyflush"] = m68k.jitlazyflush;
    j["m68k"]["jitinline"] = m68k.jitinline;
    j["m68k"]["jitblacklist"] = m68k.jitblacklist;
    j["m68k"]["swap_opt_cmd"] = m68k.swap_opt_cmd;
    j["m68k"]["keyboardtype"] = m68k.keyboardtype;

    j["ppc"]["cpu_type"] = ppc.cpu_type;
    j["ppc"]["fpu"] = ppc.fpu;
    j["ppc"]["modelid"] = ppc.modelid;
    j["ppc"]["jit"] = ppc.jit;
    j["ppc"]["jit68k"] = ppc.jit68k;
    j["ppc"]["idlewait"] = ppc.idlewait;
    j["ppc"]["ignoresegv"] = ppc.ignoresegv;
    j["ppc"]["ignoreillegal"] = ppc.ignoreillegal;
    j["ppc"]["keyboardtype"] = ppc.keyboardtype;

    return j;
}

/*
 * Merge JSON into config (partial updates OK)
 */
void EmulatorConfig::merge_json(const nlohmann::json& j) {
    if (j.contains("architecture")) {
        std::string a = json_utils::get_string(j, "architecture");
        if (a == "ppc") architecture = Architecture::PPC;
        else architecture = Architecture::M68K;
    }
    if (j.contains("cpu_backend")) {
        std::string b = json_utils::get_string(j, "cpu_backend");
        if (b == "unicorn") cpu_backend = CPUBackend::Unicorn;
        else if (b == "dualcpu") cpu_backend = CPUBackend::DualCPU;
        else cpu_backend = CPUBackend::UAE;
    }
    if (j.contains("ram_mb")) ram_mb = json_utils::get_int(j, "ram_mb");
    if (j.contains("screen")) {
        std::string s = json_utils::get_string(j, "screen");
        uint32_t w, h;
        if (parse_screen(s, w, h)) {
            screen_width = w;
            screen_height = h;
        }
    }
    if (j.contains("audio")) audio_enabled = json_utils::get_bool(j, "audio");
    if (j.contains("rom")) {
        rom_path = json_utils::get_string(j, "rom");
        if (!rom_path.empty() && rom_path[0] != '/' && !storage_dir.empty())
            rom_path = storage_dir + "/roms/" + rom_path;
    }
    if (j.contains("disks")) {
        disk_paths = json_utils::get_string_array(j, "disks");
        for (auto& p : disk_paths) {
            if (!p.empty() && p[0] != '/' && !storage_dir.empty())
                p = storage_dir + "/images/" + p;
        }
    }
    if (j.contains("cdroms")) {
        cdrom_paths = json_utils::get_string_array(j, "cdroms");
        for (auto& p : cdrom_paths) {
            if (!p.empty() && p[0] != '/' && !storage_dir.empty())
                p = storage_dir + "/images/" + p;
        }
    }
    if (j.contains("floppies")) {
        floppy_paths = json_utils::get_string_array(j, "floppies");
        for (auto& p : floppy_paths) {
            if (!p.empty() && p[0] != '/' && !storage_dir.empty())
                p = storage_dir + "/images/" + p;
        }
    }
    if (j.contains("extfs")) extfs = json_utils::get_string(j, "extfs");
    if (j.contains("bootdrive")) bootdrive = json_utils::get_int(j, "bootdrive");
    if (j.contains("bootdriver")) bootdriver = json_utils::get_int(j, "bootdriver");
    if (j.contains("codec")) codec = json_utils::get_string(j, "codec");
    if (j.contains("mousemode")) mousemode = json_utils::get_string(j, "mousemode");
    if (j.contains("http_port")) http_port = json_utils::get_int(j, "http_port");
    if (j.contains("signaling_port")) signaling_port = json_utils::get_int(j, "signaling_port");
    if (j.contains("client_dir")) client_dir = json_utils::get_string(j, "client_dir");
    if (j.contains("storage_dir")) storage_dir = json_utils::get_string(j, "storage_dir");
    if (j.contains("log_level")) log_level = json_utils::get_int(j, "log_level");
    if (j.contains("nocdrom")) nocdrom = json_utils::get_bool(j, "nocdrom");
    if (j.contains("nosound")) nosound = json_utils::get_bool(j, "nosound");
    if (j.contains("zappram")) zappram = json_utils::get_bool(j, "zappram");
    if (j.contains("frameskip")) frameskip = json_utils::get_int(j, "frameskip");
    if (j.contains("yearofs")) yearofs = json_utils::get_int(j, "yearofs");
    if (j.contains("dayofs")) dayofs = json_utils::get_int(j, "dayofs");
    if (j.contains("udptunnel")) udptunnel = json_utils::get_bool(j, "udptunnel");
    if (j.contains("udpport")) udpport = json_utils::get_int(j, "udpport");
    if (j.contains("debug_connection")) debug_connection = json_utils::get_bool(j, "debug_connection");
    if (j.contains("debug_mode_switch")) debug_mode_switch = json_utils::get_bool(j, "debug_mode_switch");
    if (j.contains("debug_perf")) debug_perf = json_utils::get_bool(j, "debug_perf");

    // M68K sub-struct
    if (j.contains("m68k")) {
        auto& m = j["m68k"];
        if (m.contains("cpu_type")) m68k.cpu_type = json_utils::get_int(m, "cpu_type");
        if (m.contains("fpu")) m68k.fpu = json_utils::get_bool(m, "fpu");
        if (m.contains("modelid")) m68k.modelid = json_utils::get_int(m, "modelid");
        if (m.contains("jit")) m68k.jit = json_utils::get_bool(m, "jit");
        if (m.contains("idlewait")) m68k.idlewait = json_utils::get_bool(m, "idlewait");
        if (m.contains("ignoresegv")) m68k.ignoresegv = json_utils::get_bool(m, "ignoresegv");
        if (m.contains("jitfpu")) m68k.jitfpu = json_utils::get_bool(m, "jitfpu");
        if (m.contains("jitdebug")) m68k.jitdebug = json_utils::get_bool(m, "jitdebug");
        if (m.contains("jitcachesize")) m68k.jitcachesize = json_utils::get_int(m, "jitcachesize");
        if (m.contains("jitlazyflush")) m68k.jitlazyflush = json_utils::get_bool(m, "jitlazyflush");
        if (m.contains("jitinline")) m68k.jitinline = json_utils::get_bool(m, "jitinline");
        if (m.contains("jitblacklist")) m68k.jitblacklist = json_utils::get_string(m, "jitblacklist");
        if (m.contains("swap_opt_cmd")) m68k.swap_opt_cmd = json_utils::get_bool(m, "swap_opt_cmd");
        if (m.contains("keyboardtype")) m68k.keyboardtype = json_utils::get_int(m, "keyboardtype");
    }

    // PPC sub-struct
    if (j.contains("ppc")) {
        auto& p = j["ppc"];
        if (p.contains("cpu_type")) ppc.cpu_type = json_utils::get_int(p, "cpu_type");
        if (p.contains("fpu")) ppc.fpu = json_utils::get_bool(p, "fpu");
        if (p.contains("modelid")) ppc.modelid = json_utils::get_int(p, "modelid");
        if (p.contains("jit")) ppc.jit = json_utils::get_bool(p, "jit");
        if (p.contains("jit68k")) ppc.jit68k = json_utils::get_bool(p, "jit68k");
        if (p.contains("idlewait")) ppc.idlewait = json_utils::get_bool(p, "idlewait");
        if (p.contains("ignoresegv")) ppc.ignoresegv = json_utils::get_bool(p, "ignoresegv");
        if (p.contains("ignoreillegal")) ppc.ignoreillegal = json_utils::get_bool(p, "ignoreillegal");
        if (p.contains("keyboardtype")) ppc.keyboardtype = json_utils::get_int(p, "keyboardtype");
    }
}

/*
 * Save config to JSON file
 */
bool EmulatorConfig::save() const {
    if (config_path.empty()) {
        fprintf(stderr, "[Config] No config path set, cannot save\n");
        return false;
    }

    try {
        ensure_parent_dir(config_path);

        std::ofstream file(config_path);
        if (!file) {
            fprintf(stderr, "[Config] Failed to open %s for writing\n", config_path.c_str());
            return false;
        }

        file << to_json().dump(2);
        file.close();
        fprintf(stderr, "[Config] Saved to %s\n", config_path.c_str());
        return true;
    } catch (const std::exception& e) {
        fprintf(stderr, "[Config] Failed to save: %s\n", e.what());
        return false;
    }
}

/*
 * Load from JSON config file (new flat format)
 */
static void load_from_json(EmulatorConfig& config, const char* path) {
    if (!path) return;

    std::string expanded = expand_home(path);
    if (!file_exists(expanded.c_str())) {
        fprintf(stderr, "[Config] No config file at %s, using defaults\n", expanded.c_str());
        return;
    }

    fprintf(stderr, "[Config] Loading: %s\n", expanded.c_str());
    config.config_path = expanded;

    try {
        auto j = json_utils::parse_file(expanded);
        config.merge_json(j);

    } catch (const std::exception& e) {
        fprintf(stderr, "[Config] JSON parse error: %s\n", e.what());
    }
}

// Environment variable snapshot (read once at startup)
struct EnvVars {
    const char* cpu_backend;
    const char* timeout;
    const char* screenshots;
    const char* log_level;
};

static EnvVars read_env_vars() {
    return {
        getenv("CPU_BACKEND"),
        getenv("EMULATOR_TIMEOUT"),
        getenv("MACEMU_SCREENSHOTS"),
        getenv("MACEMU_LOG_LEVEL"),
    };
}

/*
 * Apply CLI argument overrides
 */
static const char* apply_cli_overrides(EmulatorConfig& config, int& argc, char** argv,
                                       const EnvVars& env) {
    const char* rom_path = nullptr;
    bool backend_set_by_cli = false;
    bool timeout_set_by_cli = false;
    bool screenshots_set_by_cli = false;
    bool log_level_set_by_cli = false;

    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;

        // --help / -h
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [options] [rom-path]\n\n", argv[0]);
            printf("Options:\n");
            printf("  --rom PATH            ROM file path (alternative to positional arg)\n");
            printf("  --disk PATH           Disk image path (repeatable)\n");
            printf("  --cdrom PATH          CDROM image path (repeatable)\n");
            printf("  --ram MB              RAM size in megabytes (default: 32)\n");
            printf("  --backend NAME        CPU backend: uae, unicorn, dualcpu (default: uae)\n");
            printf("  --arch ARCH           CPU architecture: m68k, ppc (default: m68k)\n");
            printf("  --screen WxH          Display resolution (default: 640x480)\n");
            printf("  --port N              HTTP server port (default: 8000)\n");
            printf("  --signaling-port N    WebRTC signaling port (default: 8090)\n");
            printf("  --storage-dir PATH    Storage directory for ROMs/images (default: ~/storage)\n");
            printf("  --timeout N           Auto-exit after N seconds\n");
            printf("  --no-webserver        Headless mode (no HTTP/WebRTC)\n");
            printf("  --screenshots         Dump PPM screenshots to /tmp\n");
            printf("  --config PATH         JSON config file\n");
            printf("  --log-level N         Log level 0-3\n");
            printf("  --debug-connection    Debug WebRTC connections\n");
            printf("  --debug-mode-switch   Debug video mode switches\n");
            printf("  --debug-perf          Debug performance\n");
            printf("  -h, --help            Show this help message\n");
            printf("\nEnvironment variables:\n");
            printf("  CPU_BACKEND           CPU backend override\n");
            printf("  EMULATOR_TIMEOUT      Auto-exit after N seconds\n");
            printf("  MACEMU_LOG_LEVEL      Log level 0-3\n");
            printf("  MACEMU_SCREENSHOTS    Enable screenshot dumping\n");
            printf("  MACEMU_ROM            Default ROM path\n");
            exit(0);
        }

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

        // --storage-dir <path>
        if (strcmp(argv[i], "--storage-dir") == 0 && i+1 < argc) {
            config.storage_dir = argv[i+1];
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
            config.enable_webserver = false;
            argv[i] = nullptr; continue;
        }

        // --arch m68k|ppc
        if (strcmp(argv[i], "--arch") == 0 && i+1 < argc) {
            if (strcmp(argv[i+1], "ppc") == 0) config.architecture = Architecture::PPC;
            else config.architecture = Architecture::M68K;
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --log-level <n>
        if (strcmp(argv[i], "--log-level") == 0 && i+1 < argc) {
            config.log_level = atoi(argv[i+1]);
            log_level_set_by_cli = true;
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

    // Apply environment variables (lowest priority — only if not set by CLI)
    if (!backend_set_by_cli && env.cpu_backend) {
        if (strcmp(env.cpu_backend, "unicorn") == 0) config.cpu_backend = CPUBackend::Unicorn;
        else if (strcmp(env.cpu_backend, "dualcpu") == 0) config.cpu_backend = CPUBackend::DualCPU;
        else config.cpu_backend = CPUBackend::UAE;
    }

    if (!timeout_set_by_cli && config.timeout_seconds == 0 && env.timeout) {
        config.timeout_seconds = atoi(env.timeout);
    }

    if (!screenshots_set_by_cli && !config.screenshots && env.screenshots) {
        config.screenshots = true;
    }

    if (!log_level_set_by_cli && config.log_level == 0 && env.log_level) {
        config.log_level = atoi(env.log_level);
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

    // 2. Read env vars upfront (single snapshot)
    EnvVars env = read_env_vars();

    // 3. Load JSON config (sets values above defaults)
    load_from_json(config, final_path);

    // Store config path for save()
    if (final_path && config.config_path.empty()) {
        config.config_path = expand_home(final_path);
    }

    // 4. Apply CLI overrides + env vars (highest priority)
    const char* rom_from_cli = apply_cli_overrides(config, argc, argv, env);

    // 5. Expand ~ in storage_dir (must happen before ROM/disk resolution)
    if (config.storage_dir.empty()) {
        config.storage_dir = "~/storage";
        fprintf(stderr, "[Config] storage_dir not set, defaulting to ~/storage\n");
    }
    config.storage_dir = expand_home(config.storage_dir);

    // 6. Resolve ROM path
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

    // 7. Resolve disk/cdrom paths
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
    resolve_paths(config.floppy_paths);

    // 8. Resolve client_dir relative to binary location if it's a relative path
    if (!config.client_dir.empty() && config.client_dir[0] != '/') {
        char exe_path[4096];
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len > 0) {
            exe_path[len] = '\0';
            // Find last '/' to get directory
            char* last_slash = strrchr(exe_path, '/');
            if (last_slash) {
                *last_slash = '\0';  // exe_path is now the directory
                std::string resolved = std::string(exe_path) + "/../" + config.client_dir;
                // Check if resolved path exists, fall back to CWD-relative if not
                struct stat st;
                if (stat(resolved.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                    config.client_dir = resolved;
                }
            }
        }
    }

    return config;
}

/*
 * Print configuration summary
 */
void print_config(const EmulatorConfig& config) {
    fprintf(stderr, "[Config] Architecture: %s\n", config.architecture_string());
    fprintf(stderr, "[Config] RAM: %u MB\n", config.ram_mb);
    fprintf(stderr, "[Config] CPU: 680%d0, FPU: %s, Backend: %s\n",
            config.m68k.cpu_type, config.m68k.fpu ? "yes" : "no",
            config.cpu_backend_string());
    fprintf(stderr, "[Config] ROM: %s\n",
            config.rom_path.empty() ? "(none)" : config.rom_path.c_str());
    fprintf(stderr, "[Config] Screen: %ux%u\n", config.screen_width, config.screen_height);
    fprintf(stderr, "[Config] Codec: %s, Mouse: %s\n", config.codec.c_str(), config.mousemode.c_str());
    for (const auto& d : config.disk_paths)
        fprintf(stderr, "[Config] Disk: %s\n", d.c_str());
    if (config.enable_webserver)
        fprintf(stderr, "[Config] WebRTC: port %d, signaling %d\n",
                config.http_port, config.signaling_port);
    else
        fprintf(stderr, "[Config] WebRTC: disabled\n");
    if (config.timeout_seconds > 0)
        fprintf(stderr, "[Config] Timeout: %d seconds\n", config.timeout_seconds);
    if (config.screenshots)
        fprintf(stderr, "[Config] Screenshots: enabled\n");
    if (config.log_level > 0)
        fprintf(stderr, "[Config] Log level: %d\n", config.log_level);
}

}  // namespace config
