/* ot.cpp — Executor's Open Transport: OT 1.1's 68k client API in C++.
 *
 * How 68k applications reach OT. They link Apple's static glue
 * (OpenTransport.o, OpenTransportApp.o, OpenTptInet.o). Each OT function
 * there is a stub:
 *
 *     LEA    stub_record,A0     ; stub_record: +0 the bound address (0
 *     MOVE.L (A0),D0            ;   until bound), +4 the function set's
 *     BEQ.S  @bind              ;   client vector record, +12 the index
 *     MOVEA.L D0,A0             ;   of the function in the set (1-based)
 *     JMP    (A0)
 * @bind JMP  __SLM11FuncDispatch
 *
 * and __SLM11FuncDispatch, also in the glue, jumps to the Shared Library
 * Manager's binder at SLMGlobal+$88 (SLMGlobal: ExpandMem+$10C) with A0
 * the stub record. The binder finds the function set by the ID in the
 * client vector record (+16, a C string; +12, its version words), stores
 * the function's address in the stub record and jumps to it with the
 * caller's stack as it was.
 *
 * Every ASLM client's glue depends on SLMGlobal+$88, so Executor wraps
 * that binder: a stub of one of OT's function sets (ot_names.inc) is
 * bound to a callback here, any other goes on to Apple's binder with every
 * register as it came. Apple's own OT libraries (registered with ASLM from
 * the Extensions folder) are never bound. The C++ functions are in
 * ot_*.cpp, registered by function set and name; a function nobody has
 * written yet is reported by name the first time it is bound.
 *
 * InitOpenTransport first asks Gestalt 'otan' for the "present" bit and
 * answers kOTNotFoundErr without it; with OT on (--executor-ot) and the
 * Shared Library Manager up, Gestalt answers here, and that is when the
 * binder is wrapped.
 */

#include "ot_internal.h"
#include <base/traps.h>
#include <base/builtinlibs.h>
#include <PowerCore.h>

#include <rsys/opentransport.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

using namespace Executor;
using namespace Executor::ot;

bool Executor::ROMlib_open_transport = true;

namespace
{

/* ---- the function sets ----------------------------------------------- */

struct SetNames
{
    const char *id;
    uint32_t version; /* the client vector record's two version words */
    std::vector<const char *> names; /* [index - 1] */
};

#define OT_SET(id, ver, ...) SetNames{ id, ver, { __VA_ARGS__ } },
const SetNames sets[] = {
#include "ot_names.inc"
};
#undef OT_SET

const SetNames *find_set(const std::string &id)
{
    for(const auto &s : sets)
        if(id == s.id)
            return &s;
    return nullptr;
}

const char *name_of(const SetNames *s, unsigned index)
{
    if(!s || index < 1 || index > s->names.size() || !*s->names[index - 1])
        return "?";
    return s->names[index - 1];
}

/* Registered implementations, by "set/name". */
std::map<std::string, const Def *> &defs()
{
    static std::map<std::string, const Def *> m;
    return m;
}

/* ---- calls ------------------------------------------------------------ */

uint32_t enter(syn68k_addr_t, void *arg);

struct Bound
{
    const Def *def;
    syn68k_addr_t entry = 0;
};
std::map<const Def *, Bound> bound;

syn68k_addr_t entry_for(const Def *d)
{
    Bound &b = bound[d];
    if(!b.entry)
    {
        b.def = d;
        b.entry = callback_install(enter, (void *)d);
    }
    return b.entry;
}

/* A bound function: read the arguments from the caller's frame, run, and
   return as its convention says. The return address is on top of the
   stack (the glue's stub jumped here). */
syn68k_addr_t enter(syn68k_addr_t, void *arg)
{
    const Def *d = (const Def *)arg;
    uint32_t sp = EM_A7;
    Call call(sp, d->conv, d->sig);
    uint32_t ret_pc = READUL(sp);
    uint32_t result = d->impl(call);
    if(d->conv == Conv::Pascal)
    {
        uint32_t slot = sp + 4 + call.arg_bytes();
        switch(d->ret)
        {
            case 'L':
                WRITEUL(slot, result);
                break;
            case 'W':
                WRITEUW(slot, result);
                break;
            case 'B':
                WRITEUW(slot, result ? 0x0100 : 0);
                break;
        }
        EM_A7 = slot; /* the arguments popped, the result on top */
    }
    else
    {
        EM_D0 = result;
        EM_A7 = sp + 4;
    }
    return ret_pc;
}

/* A function no part implements. MacPhoenix: a C one (OT's sets name
   their Pascal functions in capitals, MPW Pascal's way, and the rest are C)
   answers kOTNotSupportedErr in D0, logged: its caller pops the
   arguments, so their size needn't be known. The TCP/IP control panel's
   first configuration-database call (OTOpenConfigurationsDB) ended the
   session. A Pascal one is still a fatal error naming it: without its
   arguments' size it can't even return an error. */
struct Missing
{
    std::string set, name;
};
bool pascal_name(const std::string &name)
{
    for(char c : name)
        if(c >= 'a' && c <= 'z')
            return false;
    return true;
}
syn68k_addr_t missing(syn68k_addr_t, void *arg)
{
    const Missing *m = (const Missing *)arg;
    if(pascal_name(m->name))
    {
        traps::log_unimplemented("Open Transport " + m->name + " (" + m->set
                                 + "), Pascal: session stopped");
        fatal("Open Transport: " + m->name + " (" + m->set + ") is not implemented");
    }
    traps::log_unimplemented("Open Transport " + m->name + " (" + m->set
                             + "): answered kOTNotSupportedErr");
    uint32_t sp = EM_A7;
    /* What it was given, for working out a private call's arguments. */
    OT_LOG("%s (%s) not implemented: called from $%08x, stack %08x %08x %08x %08x %08x %08x\n",
           m->name.c_str(), m->set.c_str(), READUL(sp), READUL(sp + 4), READUL(sp + 8),
           READUL(sp + 12), READUL(sp + 16), READUL(sp + 20), READUL(sp + 24));
    EM_D0 = (uint32_t)(int32_t)kOTNotSupportedErr;
    EM_A7 = sp + 4;
    return READUL(sp);
}

} // anonymous namespace

