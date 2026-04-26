/*
 *  network_info.cpp - Write a consolidated network/bridge status file
 *                     into the ExtFS share so the guest (and host user)
 *                     can discover DHCP, MITM, and build-info in one spot.
 *
 *  Creates <extfs>/MacPhoenix/ subfolder and writes:
 *    NetworkInfo.txt      - Human-readable summary
 *    MitmCA.crt / .cer    - Root CA for HTTPS MITM (when --mitm-tls)
 *
 *  The legacy BridgeAgent heartbeat/command files continue to live at the
 *  top of the ExtFS root because their paths are hardcoded in the
 *  pre-built BridgeAgent.bin; moving them would require a Retro68 rebuild.
 *  NetworkInfo.txt documents where they are.
 */

#include "network_info.h"
#include "../config/emulator_config.h"

#include <cstdio>
#include <cstdlib>
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

// Copy a file. Silently no-op if src missing so we don't fail hard when
// MITM was never enabled (and thus no CA was generated).
bool copy_file(const std::string &src, const std::string &dst)
{
    std::ifstream in(src, std::ios::binary);
    if (!in) return false;
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << in.rdbuf();
    return true;
}

// Write a 32-byte Finder Info sidecar so Finder picks the right app on
// double-click. ExtFS looks at <dir>/.finf/<name>.
void write_finder_info(const std::string &dir, const std::string &name,
                       const char type[4], const char creator[4])
{
    ensure_dir(dir + "/.finf");
    std::string path = dir + "/.finf/" + name;
    unsigned char buf[32] = {0};
    std::memcpy(buf, type, 4);
    std::memcpy(buf + 4, creator, 4);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<char *>(buf), sizeof(buf));
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

    // 1) Clean up files we used to drop at the root (earlier iteration).
    for (const char *stale : {"MitmCA.crt", "MitmCA.cer"}) {
        std::string p = extfs_root + "/" + stale;
        unlink(p.c_str());
        std::string finf = extfs_root + "/.finf/" + stale;
        unlink(finf.c_str());
    }

    // 2) Copy the MITM CA into the subfolder when it exists.
    bool have_mitm_ca = false;
    if (cfg.mitm_tls) {
        std::string ca_dir = cfg.mitm_ca_dir.empty() ? ".mitm_ca" : cfg.mitm_ca_dir;
        std::string pem_src = ca_dir + "/mitm_ca.crt";
        std::string pem_dst = info_dir + "/MitmCA.crt";
        std::string cer_dst = info_dir + "/MitmCA.cer";
        if (copy_file(pem_src, pem_dst)) {
            have_mitm_ca = true;
            // Generate DER alongside the PEM using the openssl CLI —
            // avoids linking OpenSSL directly into mac-phoenix just for
            // this (it's already linked into net-bridge).
            std::string cmd = "openssl x509 -in '" + pem_dst +
                              "' -outform der -out '" + cer_dst + "' 2>/dev/null";
            if (system(cmd.c_str()) != 0) {
                // Non-fatal — .cer is just a convenience for MSIE.
                fprintf(stderr, "[NetworkInfo] Could not generate DER cert\n");
            }
            // Finder Info so Netscape recognises them on double-click.
            write_finder_info(info_dir, "MitmCA.cer", "cert", "MOSS");
            write_finder_info(info_dir, "MitmCA.crt", "TEXT", "MOSS");
        }
    }

    // 3) Write NetworkInfo.txt.
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
    out << "\r";

    out << "MITM TLS proxy:   "
        << (cfg.mitm_tls ? "active" : "disabled") << "\r";
    if (cfg.mitm_tls) {
        out << "  Intercept ports: "
            << (cfg.mitm_ports.empty() ? "443 (default)" : cfg.mitm_ports.c_str())
            << "\r"
            << "  Root CA dir:     "
            << (cfg.mitm_ca_dir.empty() ? ".mitm_ca" : cfg.mitm_ca_dir.c_str())
            << " (host side)\r"
            << "\r"
            << "  Install the CA in your browser to avoid cert warnings:\r"
            << "\r"
            << "    1. Navigate to  http://10.0.2.1/MitmCA.crt  (PEM — Netscape)\r"
            << "                 or http://10.0.2.1/MitmCA.cer  (DER — MSIE)\r"
            << "       The server sends Content-Type: application/x-x509-ca-cert\r"
            << "       which triggers the browser's \"Accept this CA\" dialog.\r"
            << "    2. Alternatively, this folder has copies you can open in\r"
            << "       the browser manually: Host:MacPhoenix:MitmCA.crt / .cer\r"
            << "\r"
            << "  Once installed, any https:// site is transparently proxied —\r"
            << "  the host talks modern TLS1.2/1.3 to the real server and\r"
            << "  re-encrypts to this guest using SSLv3/TLS1.0 + classic\r"
            << "  ciphers (RC4-MD5 / 3DES-SHA / AES128-SHA).\r"
            << "\r"
            << "  Supported browsers on 7.5.5 68k:\r"
            << "    - Netscape Navigator 3.04 Gold (US build, 128-bit)\r"
            << "    - Netscape Communicator 4.08 (needs CFM-68K Runtime Enabler)\r"
            << "    - Internet Explorer 4.5\r"
            << "    - iCab 2.9.9\r";
    }
    out << "\r"
        << "ExtFS mount:      "
        << extfs_root << "\r"
        << "                  visible as Host: inside the guest\r"
        << "\r";

    if (have_mitm_ca) {
        out << "Files in this folder:\r"
            << "  NetworkInfo.txt  - this file\r"
            << "  MitmCA.crt       - MITM root CA, PEM format (Netscape)\r"
            << "  MitmCA.cer       - MITM root CA, DER format (MSIE)\r";
    }

    fprintf(stderr, "[NetworkInfo] Wrote %s/NetworkInfo.txt (mitm=%d, bridge=%d)\n",
            info_dir.c_str(), cfg.mitm_tls, cfg.bridge_enabled);

    // Also write a machine-readable key=value file alongside. BridgeAgent
    // parses this at startup to render the values in its status window,
    // so the user can see the gateway/client IPs and CA URL without
    // leaving the guest. Kept deliberately simple — one key=value per
    // line, CR-terminated — so a 20-line C parser handles it.
    std::ofstream cfg_out(info_dir + "/netcfg.txt",
                          std::ios::binary | std::ios::trunc);
    if (cfg_out) {
        cfg_out << "gw=10.0.2.1\r"
                << "guest=10.0.2.15\r"
                << "mitm=" << (cfg.mitm_tls ? "1" : "0") << "\r";
        if (cfg.mitm_tls) {
            cfg_out << "ca_url=http://10.0.2.1/MitmCA.crt\r";
        }
        // Build stamps — BridgeAgent already has its own __DATE__ but the
        // host's useful too for troubleshooting mismatched versions.
        cfg_out << "host_build=" << __DATE__ << " " << __TIME__ << "\r";
        cfg_out << "nb_build=" << nb_ver << "\r";
    }
}

} // namespace core
