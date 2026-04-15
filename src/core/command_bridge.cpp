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
#include "bridge_fs.h"
#include "../config/emulator_config.h"
#include <cstdio>
#include <cstring>

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
// IRQ entry point — called from 60Hz timer (m68k and PPC)
//
// Advances the boot_phase to "Finder" on PPC (where the WindowList heuristic
// doesn't work). BridgeFS init happens lazily the first time it's used.
// No guest-code injection — the bridge agent lives in System Folder:Startup
// Items and is launched by Finder.
// ============================================================================

static void ensure_bridge_fs() {
    if (!config::EmulatorConfig::instance().bridge_enabled) return;
    if (g_bridge_fs) return;
    g_bridge_fs = new BridgeFS();
    auto& cfg = config::EmulatorConfig::instance();
    if (!cfg.bridge_dir.empty())
        g_bridge_fs->set_bridge_dir(cfg.bridge_dir);
}

void command_bridge_drain_from_irq(M68kRegisters* r) {
    (void)r;
    ensure_bridge_fs();
}

void command_bridge_drain_from_irq_ppc(M68kRegisters* r) {
    (void)r;
    ensure_bridge_fs();
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
