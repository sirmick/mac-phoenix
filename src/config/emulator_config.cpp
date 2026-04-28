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
#include <climits>
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
    try {
        w = static_cast<uint32_t>(std::stoi(s.substr(0, x)));
        h = static_cast<uint32_t>(std::stoi(s.substr(x + 1)));
    } catch (const std::exception&) {
        return false;
    }
    return w > 0 && h > 0;
}

// Helper: Ensure parent directory exists
static void ensure_parent_dir(const std::string& path) {
    size_t slash = path.rfind('/');
    if (slash == std::string::npos) return;
    std::string dir = path.substr(0, slash);
    struct stat st;
    if (stat(dir.c_str(), &st) != 0) {
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

// Remove duplicate paths from a vector, preserving order. Compares by
// realpath() to catch storage_dir-prefixed vs. relative-then-resolved dups.
static void dedup_paths(std::vector<std::string>& paths) {
    std::vector<std::string> seen;
    std::vector<std::string> result;
    for (auto& p : paths) {
        char resolved[PATH_MAX];
        const char* canonical = realpath(p.c_str(), resolved);
        std::string key = canonical ? std::string(canonical) : p;
        bool dup = false;
        for (auto& s : seen) {
            if (s == key) { dup = true; break; }
        }
        if (!dup) {
            seen.push_back(key);
            result.push_back(p);
        }
    }
    if (result.size() != paths.size())
        paths = std::move(result);
}

// Parse a backend token. Returns true on success.
static bool parse_backend(const std::string& s, Backend& out) {
    if (s == "uae")          { out = Backend::UAE;         return true; }
    if (s == "unicorn-m68k") { out = Backend::UnicornM68K; return true; }
    if (s == "unicorn-ppc")  { out = Backend::UnicornPPC;  return true; }
    if (s == "kpx")          { out = Backend::KPX;         return true; }
    if (s == "dualcpu")      { out = Backend::DualCPU;     return true; }
    return false;
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

    // CPU
    j["backend"]  = backend_string();
    j["jit"]      = jit;
    j["jit68k"]   = jit68k;
    j["idlewait"] = idlewait;

    // UAE JIT internals
    j["jit_fpu"]         = jit_fpu;
    j["jit_debug"]       = jit_debug;
    j["jit_cache_size"]  = jit_cache_size;
    j["jit_lazy_flush"]  = jit_lazy_flush;
    j["jit_inline"]      = jit_inline;
    j["jit_blacklist"]   = jit_blacklist;

    // Memory & media
    j["ram_mb"]      = ram_mb;
    j["screen"]      = screen_string();
    j["audio"]       = audio_enabled;
    j["rom"]         = strip_roms(rom_path);
    j["disks"]       = strip_images(disk_paths);
    j["cdroms"]      = strip_images(cdrom_paths);
    j["extfs"]       = extfs_paths;
    j["bootdriver"]  = bootdriver;

    // Streaming
    j["codec"]     = codec;
    j["mousemode"] = mousemode;

    // Keyboard remap (round-tripped to JS client)
    {
        nlohmann::json kb;
        kb["ctrl"] = kb_ctrl;
        kb["alt"]  = kb_alt;
        kb["meta"] = kb_meta;
        kb["fn"]   = kb_fn;
        kb["release_on_blur"] = kb_release_on_blur;
        j["keyboard"] = kb;
    }

    // Web/server
    j["http_port"]   = http_port;
    j["client_dir"]  = client_dir;
    j["storage_dir"] = storage_dir;

    // System
    j["zappram"]                  = zappram;
    j["dismiss_shutdown_dialog"]  = dismiss_shutdown_dialog;
    j["bridge_enabled"]           = bridge_enabled;
    j["browser_enabled"]          = browser_enabled;

    // Network
    j["network"] = network_string();
    if (!network_if.empty()) j["network_if"] = network_if;
    j["mitm_tls"] = mitm_tls;
    if (!mitm_ports.empty())  j["mitm_ports"]  = mitm_ports;
    if (!mitm_ca_dir.empty()) j["mitm_ca_dir"] = mitm_ca_dir;

    // Logging
    j["log_level"]          = log_level;
    j["debug_connection"]   = debug_connection;
    j["debug_mode_switch"]  = debug_mode_switch;
    j["debug_perf"]         = debug_perf;
    j["debug_network"]      = debug_network;

    // Preserve saved presets from file_config_ (UI-managed)
    if (file_config_.contains("configs") && file_config_["configs"].is_object()) {
        j["configs"] = file_config_["configs"];
    }

    return j;
}

/*
 * Merge JSON into config (partial updates OK).
 */
void EmulatorConfig::merge_json(const nlohmann::json& j) {
    // ── CPU backend ──────────────────────────────────────────────
    if (j.contains("backend")) {
        std::string b = json_utils::get_string(j, "backend");
        if (!parse_backend(b, backend)) {
            fprintf(stderr, "[Config] Unknown backend '%s', defaulting to 'uae'\n", b.c_str());
            backend = Backend::UAE;
        }
    }

    // ── JIT toggles ──────────────────────────────────────────────
    if (j.contains("jit"))      jit      = json_utils::get_bool(j, "jit");
    if (j.contains("jit68k"))   jit68k   = json_utils::get_bool(j, "jit68k");
    if (j.contains("idlewait")) idlewait = json_utils::get_bool(j, "idlewait");

    // ── UAE JIT internals ───────────────────────────────────────
    if (j.contains("jit_fpu"))        jit_fpu        = json_utils::get_bool(j, "jit_fpu");
    if (j.contains("jit_debug"))      jit_debug      = json_utils::get_bool(j, "jit_debug");
    if (j.contains("jit_cache_size")) jit_cache_size = json_utils::get_int(j, "jit_cache_size");
    if (j.contains("jit_lazy_flush")) jit_lazy_flush = json_utils::get_bool(j, "jit_lazy_flush");
    if (j.contains("jit_inline"))     jit_inline     = json_utils::get_bool(j, "jit_inline");
    if (j.contains("jit_blacklist"))  jit_blacklist  = json_utils::get_string(j, "jit_blacklist");

    // ── Memory & media ──────────────────────────────────────────
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
    auto resolve_image = [&](std::string& p) {
        if (!p.empty() && p[0] != '/' && !storage_dir.empty())
            p = storage_dir + "/images/" + p;
    };
    if (j.contains("disks")) {
        disk_paths = json_utils::get_string_array(j, "disks");
        for (auto& p : disk_paths) resolve_image(p);
    }
    if (j.contains("cdroms")) {
        cdrom_paths = json_utils::get_string_array(j, "cdroms");
        for (auto& p : cdrom_paths) resolve_image(p);
    }
    if (j.contains("extfs")) {
        if (j["extfs"].is_array()) {
            extfs_paths = json_utils::get_string_array(j, "extfs");
        } else if (j["extfs"].is_string()) {
            std::string s = json_utils::get_string(j, "extfs");
            if (!s.empty()) extfs_paths = {s};
        }
    }
    if (j.contains("bootdriver")) bootdriver = json_utils::get_int(j, "bootdriver");

    // ── Streaming ───────────────────────────────────────────────
    if (j.contains("codec"))     codec = json_utils::get_string(j, "codec");
    if (j.contains("mousemode")) mousemode = json_utils::get_string(j, "mousemode");

    // ── Keyboard remap ──────────────────────────────────────────
    if (j.contains("keyboard") && j["keyboard"].is_object()) {
        const auto& kb = j["keyboard"];
        if (kb.contains("ctrl")) kb_ctrl = json_utils::get_string(kb, "ctrl");
        if (kb.contains("alt"))  kb_alt  = json_utils::get_string(kb, "alt");
        if (kb.contains("meta")) kb_meta = json_utils::get_string(kb, "meta");
        if (kb.contains("fn"))   kb_fn   = json_utils::get_string(kb, "fn");
        if (kb.contains("release_on_blur"))
            kb_release_on_blur = json_utils::get_bool(kb, "release_on_blur");
    }

    // ── Web/server ──────────────────────────────────────────────
    if (j.contains("http_port"))   http_port = json_utils::get_int(j, "http_port");
    if (j.contains("client_dir"))  client_dir = json_utils::get_string(j, "client_dir");
    if (j.contains("storage_dir")) storage_dir = json_utils::get_string(j, "storage_dir");

    // ── System ──────────────────────────────────────────────────
    if (j.contains("zappram")) zappram = json_utils::get_bool(j, "zappram");
    if (j.contains("dismiss_shutdown_dialog"))
        dismiss_shutdown_dialog = json_utils::get_bool(j, "dismiss_shutdown_dialog");
    if (j.contains("bridge_enabled"))
        bridge_enabled = json_utils::get_bool(j, "bridge_enabled");
    if (j.contains("browser_enabled"))
        browser_enabled = json_utils::get_bool(j, "browser_enabled");

    // ── Network ─────────────────────────────────────────────────
    if (j.contains("network")) {
        std::string n = json_utils::get_string(j, "network");
        if (n == "socket") network = NetworkMode::Socket;
        else network = NetworkMode::None;
    }
    if (j.contains("network_if")) network_if = json_utils::get_string(j, "network_if");
    if (j.contains("mitm_tls"))    mitm_tls    = json_utils::get_bool(j, "mitm_tls");
    if (j.contains("mitm_ports"))  mitm_ports  = json_utils::get_string(j, "mitm_ports");
    if (j.contains("mitm_ca_dir")) mitm_ca_dir = json_utils::get_string(j, "mitm_ca_dir");

    // ── Logging ─────────────────────────────────────────────────
    if (j.contains("log_level"))         log_level         = json_utils::get_int(j, "log_level");
    if (j.contains("debug_connection"))  debug_connection  = json_utils::get_bool(j, "debug_connection");
    if (j.contains("debug_mode_switch")) debug_mode_switch = json_utils::get_bool(j, "debug_mode_switch");
    if (j.contains("debug_perf"))        debug_perf        = json_utils::get_bool(j, "debug_perf");
    if (j.contains("debug_network"))     debug_network     = json_utils::get_bool(j, "debug_network");

    // Deduplicate path arrays (config + CLI may specify same path)
    dedup_paths(disk_paths);
    dedup_paths(cdrom_paths);
    dedup_paths(extfs_paths);
}

/*
 * Merge JSON into both runtime config AND file_config_ (for UI/API changes).
 * Only values merged here will be persisted on save().
 */
void EmulatorConfig::merge_ui_json(const nlohmann::json& j) {
    merge_json(j);
    for (auto& [key, value] : j.items()) {
        file_config_[key] = value;
    }
}

/*
 * Save config to JSON file.
 * Writes file_config_ (UI/file changes only), NOT the full runtime config.
 * CLI-only overrides are never persisted.
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

        file << file_config_.dump(2);
        file.close();
        fprintf(stderr, "[Config] Saved to %s\n", config_path.c_str());
        return true;
    } catch (const std::exception& e) {
        fprintf(stderr, "[Config] Failed to save: %s\n", e.what());
        return false;
    }
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
    config.config_path = expanded;

    try {
        auto j = json_utils::parse_file(expanded);
        config.file_config_ = j;  // preserve file contents for save()
        config.merge_json(j);
    } catch (const std::exception& e) {
        fprintf(stderr, "[Config] JSON parse error: %s\n", e.what());
    }
}

/*
 * Apply CLI argument overrides
 */
static const char* apply_cli_overrides(EmulatorConfig& config, int& argc, char** argv) {
    const char* rom_path = nullptr;

    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;

        // --help / -h
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [options] [rom-path]\n\n", argv[0]);
            printf("Machine:\n");
            printf("  --rom PATH                 ROM file (or positional arg)\n");
            printf("  --ram MB                   RAM size in megabytes (default: 64)\n");
            printf("  --screen WxH               Display resolution (default: 640x480)\n");
            printf("  --disk PATH                Disk image (repeatable)\n");
            printf("  --cdrom PATH               CD-ROM image (repeatable)\n");
            printf("  --extfs PATH               Shared folder (repeatable)\n");
            printf("  --bootdriver N             0=any, -62=CD-ROM (default: 0)\n");
            printf("  --storage-dir PATH         Default storage root (default: ~/storage)\n");
            printf("\nCPU:\n");
            printf("  --backend NAME             uae | unicorn-m68k | unicorn-ppc | kpx | dualcpu\n");
            printf("                             (default: uae)\n");
            printf("  --jit / --no-jit           Enable backend's primary JIT (uae, kpx)\n");
            printf("  --jit68k / --no-jit68k     Enable 68k-on-PPC DR JIT (kpx only, default: on)\n");
            printf("  --idlewait / --no-idlewait Pause CPU when guest idle (default: on)\n");
            printf("\nMedia:\n");
            printf("  --audio                    Enable audio (default: off)\n");
            printf("  --zap-pram                 Clear PRAM on startup\n");
            printf("  --dismiss-shutdown-dialog  Auto-dismiss improper-shutdown dialog\n");
            printf("\nNetworking:\n");
            printf("  --network MODE             none | socket[:PATH] (default: none)\n");
            printf("  --mitm-tls                 Enable MITM TLS proxy\n");
            printf("  --mitm-ports LIST          Comma-separated TCP ports (default: 443)\n");
            printf("  --mitm-ca-dir PATH         CA directory (default: .mitm_ca)\n");
            printf("\nAutomation:\n");
            printf("  --bridge                   Enable automation bridge\n");
            printf("  --browser                  Reserve BrowserShm region (MacBrowser spike)\n");
            printf("  --browser-url URL          Spawn Firefox on Xvfb and navigate to URL (implies --browser)\n");
            printf("  --headless-http            HTTP API only (no video/audio)\n");
            printf("\nServer:\n");
            printf("  --port N                   HTTP+WS port (default: 11000)\n");
            printf("  --no-webserver             Headless, no HTTP/WebRTC\n");
            printf("  --timeout N                Auto-exit after N seconds\n");
            printf("  --config PATH              JSON config file\n");
            printf("  --screenshots              Dump PPM frames to /tmp\n");
            printf("\nLogging:\n");
            printf("  --log-level N              0–3\n");
            printf("  --debug-connection\n");
            printf("  --debug-mode-switch\n");
            printf("  --debug-perf\n");
            printf("  --debug-network\n");
            printf("  -h, --help                 Show this help message\n");
            exit(0);
        }

        // --config <path> (consumed earlier)
        if (strcmp(argv[i], "--config") == 0 && i+1 < argc) {
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --rom <path>
        if (strcmp(argv[i], "--rom") == 0 && i+1 < argc) {
            config.rom_path = argv[i+1];
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --disk / --cdrom / --extfs (repeatable)
        if (strcmp(argv[i], "--disk") == 0 && i+1 < argc) {
            config.disk_paths.push_back(argv[i+1]);
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }
        if (strcmp(argv[i], "--cdrom") == 0 && i+1 < argc) {
            config.cdrom_paths.push_back(argv[i+1]);
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }
        if (strcmp(argv[i], "--extfs") == 0 && i+1 < argc) {
            config.extfs_paths.push_back(argv[i+1]);
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --ram <mb>
        if (strcmp(argv[i], "--ram") == 0 && i+1 < argc) {
            config.ram_mb = static_cast<uint32_t>(atoi(argv[i+1]));
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --bootdriver <refnum>
        if (strcmp(argv[i], "--bootdriver") == 0 && i+1 < argc) {
            config.bootdriver = atoi(argv[i+1]);
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --backend <name>
        if (strcmp(argv[i], "--backend") == 0 && i+1 < argc) {
            if (!parse_backend(argv[i+1], config.backend)) {
                // Tolerate legacy "unicorn" with no -m68k/-ppc suffix: choose by ROM path
                // heuristic later, but for now default to m68k variant.
                std::string b = argv[i+1];
                if (b == "unicorn") {
                    config.backend = Backend::UnicornM68K;
                    fprintf(stderr, "[Config] --backend unicorn is ambiguous; using unicorn-m68k. "
                                    "Use unicorn-m68k or unicorn-ppc explicitly.\n");
                } else {
                    fprintf(stderr, "[Config] Unknown backend '%s', defaulting to 'uae'\n", argv[i+1]);
                    config.backend = Backend::UAE;
                }
            }
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --jit / --no-jit
        if (strcmp(argv[i], "--jit") == 0) {
            config.jit = true; argv[i] = nullptr; continue;
        }
        if (strcmp(argv[i], "--no-jit") == 0) {
            config.jit = false; argv[i] = nullptr; continue;
        }

        // --jit68k / --no-jit68k (KPX only)
        if (strcmp(argv[i], "--jit68k") == 0) {
            config.jit68k = true; argv[i] = nullptr; continue;
        }
        if (strcmp(argv[i], "--no-jit68k") == 0) {
            config.jit68k = false; argv[i] = nullptr; continue;
        }

        // --idlewait / --no-idlewait
        if (strcmp(argv[i], "--idlewait") == 0) {
            config.idlewait = true; argv[i] = nullptr; continue;
        }
        if (strcmp(argv[i], "--no-idlewait") == 0) {
            config.idlewait = false; argv[i] = nullptr; continue;
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

        // --storage-dir <path>
        if (strcmp(argv[i], "--storage-dir") == 0 && i+1 < argc) {
            config.storage_dir = argv[i+1];
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --timeout <sec>
        if (strcmp(argv[i], "--timeout") == 0 && i+1 < argc) {
            config.timeout_seconds = atoi(argv[i+1]);
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --screenshots
        if (strcmp(argv[i], "--screenshots") == 0) {
            config.screenshots = true; argv[i] = nullptr; continue;
        }

        // --zap-pram
        if (strcmp(argv[i], "--zap-pram") == 0) {
            config.zappram = true; argv[i] = nullptr; continue;
        }

        // --dismiss-shutdown-dialog / --no-dismiss-shutdown-dialog
        if (strcmp(argv[i], "--dismiss-shutdown-dialog") == 0) {
            config.dismiss_shutdown_dialog = true; argv[i] = nullptr; continue;
        }
        if (strcmp(argv[i], "--no-dismiss-shutdown-dialog") == 0) {
            config.dismiss_shutdown_dialog = false; argv[i] = nullptr; continue;
        }

        // --no-webserver
        if (strcmp(argv[i], "--no-webserver") == 0) {
            config.enable_webserver = false; argv[i] = nullptr; continue;
        }

        // --audio
        if (strcmp(argv[i], "--audio") == 0) {
            config.audio_enabled = true; argv[i] = nullptr; continue;
        }

        // --headless-http
        if (strcmp(argv[i], "--headless-http") == 0) {
            config.enable_webserver = false;
            config.headless_http = true;
            config.bridge_enabled = true;
            argv[i] = nullptr; continue;
        }

        // --bridge
        if (strcmp(argv[i], "--bridge") == 0) {
            config.bridge_enabled = true; argv[i] = nullptr; continue;
        }

        // --browser
        if (strcmp(argv[i], "--browser") == 0) {
            config.browser_enabled = true; argv[i] = nullptr; continue;
        }

        // --browser-url URL  (implies --browser)
        if (strcmp(argv[i], "--browser-url") == 0 && i + 1 < argc) {
            config.browser_enabled = true;
            config.browser_initial_url = argv[i+1];
            argv[i] = argv[i+1] = nullptr;
            i++;
            continue;
        }

        // --ipc (IPC child mode for PPC subprocess)
        if (strcmp(argv[i], "--ipc") == 0) {
            config.ipc_mode = true;
            config.enable_webserver = false;
            argv[i] = nullptr; continue;
        }

        // --mitm-tls / --mitm-ports / --mitm-ca-dir
        if (strcmp(argv[i], "--mitm-tls") == 0) {
            config.mitm_tls = true; argv[i] = nullptr; continue;
        }
        if (strcmp(argv[i], "--mitm-ports") == 0 && i+1 < argc) {
            config.mitm_ports = argv[i+1];
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }
        if (strcmp(argv[i], "--mitm-ca-dir") == 0 && i+1 < argc) {
            config.mitm_ca_dir = argv[i+1];
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --network <mode>[:<path>]
        if (strcmp(argv[i], "--network") == 0 && i+1 < argc) {
            const char* arg = argv[i+1];
            const char* colon = strchr(arg, ':');
            std::string mode_str = colon ? std::string(arg, colon) : std::string(arg);
            if (mode_str == "socket") {
                config.network = NetworkMode::Socket;
                if (colon) config.network_if = colon + 1;
            } else {
                config.network = NetworkMode::None;
            }
            argv[i] = nullptr; argv[++i] = nullptr; continue;
        }

        // --log-level <n>
        if (strcmp(argv[i], "--log-level") == 0 && i+1 < argc) {
            config.log_level = atoi(argv[i+1]);
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
        if (strcmp(argv[i], "--debug-network") == 0) {
            config.debug_network = true; argv[i] = nullptr; continue;
        }

        // Positional: ROM path (last non-flag arg)
        if (argv[i][0] != '-') {
            rom_path = argv[i];
            argv[i] = nullptr; continue;
        }

        fprintf(stderr, "[Config] Unknown argument: %s\n", argv[i]);
    }

    // Validate flag combinations
    if (config.jit68k && config.backend != Backend::KPX) {
        // jit68k only meaningful for KPX; silently leave value but warn if explicitly set
    }
    if (config.jit && (config.backend == Backend::UnicornM68K || config.backend == Backend::UnicornPPC)) {
        fprintf(stderr, "[Config] --jit is a no-op for %s backend (no JIT available)\n",
                config.backend_string());
    }

    // --bridge: ExtFS mount for bridge file I/O. The guest BridgeAgent
    // looks for every file under Host:MacPhoenix:... so we root
    // bridge_dir at <extfs_path[0]>/MacPhoenix and ensure the directory
    // exists. One-shot cleanup of any legacy top-level bridge_* files
    // from the earlier scheme avoids duplicates living around.
    if (config.bridge_enabled) {
        std::string root;
        if (config.extfs_paths.empty()) {
            char tmp[] = "/tmp/macemu-bridge-XXXXXX";
            if (mkdtemp(tmp)) {
                root = tmp;
                config.extfs_paths.push_back(root);
                fprintf(stderr, "[Config] Bridge ExtFS: %s\n", tmp);
            }
        } else {
            root = config.extfs_paths[0];
        }
        if (!root.empty()) {
            config.bridge_dir = root + "/MacPhoenix";
            mkdir(config.bridge_dir.c_str(), 0755);

            // Remove stale top-level bridge files from the pre-subfolder layout.
            for (const char *stale : {
                "bridge_heartbeat", "bridge_loaded", "bridge_step",
                "_bridge_cmd", "_bridge_result", "_bridge_clipboard",
            }) {
                std::string path = root + "/" + stale;
                unlink(path.c_str());
                std::string finf = root + "/.finf/" + stale;
                unlink(finf.c_str());
            }
            fprintf(stderr, "[Config] Bridge dir: %s (under ExtFS)\n",
                    config.bridge_dir.c_str());
        }
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

    if (final_path && config.config_path.empty()) {
        config.config_path = expand_home(final_path);
    }

    // 3. Apply CLI overrides (highest priority)
    const char* rom_from_cli = apply_cli_overrides(config, argc, argv);

    // 4. Expand ~ in storage_dir (must happen before ROM/disk resolution)
    if (config.storage_dir.empty()) {
        config.storage_dir = "~/storage";
        fprintf(stderr, "[Config] storage_dir not set, defaulting to ~/storage\n");
    }
    config.storage_dir = expand_home(config.storage_dir);

    // 5. Resolve ROM path
    if (rom_from_cli) {
        config.rom_path = rom_from_cli;
    }
    if (!config.rom_path.empty()) {
        config.rom_path = expand_home(config.rom_path);
        if (config.rom_path[0] != '/' && !config.storage_dir.empty()) {
            config.rom_path = config.storage_dir + "/roms/" + config.rom_path;
        }
    }

    // 6. Resolve disk/cdrom paths
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

    // 7. Deduplicate (config file + CLI may specify same path)
    dedup_paths(config.disk_paths);
    dedup_paths(config.cdrom_paths);
    dedup_paths(config.extfs_paths);

    // 8. Resolve client_dir relative to binary location if it's a relative path.
    //    Searched in order: <exe>/../client (dev tree), <exe>/../share/mac-phoenix/client
    //    (FHS install — /usr/bin/mac-phoenix → /usr/share/mac-phoenix/client).
    if (!config.client_dir.empty() && config.client_dir[0] != '/') {
        char exe_path[4096];
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len > 0) {
            exe_path[len] = '\0';
            char* last_slash = strrchr(exe_path, '/');
            if (last_slash) {
                *last_slash = '\0';
                const std::string exe_dir = exe_path;
                const std::string candidates[] = {
                    exe_dir + "/../" + config.client_dir,
                    exe_dir + "/../share/mac-phoenix/client",
                };
                for (const auto& candidate : candidates) {
                    struct stat st;
                    if (stat(candidate.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                        config.client_dir = candidate;
                        break;
                    }
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
    fprintf(stderr, "[Config] Backend: %s (arch: %s)\n",
            config.backend_string(), config.arch_string());
    fprintf(stderr, "[Config] RAM: %u MB\n", config.ram_mb);
    if (config.backend == Backend::UAE || config.backend == Backend::KPX) {
        fprintf(stderr, "[Config] JIT: %s%s\n", config.jit ? "on" : "off",
                (config.backend == Backend::KPX && config.jit68k) ? " + 68k JIT" : "");
    }
    fprintf(stderr, "[Config] idlewait: %s\n", config.idlewait ? "on" : "off");
    fprintf(stderr, "[Config] ROM: %s\n",
            config.rom_path.empty() ? "(none)" : config.rom_path.c_str());
    fprintf(stderr, "[Config] Screen: %ux%u\n", config.screen_width, config.screen_height);
    fprintf(stderr, "[Config] Codec: %s, Mouse: %s\n", config.codec.c_str(), config.mousemode.c_str());
    if (config.network != NetworkMode::None) {
        fprintf(stderr, "[Config] Network: %s%s%s\n", config.network_string(),
                config.network_if.empty() ? "" : ":",
                config.network_if.empty() ? "" : config.network_if.c_str());
    }
    for (const auto& d : config.disk_paths)
        fprintf(stderr, "[Config] Disk: %s\n", d.c_str());
    for (const auto& c : config.cdrom_paths)
        fprintf(stderr, "[Config] CDROM: %s\n", c.c_str());
    if (config.enable_webserver)
        fprintf(stderr, "[Config] Web server: port %d (signaling WebSocket at /ws on same port)\n",
                config.http_port);
    else
        fprintf(stderr, "[Config] Web server: disabled\n");
    if (config.timeout_seconds > 0)
        fprintf(stderr, "[Config] Timeout: %d seconds\n", config.timeout_seconds);
    if (config.screenshots)
        fprintf(stderr, "[Config] Screenshots: enabled\n");
    if (config.log_level > 0)
        fprintf(stderr, "[Config] Log level: %d\n", config.log_level);
}

}  // namespace config
