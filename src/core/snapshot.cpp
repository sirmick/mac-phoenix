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

extern void (*uae_atrap_hook)(uint16_t opcode, uint32_t pc, uint32_t sp, uint32_t d0, uint32_t a0, int intmask);
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
    char detail[32] = {};        // driver name (Pascal) for driver calls
    char owner[64] = {};         // resource holding the caller, at first hit
};

// Dispatch traps: where each one's selector is at the A-line (generated
// from multiversal + Universal Interfaces by tools/macdecode/dispatch.py).
struct Dispatcher { int tool, index, kind; uint32_t mask; int sub_kind; uint32_t sub_when; };
const Dispatcher kDispatchers[] = {
#include "atrap_dispatchers.inc"
};
enum { SEL_NONE, SEL_D0, SEL_STACKW, SEL_STACKL, SEL_TRAPBITS,
       SEL_PB_CSCODE,   // driver call: csCode at A0+26, refnum at A0+24
       SEL_PB_REFNUM }; // driver I/O: refnum at A0+24, no selector
const Dispatcher* g_dispatch_os[256];
const Dispatcher* g_dispatch_tool[1024];

// A routine: trap word plus selector (and sub-selector) when it's a
// dispatcher, and the object a call is aimed at: the component instance
// of a CallComponent, the driver refnum of a driver call.
struct Routine {
    uint16_t trap = 0;
    uint8_t has_sel = 0, has_sub = 0, has_obj = 0;
    uint32_t sel = 0, sub = 0, obj = 0;
    bool operator==(const Routine& o) const {
        return trap == o.trap && has_sel == o.has_sel && has_sub == o.has_sub
            && has_obj == o.has_obj && sel == o.sel && sub == o.sub && obj == o.obj;
    }
};

// Key: caller pc, the routine, and the routine active when it fired.
struct AtrapKey {
    uint32_t pc;
    Routine routine, parent;
    bool operator==(const AtrapKey& o) const {
        return pc == o.pc && routine == o.routine && parent == o.parent;
    }
};
struct AtrapKeyHash {
    static uint64_t mix(const Routine& r) {
        return ((uint64_t)r.trap << 48) ^ ((uint64_t)r.sel << 16) ^ r.sub
             ^ ((uint64_t)r.obj << 24) ^ ((uint64_t)r.has_sel << 47);
    }
    size_t operator()(const AtrapKey& k) const {
        return std::hash<uint64_t>()(mix(k.routine) * 31 ^ mix(k.parent) * 17 ^ k.pc);
    }
};
std::unordered_map<AtrapKey, AtrapSite, AtrapKeyHash> g_atrap_sites;
uint32_t g_atrap_seq = 0;

// Shadow stack of active traps. Each ends when the CPU reaches its return
// address (the interpreter loop watches the innermost one). As a backstop
// for returns we miss (non-local exits), an entry whose A7 at entry is at
// or below the current A7 has returned, and one far above it (another
// stack: a process switch) is stale.
struct ActiveTrap { Routine routine; uint32_t sp, ret; };
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
    fprintf(f, "# trap\tsel\tsub\tobj\tpc\tparent\tparent_sel\tparent_sub\tparent_obj\tirq\tcount\tseq\tapp\tcode (from pc-8)\towner\tdetail\n");
    auto opt = [f](uint8_t has, uint32_t v) {
        if (has) fprintf(f, "%08X\t", v); else fputs("-\t", f);
    };
    for (auto& [key, s] : rows) {
        fprintf(f, "%04X\t", key.routine.trap);
        opt(key.routine.has_sel, key.routine.sel);
        opt(key.routine.has_sub, key.routine.sub);
        opt(key.routine.has_obj, key.routine.obj);
        fprintf(f, "%08X\t%04X\t", key.pc, key.parent.trap);
        opt(key.parent.has_sel, key.parent.sel);
        opt(key.parent.has_sub, key.parent.sub);
        opt(key.parent.has_obj, key.parent.obj);
        fprintf(f, "%u\t%llu\t%u\t", s->irq, (unsigned long long)s->count, s->seq);
        for (int i = 1; i <= (uint8_t)s->app[0] && i < 32; i++) {
            char c = s->app[i];
            fputc((c == '\t' || c == '\n' || (unsigned char)c < 0x20) ? '?' : c, f);
        }
        fputc('\t', f);
        for (int i = 0; i < s->code_len; i++)
            fprintf(f, "%02x", s->code[i]);
        fputc('\t', f);
        fputs(s->owner, f);
        fputc('\t', f);
        for (int i = 1; i <= (uint8_t)s->detail[0] && i < 32; i++) {
            char c = s->detail[i];
            fputc((c == '\t' || c == '\n' || (unsigned char)c < 0x20) ? '?' : c, f);
        }
        fputc('\n', f);
    }
    return fclose(f) == 0;
}

} // namespace