/* ---- shared helpers --------------------------------------------------- */

bool Executor::ot::log_enabled()
{
    return ERROR_ENABLED_P(ERROR_OT_LOG);
}

void Executor::ot::register_defs(const Def *d, size_t n)
{
    for(size_t i = 0; i < n; i++)
        defs()[std::string(d[i].set) + "/" + d[i].name] = &d[i];
}

bool Executor::ROMlib_ot_library(const std::string &name)
{
    return name == "OTClientLib" || name == "OTClientUtilLib" || name == "OTUtilityLib"
        || name == "OTInetClientLib" || name == "OTGlobalLib" || name == "OTConfigLib"
        || name == "OTATalkClientLib";
}

namespace
{
/* OTGlobalLib's data: the glue's globals and the timing parameters, which
   Executor's OT doesn't use; zeroed, in storage the guest can address. */
alignas(8) uint8_t ot_global[256];
alignas(8) uint8_t ot_micro_time_params[32];
alignas(8) uint8_t ot_tick_time_params[32];
struct OTGlobalData
{
    OTGlobalData()
    {
        builtinlibs::addPPCData("OTGlobalLib", "__gOTGlobal", ot_global);
        builtinlibs::addPPCData("OTGlobalLib", "__gOTMicroTimeParams", ot_micro_time_params);
        builtinlibs::addPPCData("OTGlobalLib", "__gOTTickTimeParams", ot_tick_time_params);
    }
};
}

/* A function from the definitions (OpenTransport.yaml): bound by 68k
   glue through its set, and imported by PowerPC code from its library
   (OTClientLib, OTUtilityLib, ...), under Apple's name. */
void Executor::traps::OTFunction::init()
{
    static OTGlobalData global_data;
    const Def *d = new Def{ set, setName, pascal ? Conv::Pascal : Conv::C, sig, ret, impl };
    register_defs(d, 1);
    builtinlibs::addPPCEntrypoint(library, name, [d](PowerCore &cpu) {
        /* Host code that runs 68k code (a notifier, a patched trap) pushes
           below the caller's frame. */
        EM_A7 = cpu.r[1];
        Call call(cpu, d->sig);
        uint32_t r = d->impl(call);
        switch(d->ret)
        {
            case 'W':
                cpu.r[3] = (uint32_t)(int32_t)(int16_t)r;
                break;
            case 'B':
                cpu.r[3] = r ? 1 : 0;
                break;
            default:
                cpu.r[3] = r;
        }
        return cpu.lr;
    });
}

void Executor::ot::fatal(const std::string &msg)
{
    fprintf(stderr, "[ot] %s\n", msg.c_str());
    if(ROMlib_fatal_hook)
    {
        auto hook = ROMlib_fatal_hook;
        ROMlib_fatal_hook = nullptr;
        hook(msg.c_str());
    }
    std::abort();
}

bool Executor::ot::is_ot_set(const std::string &id)
{
    return find_set(id) != nullptr;
}

/* The binder's work for a stub of one of OT's function sets: the
   function's entry, written into the stub record; 0 for another set. */
