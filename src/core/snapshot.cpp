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
#include "../common/include/platform.h"
#include "../cpu/uae_cpu/vclock.h"

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
extern uint32_t uae_current_pc(void);
extern void (*uae_atrap_return_hook)(uint32_t sp, uint32_t d0);

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
    uint8_t has_result = 0;      // first call's result, taken at its return:
    uint32_t res_d0 = 0;         //   D0 (OS traps' result code)
    uint32_t res_top = 0;        //   long at (SP) (a Pascal function's result)
    uint32_t in_d0 = 0;          // D0, A0 and TheZone at the first call
    uint32_t in_a0 = 0;
    uint32_t in_zone = 0;
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

// Trap installs (_SetTrapAddress and its variants), in order: the patch
// history of every slot. Owners are resolved when the install happens,
// while the installing code and the new routine are still where they were.
struct TrapInstall {
    uint32_t seq;                // position among all traced trap calls
    uint16_t trap;               // the _SetTrapAddress word used
    uint8_t tool;                // Toolbox table (else OS)
    uint16_t index;
    uint32_t addr, old, pc;      // new entry, previous entry, installer
    char app[32];
    char installer[64], target[64];
};
std::vector<TrapInstall> g_trap_installs;
uint64_t g_atrap_calls = 0;

// Low-memory / ExpandMem watch: which code reads or writes which global.
// The trace wraps the platform's data access functions; each distinct
// (address, size, write, pc) is kept with a count and, at first hit, the
// app and the resource holding the pc.
struct MemKey {
    uint32_t addr, pc;
    uint8_t size, write;
    bool operator==(const MemKey& o) const {
        return addr == o.addr && pc == o.pc && size == o.size && write == o.write;
    }
};
struct MemKeyHash {
    size_t operator()(const MemKey& k) const {
        return std::hash<uint64_t>()(((uint64_t)k.addr << 32 | k.pc) ^ ((uint64_t)k.size << 61) ^ k.write);
    }
};
struct MemSite {
    uint64_t count = 0;
    uint32_t seq = 0;            // trap calls traced before the first hit
    char app[32] = {};
    char owner[64] = {};
};
std::unordered_map<MemKey, MemSite, MemKeyHash> g_mem_sites;
uint32_t g_expandmem = 0, g_expandmem_size = 0;
uint8_t (*g_read8)(uint32_t);
uint16_t (*g_read16)(uint32_t);
uint32_t (*g_read32)(uint32_t);
void (*g_write8)(uint32_t, uint8_t);
void (*g_write16)(uint32_t, uint16_t);
void (*g_write32)(uint32_t, uint32_t);
bool g_watch_on = false;
bool g_in_watch = false;     // our own reads (owner lookup) aren't traced

// Shadow stack of active traps. Each ends when the CPU reaches its return
// address (the interpreter loop watches the innermost one). As a backstop
// for returns we miss (non-local exits), an entry whose A7 at entry is at
// or below the current A7 has returned, and one far above it (another
// stack: a process switch) is stale.
struct ActiveTrap { Routine routine; uint32_t sp, ret; bool idle; AtrapSite* site; };

// Traps that wait for time to pass (vclock counts every instruction inside).
bool is_idle_trap(uint16_t w)
{
    switch (w & 0xFBFF) {
    case 0xA860:    // WaitNextEvent
    case 0xA970:    // GetNextEvent
    case 0xA971:    // EventAvail
    case 0xA9B4:    // SystemTask
        return true;
    }
    return (w & 0xF0FF) == 0xA03B;   // Delay
}

std::vector<ActiveTrap> g_atrap_active;
void active_pop()
{
    if (g_atrap_active.back().idle && vclock_idle_depth > 0)
        vclock_idle_depth--;
    g_atrap_active.pop_back();
}
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

