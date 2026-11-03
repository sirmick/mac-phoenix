/*
 * ppc_break.cpp - PowerPC breakpoints and traces (rsys/ppc_break.h,
 * docs/executor/PPCTrace.md).
 *
 * The core calls hook() before every instruction (PowerCore::
 * setInstructionHook, compiled in only when armed). It looks the PC up in
 * a sorted list of breakpoint addresses, published whole so another
 * thread (the API's) can replace it; everything else is under a mutex,
 * taken only on a hit, a trace, or a change.
 */
#include <rsys/ppc_break.h>
#include <base/builtinlibs.h>
#include <base/common.h>
#include <rsys/cfm.h>
#include <PowerCore.h>
#include <SegmentLdr.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

using namespace Executor;

namespace
{
struct Break
{
    std::string spec;
    std::string fragment;   /* empty: addr is absolute */
    std::string routine;    /* !NAME: Executor's routines of that name (addrs) */
    std::vector<uint32_t> addrs;
    int sect = -1;
    uint32_t off = 0;
    uint32_t addr = 0;      /* 0: not resolved (yet) */
    uint32_t trace = 0;
    uint32_t hits_max = 1;  /* 0: every hit */
    uint32_t hits = 0;
    /* #rN[+-OFF][:LEN] or #$ADDR[:LEN]: memory shown on a hit */
    /* ^: the address is read through once more (^^r3+4: a handle's data) */
    struct Mem { int reg; int32_t off; uint32_t len; int derefs; std::string text; };
    std::vector<Mem> mems;
};

struct Report
{
    unsigned seq;
    std::string text;
};

std::recursive_mutex mutex;
std::vector<Break> breaks;
std::atomic<const std::vector<uint32_t> *> active{new std::vector<uint32_t>()};
std::atomic<bool> resolve_pending{false};
bool armed = false;

std::deque<Report> reports;   /* the last few, for the API */
const size_t REPORTS_KEPT = 8;
const size_t REPORT_MAX = 256 * 1024;
unsigned report_seq = 0;
std::string *open_report = nullptr;   /* the one being written (a trace) */

uint32_t trace_left = 0;
bool tracing = false;
PowerCoreState trace_prev;

uint32_t xer(const PowerCoreState& s)
{
    return (s.SO ? 0x80000000u : 0) | (s.OV ? 0x40000000u : 0) | (s.CA ? 0x20000000u : 0);
}

bool parse_hex(const std::string& s, uint32_t& v)
{
    std::string t = s;
    if(!t.empty() && t[0] == '$')
        t.erase(0, 1);
    else if(t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
        t.erase(0, 2);
    if(t.empty() || t.size() > 8 || t.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return false;
    v = (uint32_t)strtoul(t.c_str(), nullptr, 16);
    return true;
}

bool parse_dec(const std::string& s, uint32_t& v)
{
    if(s.empty() || s.size() > 9 || s.find_first_not_of("0123456789") != std::string::npos)
        return false;
    v = (uint32_t)strtoul(s.c_str(), nullptr, 10);
    return true;
}

bool parse_mem(const std::string& s, Break::Mem& m)
{
    m.text = s;
    std::string t = s;
    m.len = 0x20;
    m.derefs = 0;
    while(!t.empty() && t[0] == '^')
        m.derefs++, t.erase(0, 1);
    size_t colon = t.find(':');
    if(colon != std::string::npos)
    {
        if(!parse_hex(t.substr(colon + 1), m.len) || !m.len || m.len > 0x1000)
            return false;
        t.erase(colon);
    }
    if(t.size() > 1 && t[0] == 'r' && isdigit((unsigned char)t[1]))
    {
        size_t end = 1;
        while(end < t.size() && isdigit((unsigned char)t[end]))
            end++;
        m.reg = atoi(t.substr(1, end - 1).c_str());
        uint32_t off = 0;
        if(m.reg > 31 || (end < t.size() && ((t[end] != '+' && t[end] != '-') || !parse_hex(t.substr(end + 1), off))))
            return false;
        m.off = end < t.size() && t[end] == '-' ? -(int32_t)off : (int32_t)off;
        return true;
    }
    uint32_t addr;
    m.reg = -1;
    if(!parse_hex(t, addr))
        return false;
    m.off = (int32_t)addr;
    return true;
}

bool parse(const std::string& spec, Break& b, std::string& err)
{
    b.spec = spec;
    std::string where = spec;
    for(size_t hash; (hash = where.rfind('#')) != std::string::npos; where.erase(hash))
    {
        Break::Mem m;
        if(!parse_mem(where.substr(hash + 1), m))
            return err = "bad #MEM '" + where.substr(hash + 1) + "' ([^...]rN[+OFF][:LEN] or [^...]$ADDR[:LEN], hex)", false;
        b.mems.insert(b.mems.begin(), m);
    }
    size_t star = where.rfind('*');
    if(star != std::string::npos)
    {
        if(!parse_dec(where.substr(star + 1), b.hits_max))
            return err = "bad *HITS in '" + spec + "'", false;
        where.erase(star);
    }
    size_t at = where.rfind('@');
    if(at != std::string::npos)
    {
        if(!parse_dec(where.substr(at + 1), b.trace))
            return err = "bad @TRACE in '" + spec + "'", false;
        where.erase(at);
    }
    if(!where.empty() && where[0] == '!')
    {
        b.routine = where.substr(1);
        if(b.routine.empty())
            return err = "no routine name in '" + spec + "'", false;
        return true;
    }
    size_t plus = where.rfind('+');
    if(plus == std::string::npos)
    {
        if(!parse_hex(where, b.addr) || !b.addr)
            return err = "'" + spec + "': not an address, nor FRAGMENT+OFF", false;
        return true;
    }
    if(!parse_hex(where.substr(plus + 1), b.off))
        return err = "bad offset in '" + spec + "'", false;
    b.fragment = where.substr(0, plus);
    size_t colon = b.fragment.rfind(':');
    uint32_t sect;
    if(colon != std::string::npos && parse_dec(b.fragment.substr(colon + 1), sect))
    {
        b.sect = (int)sect;
        b.fragment.erase(colon);
    }
    if(b.fragment.empty())
        return err = "no fragment name in '" + spec + "'", false;
    return true;
}

/* Under the mutex. */
void publish()
{
    auto *v = new std::vector<uint32_t>();
    for(const Break& b : breaks)
        if(!b.hits_max || b.hits < b.hits_max)
        {
            if(b.addr)
                v->push_back(b.addr);
            v->insert(v->end(), b.addrs.begin(), b.addrs.end());
        }
    std::sort(v->begin(), v->end());
    v->erase(std::unique(v->begin(), v->end()), v->end());
    active.store(v);   /* the old list may still be read: kept */
}

/* Under the mutex, on a thread that runs guest code (CFM's list). */
void resolve()
{
    for(Break& b : breaks)
    {
        if(!b.routine.empty() && b.addrs.empty())
        {
            b.addrs = builtinlibs::callbackAddresses(b.routine);
            if(!b.addrs.empty())
                fprintf(stderr, "[ppcbreak] '%s': %zu routine(s), first at $%08X\n", b.spec.c_str(), b.addrs.size(),
                        b.addrs[0]);
        }
        if(b.fragment.empty())
            continue;
        uint32_t a = ROMlib_cfm_resolve(b.fragment, b.sect, b.off);
        if(a && a != b.addr)
            fprintf(stderr, "[ppcbreak] '%s' at $%08X\n", b.spec.c_str(), a);
        if(a)
            b.addr = a;
    }
    publish();
}

void out(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void out(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", line);
    if(open_report && open_report->size() < REPORT_MAX)
    {
        *open_report += line;
        *open_report += '\n';
        if(open_report->size() >= REPORT_MAX)
            *open_report += "... (the rest in the log)\n";
    }
}

void begin_report()
{
    reports.push_back({ ++report_seq, std::string() });
    if(reports.size() > REPORTS_KEPT)
        reports.pop_front();
    open_report = &reports.back().text;
}

void end_report()
{
    open_report = nullptr;
}

std::string app_name()
{
    const unsigned char *n = (const unsigned char *)LM(CurApName);
    return std::string((const char *)n + 1, std::min<int>(n[0], 31));
}

/* "$ADDR where" for an address in code. */
std::string place(uint32_t addr)
{
    char buf[32];
    snprintf(buf, sizeof buf, "$%08X", addr);
    std::string s = buf;
    std::string d = ROMlib_cfm_describe(addr);
    if(d.empty())
        if(const char *name = builtinlibs::callbackName(addr))
            d = std::string("Executor's ") + name;
    if(!d.empty())
        s += " " + d;
    return s;
}

void dump_mem(PowerCore& cpu, const PowerCoreState& s, const Break::Mem& m)
{
    uint32_t base = m.reg < 0 ? (uint32_t)m.off : s.r[m.reg] + m.off;
    for(int i = 0; i < m.derefs; i++)
        if(!cpu.peek32(base, base))
        {
            out("  #%s: unmapped", m.text.c_str());
            return;
        }
    out("  #%s = $%08X:", m.text.c_str(), base);
    for(uint32_t row = 0; row < m.len; row += 16)
    {
        char line[200], chars[17] = "";
        int n = snprintf(line, sizeof line, "    $%08X:", base + row);
        for(uint32_t k = 0; k < 16 && row + k < m.len; k += 4)
        {
            uint32_t w;
            bool ok = cpu.peek32(base + row + k, w);
            n += ok ? snprintf(line + n, sizeof line - n, " %08X", w) : snprintf(line + n, sizeof line - n, " --------");
            for(int c = 0; c < 4; c++)
            {
                unsigned char ch = ok ? (unsigned char)(w >> (24 - 8 * c)) : '?';
                chars[k + c] = ch >= 0x20 && ch < 0x7F ? (char)ch : '.';
                chars[k + c + 1] = 0;
            }
        }
        out("%-50s %s", line, chars);
    }
}

void dump(PowerCore& cpu, const PowerCoreState& s)
{
    for(int i = 0; i < 32; i += 4)
        out("  r%-2d %08X  r%-2d %08X  r%-2d %08X  r%-2d %08X", i, s.r[i], i + 1, s.r[i + 1], i + 2, s.r[i + 2], i + 3,
            s.r[i + 3]);
    out("  cr  %08X  xer %08X  ctr %08X  lr  %s", s.cr, xer(s), s.ctr, place(s.lr).c_str());
    /* The back chain: each frame's first word is its caller's r1, and a
       routine saves its return address at 8 in its caller's frame. */
    out("  back chain from r1 = $%08X:", s.r[1]);
    uint32_t sp = s.r[1];
    for(int depth = 0; depth < 24; depth++)
    {
        uint32_t back, ret;
        if(!cpu.peek32(sp, back) || !back || back <= sp || back - sp > 0x100000 || !cpu.peek32(back + 8, ret))
            break;
        out("    sp $%08X  ret %s", back, place(ret).c_str());
        sp = back;
    }
    out("  stack at r1:");
    for(int row = 0; row < 4; row++)
    {
        char line[200];
        int n = snprintf(line, sizeof line, "    $%08X:", s.r[1] + 32 * row);
        for(int k = 0; k < 8; k++)
        {
            uint32_t w;
            n += cpu.peek32(s.r[1] + 32 * row + 4 * k, w) ? snprintf(line + n, sizeof line - n, " %08X", w)
                                                          : snprintf(line + n, sizeof line - n, " --------");
        }
        out("%s", line);
    }
}

void trace_line(PowerCore& cpu, uint32_t pc)
{
    char dis[128] = "";
    uint32_t word = 0;
    cpu.peek32(pc, word);
    if(word == POWERCORE_OP_SYSCALL)
    {
        const char *name = builtinlibs::callbackName(pc);
        snprintf(dis, sizeof dis, "(call Executor's %s)", name ? name : "?");
    }
    else if(pc == cpu.exitAddress())
        snprintf(dis, sizeof dis, "(back to Executor)");
    else if(!cpu.disassemble(pc, dis, sizeof dis))
        snprintf(dis, sizeof dis, ".long $%08X", word);
    std::string where = ROMlib_cfm_where(pc);
    out("  $%08X %-28s %s", pc, where.c_str(), dis);
}

/* What the instruction before changed. */
void trace_changes(const PowerCoreState& now)
{
    std::string line;
    char buf[64];
    for(int i = 0; i < 32; i++)
        if(now.r[i] != trace_prev.r[i])
            snprintf(buf, sizeof buf, " r%d=%08X", i, now.r[i]), line += buf;
    for(int i = 0; i < 32; i++)
        if(memcmp(&now.f[i], &trace_prev.f[i], sizeof(double)))
            snprintf(buf, sizeof buf, " f%d=%g", i, now.f[i]), line += buf;
    if(now.cr != trace_prev.cr)
        snprintf(buf, sizeof buf, " cr=%08X", now.cr), line += buf;
    if(now.lr != trace_prev.lr)
        snprintf(buf, sizeof buf, " lr=%08X", now.lr), line += buf;
    if(now.ctr != trace_prev.ctr)
        snprintf(buf, sizeof buf, " ctr=%08X", now.ctr), line += buf;
    if(xer(now) != xer(trace_prev))
        snprintf(buf, sizeof buf, " xer=%08X", xer(now)), line += buf;
    if(!line.empty())
        out("  %38s->%s", "", line.c_str());
}

void hook(PowerCore& cpu, uint32_t pc)
{
    if(resolve_pending.load(std::memory_order_relaxed) && resolve_pending.exchange(false))
    {
        std::lock_guard lk(mutex);
        resolve();
    }
    const std::vector<uint32_t>& a = *active.load(std::memory_order_acquire);
    bool hit = !a.empty() && std::binary_search(a.begin(), a.end(), pc);
    if(!hit && !tracing)
        return;

    std::lock_guard lk(mutex);
    PowerCoreState now;
    cpu.liveState(now);
    if(tracing)
    {
        trace_changes(now);
        trace_prev = now;
        if(!trace_left)
        {
            out("[ppcbreak] end of trace");
            tracing = false;
            end_report();
        }
    }
    if(hit)
    {
        Break *first = nullptr;
        for(Break& b : breaks)
            if((b.addr == pc || std::count(b.addrs.begin(), b.addrs.end(), pc)) &&
               (!b.hits_max || b.hits < b.hits_max))
            {
                b.hits++;
                if(!first)
                    first = &b;
            }
        if(first && !tracing)
        {
            begin_report();
            out("[ppcbreak] hit %u of '%s' at %s, in '%s'", first->hits, first->spec.c_str(), place(pc).c_str(),
                app_name().c_str());
            dump(cpu, now);
            for(const Break::Mem& m : first->mems)
                dump_mem(cpu, now, m);
            if(first->trace)
            {
                out("[ppcbreak] trace of %u instructions:", first->trace);
                tracing = true;
                trace_left = first->trace;
                trace_prev = now;
            }
            else
                end_report();
        }
        else if(first)
            out("[ppcbreak] (hit %u of '%s')", first->hits, first->spec.c_str());
        if(first && first->hits_max && first->hits >= first->hits_max)
            publish();
    }
    if(tracing && trace_left)
    {
        trace_line(cpu, pc);
        trace_left--;
    }
}

void json_string(std::string& j, const std::string& s)
{
    j += '"';
    for(unsigned char c : s)
    {
        if(c == '"' || c == '\\')
            j += '\\', j += (char)c;
        else if(c == '\n')
            j += "\\n";
        else if(c < 0x20)
        {
            char buf[8];
            snprintf(buf, sizeof buf, "\\u%04x", c);
            j += buf;
        }
        else if(c >= 0x80)
        {
            /* MacRoman names: as Latin-1, which is valid JSON once in UTF-8 */
            j += (char)(0xC0 | (c >> 6));
            j += (char)(0x80 | (c & 0x3F));
        }
        else
            j += (char)c;
    }
    j += '"';
}
}

void Executor::ppc_break_arm()
{
    std::lock_guard lk(mutex);
    if(armed)
        return;
    PowerCore& cpu = PowerCore::instance();
    cpu.instructionHooks = true;
    armed = cpu.setInstructionHook(&hook);
    if(!armed)
        fprintf(stderr, "[ppcbreak] the PowerPC core is already running without instruction calls\n");
}

bool Executor::ppc_break_armed()
{
    return armed;
}

bool Executor::ppc_break_add(const std::string& spec, std::string& err)
{
    std::lock_guard lk(mutex);
    if(!armed)
        return err = "not armed: start the session with --executor-ppc-break (a SPEC, or on)", false;
    Break b;
    if(!parse(spec, b, err))
        return false;
    breaks.push_back(b);
    if(b.fragment.empty() && b.routine.empty())
        publish();
    else
        resolve_pending = true;   /* on the guest's thread: CFM's list */
    return true;
}

bool Executor::ppc_break_remove(const std::string& spec)
{
    std::lock_guard lk(mutex);
    size_t before = breaks.size();
    breaks.erase(std::remove_if(breaks.begin(), breaks.end(),
                                [&](const Break& b) { return spec == "all" || b.spec == spec; }),
                 breaks.end());
    publish();
    return breaks.size() != before;
}

void Executor::ppc_break_fragments_changed()
{
    if(!armed)
        return;
    std::lock_guard lk(mutex);
    resolve();
}

std::string Executor::ppc_break_json(unsigned since)
{
    std::lock_guard lk(mutex);
    std::string j = "{\"armed\": ";
    j += armed ? "true" : "false";
    j += ", \"breakpoints\": [";
    for(size_t i = 0; i < breaks.size(); i++)
    {
        const Break& b = breaks[i];
        char buf[160];
        j += i ? ", {\"spec\": " : "{\"spec\": ";
        json_string(j, b.spec);
        uint32_t at = b.addr ? b.addr : b.addrs.empty() ? 0 : b.addrs[0];
        snprintf(buf, sizeof buf, ", \"address\": \"%s%08X\", \"hits\": %u, \"hits_max\": %u, \"trace\": %u}",
                 at ? "$" : "pending ", at, b.hits, b.hits_max, b.trace);
        j += buf;
    }
    j += "], \"hits\": [";
    bool firstr = true;
    for(const Report& r : reports)
    {
        if(r.seq <= since)
            continue;
        j += firstr ? "{\"seq\": " : ", {\"seq\": ";
        firstr = false;
        j += std::to_string(r.seq) + ", \"done\": ";
        j += (&r.text == open_report) ? "false" : "true";
        j += ", \"text\": ";
        json_string(j, r.text);
        j += "}";
    }
    j += "]}";
    return j;
}

std::string Executor::ppc_break_command(const std::string& command)
{
    std::string verb = command.substr(0, command.find(' '));
    std::string arg = command.size() > verb.size() ? command.substr(verb.size() + 1) : "";
    std::string err;
    if(verb == "add" && !ppc_break_add(arg, err))
    {
        std::string j = "{\"error\": ";
        json_string(j, err);
        return j + "}";
    }
    if(verb == "remove" && !ppc_break_remove(arg))
        return "{\"error\": \"no such breakpoint\"}";
    unsigned since = verb == "list" && !arg.empty() ? (unsigned)strtoul(arg.c_str(), nullptr, 10) : 0;
    return ppc_break_json(since);
}
