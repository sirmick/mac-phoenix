/*
 *  network_info.cpp - Write a consolidated network/bridge status file
 *                     into the ExtFS share so the guest (and host user)
 *                     can discover DHCP and build-info in one spot.
 *
 *  Creates <extfs>/MacPhoenix/ subfolder and writes:
 *    NetworkInfo.txt      - Human-readable summary
 *
 *  The legacy BridgeAgent heartbeat/command files continue to live at the
 *  top of the ExtFS root because their paths are hardcoded in the
 *  pre-built BridgeAgent.bin; moving them would require a Retro68 rebuild.
 *  NetworkInfo.txt documents where they are.
 */

#include "network_info.h"
#include "../config/emulator_config.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

void ensure_dir(const std::string &path)
{
    struct stat st;
    if (stat(path.c_str(), &st) == 0) return;
    mkdir(path.c_str(), 0755);
}

// Pipe the output of `net-bridge --version` into a string. Safe to call
// even when the bridge isn't running — we just exec it briefly.
std::string query_net_bridge_version()
{
    char pathbuf[4096];
    ssize_t n = readlink("/proc/self/exe", pathbuf, sizeof(pathbuf) - 1);
    if (n <= 0) return "unknown";
    pathbuf[n] = 0;
    std::string exe = pathbuf;
    auto slash = exe.rfind('/');
    if (slash == std::string::npos) return "unknown";
    std::string nb = exe.substr(0, slash) + "/net-bridge";
    std::string cmd = nb + " --version 2>/dev/null";
    FILE *fp = popen(cmd.c_str(), "r");
    if (!fp) return "unknown";
    char line[256] = {0};
    char *got = fgets(line, sizeof(line), fp);
    pclose(fp);
    if (!got) return "unknown";
    // Trim trailing newline.
    size_t len = std::strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = 0;
    }
    return line;
}

} // namespace

namespace core {

void write_network_info(const config::EmulatorConfig &cfg)
{
    if (cfg.extfs_paths.empty()) return;
    const std::string &extfs_root = cfg.extfs_paths[0];
    const std::string info_dir = extfs_root + "/MacPhoenix";
    ensure_dir(info_dir);

    // Clean up files we used to drop at the root (earlier MITM iteration).
    for (const char *stale : {"MitmCA.crt", "MitmCA.cer"}) {
        std::string p = extfs_root + "/" + stale;
        unlink(p.c_str());
        std::string finf = extfs_root + "/.finf/" + stale;
        unlink(finf.c_str());
    }
    for (const char *stale : {"MitmCA.crt", "MitmCA.cer"}) {
        std::string p = info_dir + "/" + stale;
        unlink(p.c_str());
        std::string finf = info_dir + "/.finf/" + stale;
        unlink(finf.c_str());
    }

    std::ofstream out(info_dir + "/NetworkInfo.txt",
                      std::ios::binary | std::ios::trunc);
    if (!out) return;

    std::string nb_ver = query_net_bridge_version();
    const char *net_mode =
        cfg.network == config::NetworkMode::Socket ? "socket (net-bridge)"
                                                   : "none";

    out << "MacPhoenix Network Info\r"
        << "=======================\r"
        << "\r"
        << "MacPhoenix (host) build: " << __DATE__ << " " << __TIME__ << "\r"
        << "net-bridge:              " << nb_ver << "\r"
        << "\r"
        << "Network driver:   " << net_mode << "\r";

    if (cfg.network == config::NetworkMode::Socket) {
        out << "Host gateway:     10.0.2.1\r"
            << "Guest DHCP lease: 10.0.2.15\r"
            << "DNS / routing:    via gateway (net-bridge)\r"
            << "\r";
    }

    out << "BridgeAgent:      "
        << (cfg.bridge_enabled ? "active" : "disabled") << "\r";
    if (cfg.bridge_enabled) {
        out << "  Heartbeat file: Host:MacPhoenix:bridge_heartbeat\r"
            << "  Command file:   Host:MacPhoenix:_bridge_cmd\r"
            << "  Result file:    Host:MacPhoenix:_bridge_result\r"
            << "  Clipboard:      Host:MacPhoenix:_bridge_clipboard\r";
    }
    out << "\r"
        << "ExtFS mount:      "
        << extfs_root << "\r"
        << "                  visible as Host: inside the guest\r"
        << "\r";

    fprintf(stderr, "[NetworkInfo] Wrote %s/NetworkInfo.txt (bridge=%d)\n",
            info_dir.c_str(), cfg.bridge_enabled);

    // Also write a machine-readable key=value file alongside. BridgeAgent
    // parses this at startup to render the values in its status window,
    // so the user can see the gateway/client IPs without leaving the
    // guest. Kept deliberately simple — one key=value per line,
    // CR-terminated — so a 20-line C parser handles it.
    std::ofstream cfg_out(info_dir + "/netcfg.txt",
                          std::ios::binary | std::ios::trunc);
    if (cfg_out) {
        cfg_out << "gw=10.0.2.1\r"
                << "guest=10.0.2.15\r";
        // Build stamps — BridgeAgent already has its own __DATE__ but the
        // host's useful too for troubleshooting mismatched versions.
        cfg_out << "host_build=" << __DATE__ << " " << __TIME__ << "\r";
        cfg_out << "nb_build=" << nb_ver << "\r";
    }
}

} // namespace core
