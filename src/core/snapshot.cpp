/*
 * snapshot.cpp - Guest memory snapshots (see snapshot.h).
 *
 * Layout of a snapshot directory:
 *   ram.bin    RAM from Mac address RAMBaseMac, RAMSize bytes
 *   rom.bin    ROM from Mac address ROMBaseMac, ROMSize bytes
 *   meta.json  addresses, sizes, registers; written last, so its
 *              presence means the snapshot is complete
 */

#include "snapshot.h"
#include "../common/include/sysdeps.h"
#include "../common/include/cpu_emulation.h"
#include "../common/include/m68k_registers.h"
#include "../config/emulator_config.h"
#include "boot_progress.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static std::atomic<bool> g_requested{false};

static std::string expand_home(const std::string& p)
{
    if (!p.empty() && p[0] == '~') {
        const char* home = getenv("HOME");
        return std::string(home ? home : "") + p.substr(1);
    }
    return p;
}

static std::string request_file(const std::string& storage_dir)
{
    return expand_home(storage_dir) + "/snapshots/.request";
}

std::string snapshot_prepare(const std::string& storage_dir, const std::string& name)
{
    std::string root = expand_home(storage_dir) + "/snapshots";
    std::string dir = root + "/" + name;
    mkdir(expand_home(storage_dir).c_str(), 0755);
    mkdir(root.c_str(), 0755);
    if (mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST)
        return "";
    remove((dir + "/meta.json").c_str());

    std::ofstream req(request_file(storage_dir), std::ios::trunc);
    req << dir;
    return req ? dir : "";
}

void snapshot_request()
{
    g_requested.store(true);
}

// write(2) rather than fwrite: Executor's guest RAM starts at host
// address 0, and stdio may memcpy from the buffer.
static bool write_file(const std::string& path, const uint8* data, uint32 size)
{
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    uintptr_t p = (uintptr_t)data;
    uint32 left = size;
    while (left > 0) {
        ssize_t n = write(fd, (const void*)p, left > (1u << 20) ? (1u << 20) : left);
        if (n <= 0) { close(fd); return false; }
        p += (uintptr_t)n;
        left -= (uint32)n;
    }
    return close(fd) == 0;
}

static std::string json_str(const std::string& v)
{
    std::string out;
    for (char c : v) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

static std::string hex32(uint32 v)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "\"0x%08x\"", v);
    return buf;
}

bool snapshot_pending()
{
    return g_requested.load();
}

void snapshot_service(const SnapshotMemory& mem)
{
    if (!g_requested.exchange(false))
        return;

    auto& cfg = config::EmulatorConfig::instance();
    std::ifstream req(request_file(cfg.storage_dir));
    std::string dir;
    std::getline(req, dir);
    if (dir.empty()) {
        fprintf(stderr, "[Snapshot] no request file\n");
        return;
    }

    bool ok = write_file(dir + "/ram.bin", mem.ram, mem.ram_size);
    if (mem.rom && mem.rom_size)
        ok = ok && write_file(dir + "/rom.bin", mem.rom, mem.rom_size);

    std::ostringstream j;
    j << "{\n"
      << "  \"format\": 1,\n"
      << "  \"ok\": " << (ok ? "true" : "false") << ",\n"
      << "  \"backend\": \"" << cfg.backend_string() << "\",\n"
      << "  \"time\": " << (long long)time(nullptr) << ",\n"
      << "  \"boot_phase\": \"" << boot_progress_phase() << "\",\n"
      << "  \"ram_base\": " << hex32(mem.ram_base) << ",\n"
      << "  \"ram_size\": " << mem.ram_size << ",\n"
      << "  \"rom_base\": " << hex32(mem.rom_base) << ",\n"
      << "  \"rom_size\": " << mem.rom_size << ",\n"
      << "  \"rom_path\": \"" << json_str(cfg.rom_path) << "\",\n"
      << "  \"disks\": [";
    for (size_t i = 0; i < cfg.disk_paths.size(); i++)
        j << (i ? ", " : "") << "\"" << json_str(expand_home(cfg.disk_paths[i])) << "\"";
    j << "],\n"
      << "  \"context\": \"" << json_str(mem.context) << "\"";
    if (mem.regs) {
        const M68kRegisters* r = mem.regs;
        j << ",\n  \"regs\": {";
        for (int i = 0; i < 8; i++) j << (i ? ", " : "") << "\"d" << i << "\": " << hex32(r->d[i]);
        for (int i = 0; i < 8; i++) j << ", \"a" << i << "\": " << hex32(r->a[i]);
        j << ", \"sr\": " << hex32(r->sr) << "}";
    }
    j << "\n}\n";

    std::string tmp = dir + "/meta.json.tmp";
    {
        std::ofstream m(tmp, std::ios::trunc);
        m << j.str();
    }
    rename(tmp.c_str(), (dir + "/meta.json").c_str());
    fprintf(stderr, "[Snapshot] %s (%s)\n", dir.c_str(), ok ? "ok" : "write failed");
}

void snapshot_service_from_irq(M68kRegisters* r)
{
    if (!snapshot_pending())
        return;
    SnapshotMemory mem;
    mem.ram = RAMBaseHost;
    mem.ram_base = RAMBaseMac;
    mem.ram_size = RAMSize;
    mem.rom = ROMBaseHost;
    mem.rom_base = ROMBaseMac;
    mem.rom_size = ROMSize;
    mem.context = "60Hz IRQ EmulOp (registers are the IRQ handler's, not the interrupted code's)";
    mem.regs = r;
    snapshot_service(mem);
}