uint32_t guest_long(uint32_t addr)
{
    const uint8_t* p = guest_bytes(addr, 4);
    return p ? (uint32_t)(p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]) : 0;
}

// The selector a dispatcher reads, at the A-line: nothing pushed yet, so
// a stack selector is at (A7).
uint16_t guest_word(uint32_t addr)
{
    const uint8_t* p = guest_bytes(addr, 2);
    return p ? (uint16_t)(p[0] << 8 | p[1]) : 0;
}

uint32_t read_selector(int kind, uint32_t mask, uint16_t opcode, uint32_t sp, uint32_t d0, uint32_t a0)
{
    switch (kind) {
    case SEL_PB_CSCODE: return guest_word(a0 + 26) & mask;
    case SEL_D0:       return d0 & mask;
    case SEL_STACKW:   return (guest_long(sp) >> 16) & mask;
    case SEL_STACKL:   return guest_long(sp) & mask;
    case SEL_TRAPBITS: return opcode & mask;
    }
    return 0;
}

Routine routine_for(uint16_t opcode, uint32_t sp, uint32_t d0, uint32_t a0)
{
    Routine r;
    r.trap = opcode;
    const Dispatcher* d = (opcode & 0x0800) ? g_dispatch_tool[opcode & 0x3FF] : g_dispatch_os[opcode & 0xFF];
    if (!d)
        return r;
    if (d->kind == SEL_PB_CSCODE || d->kind == SEL_PB_REFNUM) {
        r.has_obj = 1;
        r.obj = guest_word(a0 + 24);                 // ioRefNum
        if (d->kind == SEL_PB_CSCODE) {
            r.has_sel = 1;
            r.sel = guest_word(a0 + 26);             // csCode
        }
        return r;
    }
    r.has_sel = 1;
    r.sel = read_selector(d->kind, d->mask, opcode, sp, d0, a0);
    if (d->sub_kind && r.sel == d->sub_when) {
        r.has_sub = 1;
        r.sub = read_selector(d->sub_kind, 0xFFFFFFFFu, opcode, sp, d0, a0);
        // CallComponent: (A7) is {flags.b, paramSize.b, what.w}, then
        // paramSize bytes of parameters, then the component instance.
        r.has_obj = 1;
        r.obj = guest_long(sp + 4 + ((r.sub >> 16) & 0xFF));
    }
    return r;
}

// A driver's name from its refnum: UTableBase[-refnum - 1] is a DCtlHandle;
// dCtlDriver (+0) is a pointer, or a handle when dCtlFlags (+4) has
// dRAMBased (0x0040); the driver header's name is at +18.
void driver_name(int16_t refnum, char out[32])
{
    if (refnum >= 0)
        return;
    uint32_t utable = guest_long(0x11C);
    uint32_t dce_h = guest_long(utable + 4 * (uint32_t)(-refnum - 1));
    uint32_t dce = dce_h ? guest_long(dce_h) : 0;
    if (!dce)
        return;
    uint32_t drvr = guest_long(dce);
    if (guest_word(dce + 4) & 0x0040)
        drvr = drvr ? guest_long(drvr) : 0;
    if (const uint8_t* name = drvr ? guest_bytes(drvr + 18, 32) : nullptr)
        memcpy(out, name, std::min<int>(name[0] + 1, 32));
}

