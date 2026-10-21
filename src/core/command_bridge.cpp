/*
 * command_bridge.cpp - Host-side command dispatcher for controlling Mac OS
 *
 * Read commands (app name, window list, memory) peek Mac memory directly
 * from the 60Hz IRQ — safe because no Toolbox calls are needed.
 *
 * Action commands (launch, quit, script, copy, ...) are handled by
 * BridgeAgent, unpacked here into the bridge dir on the Host volume and
 * launched from there once the Finder is up (a Time Manager task and a
 * Notification Manager request, text_feed.cpp); the boot disks aren't
 * written.
 *
 * They go to it over the bridge trap $ABE5 (guest_bridge.h,
 * docs/CommandBridge.md).
 */

#include "command_bridge.h"
#include "../common/include/sysdeps.h"
#include "../common/include/cpu_emulation.h"
#include "../common/include/m68k_registers.h"
#include "../common/include/platform.h"
#include "../webserver/file_scanner.h"  // storage::json_escape
#include "boot_progress.h"
#include "../config/emulator_config.h"
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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
                    for (int i = 0; i < tlen; i++)
                        title += static_cast<char>(ReadMacInt8(title_ptr + 1 + i));
                }
            }

            int16_t top = static_cast<int16_t>(ReadMacInt16(wp + 16));
            int16_t left = static_cast<int16_t>(ReadMacInt16(wp + 18));
            int16_t bottom = static_cast<int16_t>(ReadMacInt16(wp + 20));
            int16_t right = static_cast<int16_t>(ReadMacInt16(wp + 22));
            bool visible = ReadMacInt8(wp + 110) != 0;

            json += "{\"title\":\"" + storage::json_escape(title) + "\""
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
// Logs the bridge directory so it's visible in startup output. It holds
// what the guest reads at startup (netcfg.txt, NetworkInfo.txt).
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

std::string command_bridge_agent_bin() {
    return resolve_repo_relative("BridgeAgent/BridgeAgent.bin");
}

std::string command_bridge_guest_dir() {
    auto& cfg = config::EmulatorConfig::instance();
    const auto& roots = cfg.extfs_paths;
    if (!cfg.bridge_enabled || cfg.bridge_dir.empty() || roots.empty()) return "";
    const std::string& root = roots[0];
    if (cfg.bridge_dir.size() <= root.size() + 1 ||
        cfg.bridge_dir.compare(0, root.size(), root) != 0) return "";
    std::string tail = cfg.bridge_dir.substr(root.size() + 1);
    /* Replace '/' with ':' for Mac path syntax. */
    for (char& c : tail) if (c == '/') c = ':';
    return "Host:" + tail;
}

// BridgeAgent.bin (MacBinary) unpacked into the bridge dir as the Host
// volume's ExtFS keeps a Mac file: data fork <dir>/BridgeAgent, resource
// fork .rsrc/BridgeAgent, Finder info .finf/BridgeAgent (FInfo + FXInfo).
// The ROM machines launch it from there (bridge_launcher_install), so the
// boot disks are never written. Executor has its own (executor_systems.cpp).
static void unpack_bridge_agent() {
    auto& cfg = config::EmulatorConfig::instance();
    if (cfg.backend == config::Backend::EXECUTOR || cfg.bridge_dir.empty()) return;
    std::string bin = command_bridge_agent_bin();
    FILE* f = bin.empty() ? nullptr : fopen(bin.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "[Bridge] BridgeAgent.bin not found — no agent\n");
        return;
    }
    std::string blob;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) blob.append(buf, n);
    fclose(f);
    auto u8 = [&](size_t at) { return (uint8_t)blob[at]; };
    auto u32 = [&](size_t at) {
        return (uint32_t)u8(at) << 24 | (uint32_t)u8(at + 1) << 16 | (uint32_t)u8(at + 2) << 8 | u8(at + 3);
    };
    if (blob.size() < 128 || u8(0) != 0 || u8(74) != 0) {
        fprintf(stderr, "[Bridge] %s: not MacBinary\n", bin.c_str());
        return;
    }
    size_t dlen = u32(83), rlen = u32(87);
    size_t rsrc_at = 128 + ((dlen + 127) & ~(size_t)127);
    if (rsrc_at + rlen > blob.size()) {
        fprintf(stderr, "[Bridge] %s: truncated\n", bin.c_str());
        return;
    }
    std::string finf(32, '\0');
    finf.replace(0, 8, blob, 65, 8);                       // type, creator
    finf[8] = blob[73];                                     // Finder flags, high byte
    finf[9] = blob[101];                                    // low byte (MacBinary II)
    finf.replace(10, 6, blob, 75, 6);                       // location, folder
    struct Part { const char* sub; std::string bytes; } parts[] = {
        {"", blob.substr(128, dlen)}, {"/.rsrc", blob.substr(rsrc_at, rlen)}, {"/.finf", finf},
    };
    for (const auto& part : parts) {
        std::string dir = cfg.bridge_dir + part.sub;
        mkdir(dir.c_str(), 0755);
        std::string path = dir + "/BridgeAgent";
        FILE* o = fopen(path.c_str(), "wb");
        if (!o || fwrite(part.bytes.data(), 1, part.bytes.size(), o) != part.bytes.size()) {
            fprintf(stderr, "[Bridge] writing %s failed\n", path.c_str());
            if (o) fclose(o);
            return;
        }
        fclose(o);
    }
    fprintf(stderr, "[Bridge] BridgeAgent unpacked into %s\n", cfg.bridge_dir.c_str());
}

void command_bridge_init() {
    auto& cfg = config::EmulatorConfig::instance();
    if (!cfg.bridge_enabled) return;
    fprintf(stderr, "[Bridge] Enabled (dir=%s)\n",
            cfg.bridge_dir.empty() ? "<none>" : cfg.bridge_dir.c_str());
    unpack_bridge_agent();
}

// ============================================================================
// IRQ entry point — called from 60Hz timer (m68k and PPC)
//
// Advances the boot_phase to "Finder" on PPC (where the WindowList heuristic
// doesn't work). BridgeAgent is launched from the Host volume
// (text_feed.cpp), not from here.
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
