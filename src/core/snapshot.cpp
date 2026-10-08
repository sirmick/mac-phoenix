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
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstring>
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

// ── A-trap trace ─────────────────────────────────────────────────────

extern void (*uae_atrap_hook)(uint16_t opcode, uint32_t pc, uint32_t sp, int intmask);
extern uint32_t uae_atrap_watch_pc;
extern void (*uae_atrap_return_hook)(uint32_t sp);

namespace {

struct AtrapSite {
    uint64_t count = 0;
    uint32_t seq = 0;            // first-seen order
    uint8_t irq = 0;             // interrupt mask at first hit (nonzero: interrupt time)
    char app[32] = {};           // CurApName at first hit
    uint8_t code[24] = {};       // code bytes from pc - 8
    uint8_t code_len = 0;
};

// Key: caller pc, trap word, and the trap active when it fired (0: none).
struct AtrapKey {
    uint32_t pc;
    uint16_t trap, parent;
    bool operator==(const AtrapKey& o) const { return pc == o.pc && trap == o.trap && parent == o.parent; }
};
struct AtrapKeyHash {
    size_t operator()(const AtrapKey& k) const {
        return std::hash<uint64_t>()(((uint64_t)k.pc << 32) | ((uint32_t)k.trap << 16) | k.parent);
    }
};
std::unordered_map<AtrapKey, AtrapSite, AtrapKeyHash> g_atrap_sites;
uint32_t g_atrap_seq = 0;

// Shadow stack of active traps. Each ends when the CPU reaches its return
// address (the interpreter loop watches the innermost one). As a backstop
// for returns we miss (non-local exits), an entry whose A7 at entry is at
// or below the current A7 has returned, and one far above it (another
// stack: a process switch) is stale.
struct ActiveTrap { uint16_t trap; uint32_t sp, ret; };
std::vector<ActiveTrap> g_atrap_active;
constexpr uint32_t kOtherStack = 256 * 1024;

// Guest bytes for the trace: RAM or ROM only.
const uint8_t* guest_bytes(uint32_t addr, uint32_t len)
{
    if (addr >= RAMBaseMac && addr + len <= RAMBaseMac + RAMSize)
        return RAMBaseHost + (addr - RAMBaseMac);
    if (ROMBaseHost && addr >= ROMBaseMac && addr + len <= ROMBaseMac + ROMSize)
        return ROMBaseHost + (addr - ROMBaseMac);
    return nullptr;
}

bool write_atraps(const std::string& path)
{
    std::vector<std::pair<AtrapKey, const AtrapSite*>> rows;
    rows.reserve(g_atrap_sites.size());
    for (auto& [key, site] : g_atrap_sites)
        rows.emplace_back(key, &site);
    std::sort(rows.begin(), rows.end(),
              [](auto& a, auto& b) { return a.second->seq < b.second->seq; });

    FILE* f = fopen(path.c_str(), "w");
    if (!f)
        return false;
    fprintf(f, "# trap\tpc\tparent\tirq\tcount\tseq\tapp\tcode (from pc-8)\n");
    for (auto& [key, s] : rows) {
        fprintf(f, "%04X\t%08X\t%04X\t%u\t%llu\t%u\t", key.trap, key.pc, key.parent,
                s->irq, (unsigned long long)s->count, s->seq);
        for (int i = 1; i <= (uint8_t)s->app[0] && i < 32; i++) {
            char c = s->app[i];
            fputc((c == '\t' || c == '\n' || (unsigned char)c < 0x20) ? '?' : c, f);
        }
        fputc('\t', f);
        for (int i = 0; i < s->code_len; i++)
            fprintf(f, "%02x", s->code[i]);
        fputc('\n', f);
    }
    return fclose(f) == 0;
}

} // namespace

static void atrap_trace_record(uint16_t opcode, uint32_t pc, uint32_t sp, int intmask)
{
    while (!g_atrap_active.empty()
           && (g_atrap_active.back().sp <= sp || g_atrap_active.back().sp - sp > kOtherStack))
        g_atrap_active.pop_back();
    uint16_t parent = g_atrap_active.empty() ? 0 : g_atrap_active.back().trap;

    // Toolbox traps with the auto-pop bit return to the caller's caller.
    uint32_t ret = pc + 2;
    if ((opcode & 0x0C00) == 0x0C00) {
        const uint8_t* p = guest_bytes(sp, 4);
        ret = p ? (uint32_t)(p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]) : 0;
    }
    if (g_atrap_active.size() < 256)
        g_atrap_active.push_back({opcode, sp, ret});
    uae_atrap_watch_pc = g_atrap_active.back().ret;

    AtrapSite& s = g_atrap_sites[AtrapKey{pc, opcode, parent}];
    if (s.count++ == 0) {
        s.seq = g_atrap_seq++;
        s.irq = (uint8_t)intmask;
        if (const uint8_t* name = guest_bytes(0x910, 32))
            memcpy(s.app, name, 32);
        uint32_t from = pc >= 8 ? pc - 8 : pc;
        for (uint32_t len = 24; len >= 2; len -= 2) {
            if (const uint8_t* code = guest_bytes(from, len)) {
                memcpy(s.code, code, len);
                s.code_len = len;
                break;
            }
        }
    }
}

static void atrap_trace_return(uint32_t sp)
{
    // Recursion through the same call site returns innermost first; an
    // A7 below the entry A7 means this is a deeper copy still running.
    if (!g_atrap_active.empty() && sp >= g_atrap_active.back().sp)
        g_atrap_active.pop_back();
    uae_atrap_watch_pc = g_atrap_active.empty() ? 0 : g_atrap_active.back().ret;
}

void atrap_trace_enable()
{
    uae_atrap_hook = atrap_trace_record;
    uae_atrap_return_hook = atrap_trace_return;
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

    if (!g_atrap_sites.empty())
        ok = ok && write_atraps(dir + "/atraps.tsv");

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