// Which loaded resource holds pc: walk the resource map chain from
// TopMapHndl (the current process's maps, then the System's) and check
// each loaded resource's block. "File 'TYPE' id +offset", or "".
// Map in memory: +16 next map handle, +20 refnum, +24 type list offset;
// type list: count-1, then {type.l, count-1.w, ref list offset.w}; refs:
// {id.w, name offset.w, attrs+data offset.l, handle.l}. The block size is
// in the 32-bit block header, 8 bytes before the data.
void resource_owner(uint32_t pc, char out[64])
{
    uint32_t mh = guest_long(0xA50);
    for (int guard = 0; mh && guard < 64; guard++) {
        uint32_t map = guest_long(mh);
        if (!map)
            break;
        uint32_t types = map + guest_word(map + 24);
        int ntypes = (int16_t)guest_word(types) + 1;
        for (int t = 0; t < ntypes && t < 512; t++) {
            uint32_t te = types + 2 + 8 * t;
            uint32_t type = guest_long(te);
            int nrefs = (int16_t)guest_word(te + 4) + 1;
            uint32_t refs = types + guest_word(te + 6);
            for (int r = 0; r < nrefs && r < 4096; r++) {
                uint32_t ref = refs + 12 * r;
                uint32_t h = guest_long(ref + 8);
                uint32_t p = h ? guest_long(h) : 0;
                if (!p || pc < p)
                    continue;
                uint32_t size = guest_long(p - 8) & 0x00FFFFFF;
                if (pc >= p + size)
                    continue;
                // File name from the FCB: FCBSPtr + refnum, name at +62.
                int16_t refnum = (int16_t)guest_word(map + 20);
                char name[32] = "?";
                if (const uint8_t* n = guest_bytes(guest_long(0x34E) + refnum + 62, 32)) {
                    int len = std::min<int>(n[0], 31);
                    for (int i = 0; i < len; i++)
                        name[i] = (n[i + 1] < 0x20 || n[i + 1] == '\t') ? '?' : (char)n[i + 1];
                    name[len] = 0;
                }
                snprintf(out, 64, "%s '%c%c%c%c' %d +%x", name, (char)(type >> 24), (char)(type >> 16),
                         (char)(type >> 8), (char)type, (int16_t)guest_word(ref), pc - p);
                return;
            }
        }
        mh = guest_long(map + 16);
    }
}

static void atrap_trace_record(uint16_t opcode, uint32_t pc, uint32_t sp, uint32_t d0, uint32_t a0, int intmask)
{
    while (!g_atrap_active.empty()
           && (g_atrap_active.back().sp <= sp || g_atrap_active.back().sp - sp > kOtherStack))
        g_atrap_active.pop_back();
    Routine parent = g_atrap_active.empty() ? Routine{} : g_atrap_active.back().routine;
    Routine routine = routine_for(opcode, sp, d0, a0);

    // Toolbox traps with the auto-pop bit return to the caller's caller.
    uint32_t ret = (opcode & 0x0C00) == 0x0C00 ? guest_long(sp) : pc + 2;
    if (g_atrap_active.size() < 256)
        g_atrap_active.push_back({routine, sp, ret});
    uae_atrap_watch_pc = g_atrap_active.back().ret;

    AtrapSite& s = g_atrap_sites[AtrapKey{pc, routine, parent}];
    if (s.count++ == 0) {
        s.seq = g_atrap_seq++;
        s.irq = (uint8_t)intmask;
        if (const uint8_t* name = guest_bytes(0x910, 32))
            memcpy(s.app, name, 32);
        const Dispatcher* d = (opcode & 0x0800) ? nullptr : g_dispatch_os[opcode & 0xFF];
        if (d && routine.has_obj)
            driver_name((int16_t)routine.obj, s.detail);
        if (pc < ROMBaseMac)
            resource_owner(pc, s.owner);
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
    for (const Dispatcher& d : kDispatchers)
        (d.tool ? g_dispatch_tool : g_dispatch_os)[d.index] = &d;
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