syn68k_addr_t Executor::ot::bind_ot_stub(uint32_t stub)
{
    uint32_t cvr = READUL(stub + 4);
    std::string id = guest_cstring(cvr + 16, 63);
    const SetNames *s = find_set(id);
    if(!s)
        return 0;
    unsigned index = READUW(stub + 12);
    const char *name = name_of(s, index);
    syn68k_addr_t addr;
    auto it = defs().find(id + "/" + name);
    if(it != defs().end())
    {
        addr = entry_for(it->second);
        OT_LOG("bind %s #%u %s: $%08x\n", id.c_str(), index, name, (unsigned)addr);
    }
    else
    {
        fprintf(stderr, "[ot] bind %s #%u %s: not implemented\n", id.c_str(), index, name);
        addr = callback_install(missing, new Missing{ id, name });
    }
    WRITEUL(stub, addr);
    return addr;
}

/* TLibraryManager::GetFunctionPointer's work (ot_slm.cpp): a function of
   one of OT's sets by the set's name for it, or by its index (1-based, as
   in the stubs); 0 if the set isn't OT's or has no such function. */
syn68k_addr_t Executor::ot::function_pointer(const std::string &id, const std::string &name)
{
    if(!find_set(id))
        return 0;
    auto it = defs().find(id + "/" + name);
    if(it == defs().end())
    {
        fprintf(stderr, "[ot] GetFunctionPointer %s %s: not implemented\n", id.c_str(), name.c_str());
        return callback_install(missing, new Missing{ id, name });
    }
    OT_LOG("GetFunctionPointer %s %s\n", id.c_str(), name.c_str());
    return entry_for(it->second);
}

syn68k_addr_t Executor::ot::function_pointer(const std::string &id, unsigned index)
{
    const SetNames *s = find_set(id);
    const char *name = name_of(s, index);
    if(!s || !strcmp(name, "?"))
        return 0;
    return function_pointer(id, std::string(name));
}

std::string Executor::ot::guest_cstring(uint32_t addr, size_t max)
{
    std::string s;
    if(!addr)
        return s;
    for(size_t i = 0; i < max; i++)
    {
        char c = READUB(addr + i);
        if(!c)
            break;
        s += c;
    }
    return s;
}

Call::Call(uint32_t sp, Conv conv, const char *sig)
    : sp_(sp), conv_(conv)
{
    for(const char *p = sig; *p && n_ < 16; p++)
        kind_[n_++] = *p;
    if(conv == Conv::C)
    {
        for(int i = 0; i < n_; i++)
            off_[i] = 4 + 4 * i;
        bytes_ = 4 * n_;
    }
    else
    {
        /* Pushed left to right: the last argument is nearest the return
           address. */
        uint32_t off = 4;
        for(int i = n_ - 1; i >= 0; i--)
        {
            off_[i] = off;
            off += kind_[i] == 'L' ? 4 : 2;
        }
        bytes_ = off - 4;
    }
}

Call::Call(PowerCore &cpu, const char *sig)
    : cpu_(&cpu)
{
    for(const char *p = sig; *p && n_ < 16; p++)
        kind_[n_++] = *p;
}

uint32_t Call::l(int i) const
{
    if(cpu_)
        return i < 8 ? cpu_->r[3 + i] : READUL(cpu_->r[1] + 24 + 4 * i);
    if(conv_ == Conv::Pascal && kind_[i] != 'L')
        return w(i);
    return READUL(sp_ + off_[i]);
}

uint16_t Call::w(int i) const
{
    if(cpu_)
        return (uint16_t)l(i);
    if(conv_ == Conv::C)
        return (uint16_t)READUL(sp_ + off_[i]);
    return READUW(sp_ + off_[i]);
}

uint8_t Call::byte(int i) const
{
    if(cpu_ || conv_ == Conv::C)
        return (uint8_t)l(i);
    return READUB(sp_ + off_[i]);
}

bool Call::b(int i) const
{
    if(cpu_)
        return (l(i) & 0xFF) != 0;
    if(conv_ == Conv::C)
        return (READUL(sp_ + off_[i]) & 0xFF) != 0;
    return READUB(sp_ + off_[i]) != 0;
}

/* ---- Gestalt ---------------------------------------------------------- */

bool Executor::ROMlib_ot_gestalt(OSType selector, int32_t *value)
{
    if(!ROMlib_open_transport || !slm_ready())
        return false;
    switch(selector)
    {
        case "otan"_4:
            /* gestaltOpenTptPresent | Loaded | TCPPresent | TCPLoaded */
            *value = 0x00000033;
            slm_expect_ot_client();
            return true;
        case "otvr"_4:
            *value = 0x01108000; /* 1.1 final, the glue's generation */
            return true;
    }
    return false;
}

void Executor::ROMlib_ot_process_start()
{
    if(ROMlib_open_transport && slm_apple_running())
        slm_ready();
}

void Executor::ROMlib_ot_process_exit(uint32_t part_lo, uint32_t part_hi)
{
    if(!ROMlib_open_transport)
        return;
    close_clients_in(part_lo, part_hi);
    tasks_process_exit(part_lo, part_hi);
    slm_process_exit(part_lo, part_hi);
}