bool write_mem_sites(const std::string& path)
{
    std::vector<std::pair<MemKey, const MemSite*>> rows;
    for (auto& [k, s] : g_mem_sites)
        rows.emplace_back(k, &s);
    std::sort(rows.begin(), rows.end(), [](auto& a, auto& b) {
        return a.first.addr != b.first.addr ? a.first.addr < b.first.addr : a.second->seq < b.second->seq;
    });
    FILE* f = fopen(path.c_str(), "w");
    if (!f)
        return false;
    fprintf(f, "# addr\tsize\trw\tpc\tcount\tseq\tapp\towner\texpandmem\n");
    fprintf(f, "# expandmem %08X size %u\n", g_expandmem, g_expandmem_size);
    for (auto& [k, s] : rows) {
        fprintf(f, "%08X\t%u\t%c\t%08X\t%llu\t%u\t", k.addr, k.size, k.write ? 'w' : 'r', k.pc,
                (unsigned long long)s->count, s->seq);
        for (int i = 1; i <= (uint8_t)s->app[0] && i < 32; i++) {
            char c = s->app[i];
            fputc((c == '\t' || c == '\n' || (unsigned char)c < 0x20) ? '?' : c, f);
        }
        fprintf(f, "\t%s\t%s\n", s->owner,
                (g_expandmem && k.addr >= g_expandmem) ? "1" : "");
    }
    return fclose(f) == 0;
}

bool write_installs(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "w");
    if (!f)
        return false;
    fprintf(f, "# seq\ttrap\ttable\tindex\taddr\told\tpc\tapp\tinstaller\ttarget\n");
    for (const TrapInstall& t : g_trap_installs) {
        fprintf(f, "%u\t%04X\t%s\t%03X\t%08X\t%08X\t%08X\t", t.seq, t.trap,
                t.tool ? "tool" : "os", t.index, t.addr, t.old, t.pc);
        for (int i = 1; i <= (uint8_t)t.app[0] && i < 32; i++) {
            char c = t.app[i];
            fputc((c == '\t' || c == '\n' || (unsigned char)c < 0x20) ? '?' : c, f);
        }
        fprintf(f, "\t%s\t%s\n", t.installer, t.target);
    }
    return fclose(f) == 0;
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
    fprintf(f, "# trap\tsel\tsub\tobj\tpc\tparent\tparent_sel\tparent_sub\tparent_obj\tirq\tcount\tseq\tapp\tcode (from pc-8)\towner\tdetail\tres_d0\tres_top\tin_d0\tin_a0\tin_zone\n");
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
        if (s->has_result)
            fprintf(f, "\t%08X\t%08X", s->res_d0, s->res_top);
        else
            fputs("\t-\t-", f);
        fprintf(f, "\t%08X\t%08X\t%08X\n", s->in_d0, s->in_a0, s->in_zone);
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
    // A Toolbox trap with the auto-pop bit is reached through glue that
    // left its caller's return address on top: stack arguments start at +4.
    if ((opcode & 0x0C00) == 0x0C00)
        sp += 4;
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

void mem_note(uint32_t addr, int size, bool write)
{
    if (g_in_watch)
        return;
    // $0-$FF are the exception vectors: the CPU fetches them on every
    // trap and interrupt, which would be charged to the trapping code.
    bool low = addr >= 0x100 && addr < 0x2000;
    bool em = g_expandmem && addr >= g_expandmem && addr < g_expandmem + g_expandmem_size;
    if (!low && !em)
        return;
    g_in_watch = true;
    uint32_t pc = uae_current_pc();
    MemSite& s = g_mem_sites[MemKey{addr, pc, (uint8_t)size, (uint8_t)write}];
    if (s.count++ == 0) {
        s.seq = (uint32_t)g_atrap_calls;
        if (const uint8_t* name = guest_bytes(0x910, 32))
            memcpy(s.app, name, 32);
        if (pc < ROMBaseMac)
            resource_owner(pc, s.owner);
    }
    g_in_watch = false;
}

