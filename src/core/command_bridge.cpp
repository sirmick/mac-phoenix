/*
 * command_bridge.cpp - Host-side command dispatcher for controlling Mac OS
 *
 * Read commands (app name, window list, memory) peek Mac memory directly
 * from the 60Hz IRQ — safe because no Toolbox calls are needed.
 *
 * Action commands (launch, quit) are handled by a guest-side agent app
 * installed in System Folder:Startup Items. Finder launches it at desktop
 * time; the emulator does not inject anything.
 *
 * Communication is entirely file-based via the ExtFS "Host" volume:
 *   Host writes Host:_bridge_cmd  → agent reads, executes, deletes
 *   agent writes Host:_bridge_result → Host reads, returns to API caller
 */

#include "command_bridge.h"
#include "../common/include/sysdeps.h"
#include "../common/include/cpu_emulation.h"
#include "../common/include/m68k_registers.h"
#include "../common/include/platform.h"
#include "boot_progress.h"
#include "../config/emulator_config.h"
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// ============================================================================
// Read commands — peek Mac memory, no Toolbox calls needed
// ============================================================================

CommandResult command_bridge_read(CmdType type, uint32_t addr, uint32_t len) {
    CommandResult result;
    result.done = true;

    if (RAMSize == 0) {
        result.err = -1;
        return result;
    }

    switch (type) {

    case CmdType::GET_APP_NAME: {
        uint8_t namelen = ReadMacInt8(0x0910);
        if (namelen > 31) namelen = 31;
        char name[32];
        for (int i = 0; i < namelen; i++)
            name[i] = static_cast<char>(ReadMacInt8(0x0911 + i));
        name[namelen] = '\0';
        result.data = name;
        break;
    }

    case CmdType::GET_TICKS: {
        result.data = std::to_string(ReadMacInt32(0x016A));
        break;
    }

    case CmdType::GET_WINDOW_LIST: {
        std::string json = "[";
        uint32_t wp = ReadMacInt32(0x09D6);
        bool first = true;
        int limit = 50;

        while (wp && wp < RAMSize && limit-- > 0) {
            if (!first) json += ",";
            first = false;

            std::string title;
            uint32_t title_handle = ReadMacInt32(wp + 134);
            if (title_handle) {
                uint32_t title_ptr = ReadMacInt32(title_handle);
                if (title_ptr && title_ptr < RAMSize) {
                    uint8_t tlen = ReadMacInt8(title_ptr);
                    for (int i = 0; i < tlen; i++) {
                        char c = static_cast<char>(ReadMacInt8(title_ptr + 1 + i));
                        if (c == '"' || c == '\\') json += "\\";
                        title += c;
                    }
                }
            }

            int16_t top = static_cast<int16_t>(ReadMacInt16(wp + 16));
            int16_t left = static_cast<int16_t>(ReadMacInt16(wp + 18));
            int16_t bottom = static_cast<int16_t>(ReadMacInt16(wp + 20));
            int16_t right = static_cast<int16_t>(ReadMacInt16(wp + 22));
            bool visible = ReadMacInt8(wp + 110) != 0;

            json += "{\"title\":\"" + title + "\""
                 +  ",\"rect\":[" + std::to_string(left) + ","
                 +  std::to_string(top) + ","
                 +  std::to_string(right) + ","
                 +  std::to_string(bottom) + "]"
                 +  ",\"visible\":" + (visible ? "true" : "false") + "}";

            wp = ReadMacInt32(wp + 144);
        }
        json += "]";
        result.data = json;
        break;
    }

    case CmdType::READ_MEMORY: {
        uint32_t read_len = std::min(len, (uint32_t)1024);
        std::string hex;
        hex.reserve(read_len * 2);
        for (uint32_t i = 0; i < read_len; i++) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02x", ReadMacInt8(addr + i));
            hex += buf;
        }
        result.data = hex;
        break;
    }

    default:
        result.err = -1;
        result.data = "not a read command";
        break;
    }

    return result;
}

// ============================================================================
// One-shot init — called from main.cpp after EmulatorConfig is finalized.
// Logs the bridge directory so it's visible in startup output. The actual
// bridge files (_bridge_cmd, _bridge_result, bridge_heartbeat) live on disk
// in cfg.bridge_dir and are read/written by both parent and IPC child.
// ============================================================================

// Resolve a file relative to the executable's grandparent directory
// (build/mac-phoenix → repo root). Returns "" if the candidate doesn't exist.
static std::string resolve_repo_relative(const char* rel) {
    char exe_path[4096];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len <= 0) return "";
    exe_path[len] = '\0';
    char* last_slash = strrchr(exe_path, '/');
    if (!last_slash) return "";
    *last_slash = '\0';  // exe_path is now build/ (or /usr/bin)
    struct stat st;

    // 1. Dev tree: build/../<rel>
    std::string candidate = std::string(exe_path) + "/../" + rel;
    if (stat(candidate.c_str(), &st) == 0) return candidate;

    // 2. Installed: /usr/bin/mac-phoenix → /usr/share/mac-phoenix/<rel>
    candidate = "/usr/share/mac-phoenix/" + std::string(rel);
    if (stat(candidate.c_str(), &st) == 0) return candidate;

    return "";
}