uint8_t watch_read8(uint32_t a) { mem_note(a, 1, false); return g_read8(a); }
uint16_t watch_read16(uint32_t a) { mem_note(a, 2, false); return g_read16(a); }
uint32_t watch_read32(uint32_t a) { mem_note(a, 4, false); return g_read32(a); }
void watch_write8(uint32_t a, uint8_t v) { mem_note(a, 1, true); g_write8(a, v); }
void watch_write16(uint32_t a, uint16_t v) { mem_note(a, 2, true); g_write16(a, v); }
void watch_write32(uint32_t a, uint32_t v) { mem_note(a, 4, true); g_write32(a, v); }

// Wrap the platform's data access functions once they exist (at the first
// traced trap), and follow ExpandMem ($2B6) as the boot sets it up. The
// record's own header gives its size (emSize, a long at +2).
void watch_update()
{
    if (!g_watch_on && g_platform.mem_read_long) {
        g_read8 = g_platform.mem_read_byte;   g_platform.mem_read_byte = watch_read8;
        g_read16 = g_platform.mem_read_word;  g_platform.mem_read_word = watch_read16;
        g_read32 = g_platform.mem_read_long;  g_platform.mem_read_long = watch_read32;
        g_write8 = g_platform.mem_write_byte;   g_platform.mem_write_byte = watch_write8;
        g_write16 = g_platform.mem_write_word;  g_platform.mem_write_word = watch_write16;
        g_write32 = g_platform.mem_write_long;  g_platform.mem_write_long = watch_write32;
        g_watch_on = true;
    }
    uint32_t em = guest_long(0x2B6);
    if (em != g_expandmem && em && em < RAMBaseMac + RAMSize) {
        g_expandmem = em;
        g_expandmem_size = std::min<uint32_t>(guest_long(em + 2), 0x10000);
    }
}

// _SetTrapAddress ($A047): D0 = trap number, A0 = routine. Bit $0200 of
// the trap word means the new form, where bit $0400 picks the Toolbox
// table; the old form puts numbers above $4F (except $54, $57) there.
// Tables: OS at $400 (256 longs), Toolbox at $E00 (1024 longs).
void record_install(uint16_t opcode, uint32_t pc, uint32_t d0, uint32_t a0)
{
    TrapInstall t = {};
    bool tool;
    uint16_t n = d0 & 0xFFFF;
    if (opcode & 0x0200) {
        tool = opcode & 0x0400;
    } else {
        n &= 0x1FF;
        tool = n > 0x4F && n != 0x54 && n != 0x57;
    }
    t.seq = (uint32_t)g_atrap_calls;
    t.trap = opcode;
    t.tool = tool;
    t.index = n & (tool ? 0x3FF : 0xFF);
    t.addr = a0;
    t.old = guest_long((tool ? 0xE00 : 0x400) + 4 * t.index);
    t.pc = pc;
    if (const uint8_t* name = guest_bytes(0x910, 32))
        memcpy(t.app, name, 32);
    if (pc < ROMBaseMac)
        resource_owner(pc, t.installer);
    if (a0 < ROMBaseMac)
        resource_owner(a0, t.target);
    g_trap_installs.push_back(t);
}

// --deterministic: both backends rebase their virtual clock when the traced
// application first calls a trap from its own code, so boot time before it
// (the whole ROM boot on one side, almost none on Executor) drops out.
static const char kTracedApp[] = "\006Finder";

// --snapshot-at NAME@SECONDS: request a snapshot once the virtual clock is
// that far past the traced app's start (the snapshot itself is taken at the
// backend's next safe point).
static uint64_t g_snapshot_at_usec = 0;
static std::string g_snapshot_at_name;

static void snapshot_at_check()
{
    if (!g_snapshot_at_usec || !vclock_rebased || vclock_usec() < g_snapshot_at_usec)
        return;
    g_snapshot_at_usec = 0;
    auto& cfg = config::EmulatorConfig::instance();
    if (!snapshot_prepare(cfg.storage_dir, g_snapshot_at_name).empty())
        snapshot_request();
}

static void atrap_trace_record(uint16_t opcode, uint32_t pc, uint32_t sp, uint32_t d0, uint32_t a0, int intmask)
{
    g_atrap_calls++;
    snapshot_at_check();
    if (vclock_enabled && !vclock_rebased && pc >= vclock_app_lo && pc < vclock_app_hi) {
        const uint8_t* name = guest_bytes(0x910, 8);
        if (name && memcmp(name, kTracedApp, kTracedApp[0] + 1) == 0)
            vclock_rebase();
    }
    if ((g_atrap_calls & 0xFF) == 1)
        watch_update();
    if ((opcode & 0xF1FF) == 0xA047)
        record_install(opcode, pc, d0, a0);

    while (!g_atrap_active.empty()
           && (g_atrap_active.back().sp <= sp || g_atrap_active.back().sp - sp > kOtherStack))
        active_pop();
    Routine parent = g_atrap_active.empty() ? Routine{} : g_atrap_active.back().routine;
    Routine routine = routine_for(opcode, sp, d0, a0);

    // Toolbox traps with the auto-pop bit return to the caller's caller.
    // _LoadSeg never returns: it fixes the jump table entry and jumps into
    // the segment, so it doesn't open a nesting level.
    uint32_t ret = (opcode & 0x0C00) == 0x0C00 ? guest_long(sp) : pc + 2;
    if ((opcode & 0xFBFF) != 0xA9F0 && g_atrap_active.size() < 256) {
        bool idle = is_idle_trap(opcode);
        g_atrap_active.push_back({routine, sp, ret, idle, nullptr});
        if (idle)
            vclock_idle_depth++;
    }
    if (!g_atrap_active.empty())
        uae_atrap_watch_pc = g_atrap_active.back().ret;

    AtrapSite& s = g_atrap_sites[AtrapKey{pc, routine, parent}];
    // unordered_map nodes stay put: the return hook fills in the result.
    if (!g_atrap_active.empty() && g_atrap_active.back().routine == routine && !s.has_result)
        g_atrap_active.back().site = &s;
    if (s.count++ == 0) {
        s.seq = g_atrap_seq++;
        s.irq = (uint8_t)intmask;
        s.in_d0 = d0;
        s.in_a0 = a0;
        s.in_zone = guest_long(0x118);
        if (const uint8_t* name = guest_bytes(0x910, 32))
            memcpy(s.app, name, 32);
        const Dispatcher* d = (opcode & 0x0800) ? nullptr : g_dispatch_os[opcode & 0xFF];
        if (d && routine.has_obj)
            driver_name((int16_t)routine.obj, s.detail);
        // Auto-pop glue leaves its caller's return address on top: keep it,
        // since the trap's own pc is just the shared glue.
        if ((opcode & 0x0C00) == 0x0C00) {
            int n = snprintf(s.detail + 1, sizeof s.detail - 1, "caller $%08X", guest_long(sp) - 4);
            s.detail[0] = (char)std::min<int>(n, sizeof s.detail - 2);
        }
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

static void atrap_trace_return(uint32_t sp, uint32_t d0)
{
    // Recursion through the same call site returns innermost first; an
    // A7 below the entry A7 means this is a deeper copy still running.
    if (!g_atrap_active.empty() && sp >= g_atrap_active.back().sp) {
        if (AtrapSite* site = g_atrap_active.back().site) {
            site->has_result = 1;
            site->res_d0 = d0;
            site->res_top = guest_long(sp);
        }
        active_pop();
    }
    uae_atrap_watch_pc = g_atrap_active.empty() ? 0 : g_atrap_active.back().ret;
}

void atrap_trace_enable()
{
    auto& cfg = config::EmulatorConfig::instance();
    auto at = cfg.snapshot_at.rfind('@');
    if (at != std::string::npos) {
        g_snapshot_at_name = cfg.snapshot_at.substr(0, at);
        g_snapshot_at_usec = VCLOCK_APP_START_USEC + (uint64_t)(atof(cfg.snapshot_at.c_str() + at + 1) * 1e6);
    }
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
    if (!g_trap_installs.empty())
        ok = ok && write_installs(dir + "/trap_installs.tsv");
    if (!g_mem_sites.empty())
        ok = ok && write_mem_sites(dir + "/lowmem_access.tsv");

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