// When the bridge is enabled, install BridgeAgent.bin into
// :System Folder:Startup Items: on every configured non-CDROM disk that has a
// System Folder. The script handles HFS / non-system-disk skipping.
// Run before the emulator opens the disks (i.e. from command_bridge_init).
static void provision_bridge_agent_disks() {
    auto& cfg = config::EmulatorConfig::instance();
    if (cfg.disk_paths.empty()) return;

    std::string script = resolve_repo_relative("provisioning/install_bridge_agent.sh");
    if (script.empty()) {
        fprintf(stderr, "[Bridge] provisioning script not found — "
                        "skipping BridgeAgent install\n");
        return;
    }

    std::string cmd = "'" + script + "'";
    for (const auto& p : cfg.disk_paths) {
        if (p.empty()) continue;
        cmd += " '" + p + "'";
    }

    fprintf(stderr, "[Bridge] Provisioning BridgeAgent across %zu configured "
                    "disk(s)...\n", cfg.disk_paths.size());
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        fprintf(stderr, "[Bridge] WARNING: provisioning script exited with "
                        "status %d — guest may not auto-launch BridgeAgent\n",
                        WEXITSTATUS(rc));
    }
}

void command_bridge_init() {
    auto& cfg = config::EmulatorConfig::instance();
    if (!cfg.bridge_enabled) return;
    fprintf(stderr, "[Bridge] Enabled (dir=%s)\n",
            cfg.bridge_dir.empty() ? "<none>" : cfg.bridge_dir.c_str());
    provision_bridge_agent_disks();
}

void command_bridge_start_watchdog(std::function<bool()> finder_reached, int grace_seconds) {
    auto& cfg = config::EmulatorConfig::instance();
    if (!cfg.bridge_enabled || cfg.bridge_dir.empty() || !finder_reached) return;

    std::string heartbeat_path = cfg.bridge_dir + "/bridge_heartbeat";
    std::thread([finder_reached = std::move(finder_reached), heartbeat_path, grace_seconds]() {
        // Wait up to ~5 minutes for Finder
        for (int i = 0; i < 300; i++) {
            if (finder_reached()) break;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (!finder_reached()) return;  // never booted; nothing to warn about

        std::this_thread::sleep_for(std::chrono::seconds(grace_seconds));

        struct stat st;
        if (stat(heartbeat_path.c_str(), &st) != 0) {
            fprintf(stderr,
                "[Bridge] WARNING: bridge enabled but no heartbeat at %s "
                "after %ds past Finder. Is BridgeAgent installed in "
                ":System Folder:Startup Items: of the boot disk?\n",
                heartbeat_path.c_str(), grace_seconds);
            return;
        }
        time_t age = time(nullptr) - st.st_mtime;
        if (age > grace_seconds) {
            fprintf(stderr,
                "[Bridge] WARNING: heartbeat at %s is stale (%lds old). "
                "BridgeAgent may have crashed or quit.\n",
                heartbeat_path.c_str(), (long)age);
        } else {
            fprintf(stderr, "[Bridge] BridgeAgent heartbeat detected (%lds old)\n",
                    (long)age);
        }
    }).detach();
}

// ============================================================================
// IRQ entry point — called from 60Hz timer (m68k and PPC)
//
// Advances the boot_phase to "Finder" on PPC (where the WindowList heuristic
// doesn't work). No guest-code injection — the bridge agent lives in
// System Folder:Startup Items and is launched by Finder.
// ============================================================================

void command_bridge_drain_from_irq(M68kRegisters* r) {
    (void)r;
}

void command_bridge_drain_from_irq_ppc(M68kRegisters* r) {
    (void)r;
    if (RAMSize == 0) return;

    // PPC has no WindowList population in our boot path, so the usual
    // window-heuristic never triggers PHASE_FINDER. Detect Finder via
    // CurApName and advance the phase manually. Only fires once.
    uint8_t namelen = ReadMacInt8(0x0910);
    bool finder_running = (namelen == 6
                           && ReadMacInt8(0x0911) == 'F'
                           && ReadMacInt8(0x0912) == 'i');
    if (!finder_running) return;

    static bool ppc_phase_set = false;
    if (ppc_phase_set || boot_progress_phase_reached("Finder")) return;

    extern void boot_progress_set_phase_finder(void);
    boot_progress_set_phase_finder();
    ppc_phase_set = true;
}
