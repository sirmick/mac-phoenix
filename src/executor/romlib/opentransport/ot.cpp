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

#include <rsys/opentransport.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

using namespace Executor;
using namespace Executor::ot;

bool Executor::ROMlib_open_transport = false;

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

/* A function no part implements: a fatal error naming it (its arguments'
   size isn't known, so it can't even return an error to a pascal
   caller). */
struct Missing
{
    std::string set, name;
};
syn68k_addr_t missing(syn68k_addr_t, void *arg)
{
    const Missing *m = (const Missing *)arg;
    char msg[160];
    snprintf(msg, sizeof msg, "Open Transport: %s (%s) is not implemented",
             m->name.c_str(), m->set.c_str());
    fprintf(stderr, "[ot] %s\n", msg);
    if(ROMlib_fatal_hook)
    {
        auto hook = ROMlib_fatal_hook;
        ROMlib_fatal_hook = nullptr;
        hook(msg);
    }
    std::abort();
}

/* ---- the binder ------------------------------------------------------- */

uint32_t apple_binder;     /* what SLMGlobal+$88 held */
syn68k_addr_t our_binder;  /* the callback in its place */

uint32_t slm_global()
{
    uint32_t em = READUL(0x2B6);
    return em ? READUL(em + 0x10C) : 0;
}

syn68k_addr_t bind(syn68k_addr_t, void *)
{
    uint32_t stub = EM_A0;
    uint32_t cvr = READUL(stub + 4);
    std::string id = guest_cstring(cvr + 16, 63);
    const SetNames *s = find_set(id);
    if(!s)
        return apple_binder;

    unsigned index = READUW(stub + 12);
    const char *name = name_of(s, index);
    syn68k_addr_t addr;
    auto it = defs().find(id + "/" + name);
    if(it != defs().end())
    {
        addr = entry_for(it->second);
        OT_LOG("bind %s #%u %s\n", id.c_str(), index, name);
    }
    else
    {
        fprintf(stderr, "[ot] bind %s #%u %s: not implemented\n", id.c_str(),
                index, name);
        addr = callback_install(missing, new Missing{ id, name });
    }
    WRITEUL(stub, addr);
    return addr;
}

/* ---- LoadClass ---------------------------------------------------------
   InitOpenTransport's glue loads "OTClnt$LclUtils" before it binds
   anything: gLibraryManager->LoadClass(id, true), slot 16 ($40) of a
   TLibraryManager's vtable (LibraryManager.h: 9 TDynamic slots, the first
   ASLM_COMPATIBLE's dummy, then the manager's). Apple's would load Apple's
   OT library. A client's TLibraryManager comes from the root manager
   (SLMGlobal+$10), slot 39 ($9C), when its glue calls InitLibraryManager;
   that call is wrapped, and each new manager's vtable gets a LoadClass
   that answers noErr for OT's function sets and goes on to Apple's for
   the rest. */

std::map<uint32_t, uint32_t> apple_load_class; /* by vtable */
syn68k_addr_t our_load_class, our_factory;
uint32_t apple_factory;

/* OSErr LoadClass(TLibraryManager *this, const TClassID &id, Boolean) */
syn68k_addr_t load_class(syn68k_addr_t, void *)
{
    uint32_t sp = EM_A7;
    uint32_t self = READUL(sp + 4);
    std::string id = guest_cstring(READUL(sp + 8), 63);
    if(find_set(id))
    {
        OT_LOG("LoadClass \"%s\": Executor's\n", id.c_str());
        EM_D0 = 0;
        EM_A7 = sp + 4;
        return READUL(sp);
    }
    auto it = apple_load_class.find(READUL(self));
    return it != apple_load_class.end() ? it->second : apple_load_class.begin()->second;
}

/* The root manager's slot 39: (this, TLibraryManager **out, frame, 4 more),
   C. Apple's makes the manager; then its vtable is patched. */
syn68k_addr_t make_manager(syn68k_addr_t, void *)
{
    uint32_t sp = EM_A7;
    uint32_t ret = READUL(sp);
    uint32_t args[7];
    for(int i = 0; i < 7; i++)
        args[i] = READUL(sp + 4 + 4 * i);
    EM_A7 = sp - 28;
    for(int i = 0; i < 7; i++)
        WRITEUL(EM_A7 + 4 * i, args[i]);
    execute68K(apple_factory);
    uint32_t result = EM_D0;
    EM_A7 = sp;
    uint32_t manager = args[1] ? READUL(args[1]) : 0;
    if(manager)
    {
        uint32_t vt = READUL(manager);
        uint32_t slot = READUL(vt + 0x40);
        if(slot != our_load_class)
        {
            apple_load_class[vt] = slot;
            WRITEUL(vt + 0x40, our_load_class);
            OT_LOG("TLibraryManager vtable $%08x: LoadClass wrapped\n", vt);
        }
    }
    EM_D0 = result;
    EM_A7 = sp + 4;
    return ret;
}

/* Put the wrappers in place, or back after ASLM rewrote a slot. */
bool hook_binder()
{
    uint32_t slm = slm_global();
    if(!slm)
        return false;
    if(!our_binder)
    {
        register_client();
        register_utils();
        register_providers();
        register_inet();
        our_binder = callback_install(bind, nullptr);
        our_load_class = callback_install(load_class, nullptr);
        our_factory = callback_install(make_manager, nullptr);
    }
    uint32_t cur = READUL(slm + 0x88);
    if(cur != our_binder)
    {
        apple_binder = cur;
        WRITEUL(slm + 0x88, our_binder);
        OT_LOG("binder wrapped (SLMGlobal $%08x, Apple's binder $%08x)\n", slm,
               apple_binder);
    }
    uint32_t root = READUL(slm + 0x10);
    if(root)
    {
        uint32_t slot = READUL(root) + 0x9C;
        if(READUL(slot) != our_factory)
        {
            apple_factory = READUL(slot);
            WRITEUL(slot, our_factory);
            OT_LOG("root manager $%08x: manager factory wrapped\n", root);
        }
    }
    return true;
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

uint32_t Call::l(int i) const
{
    if(conv_ == Conv::Pascal && kind_[i] != 'L')
        return w(i);
    return READUL(sp_ + off_[i]);
}

uint16_t Call::w(int i) const
{
    if(conv_ == Conv::C)
        return (uint16_t)READUL(sp_ + off_[i]);
    return READUW(sp_ + off_[i]);
}

bool Call::b(int i) const
{
    if(conv_ == Conv::C)
        return (READUL(sp_ + off_[i]) & 0xFF) != 0;
    return READUB(sp_ + off_[i]) != 0;
}

/* ---- Gestalt ---------------------------------------------------------- */

bool Executor::ROMlib_ot_gestalt(OSType selector, int32_t *value)
{
    if(!ROMlib_open_transport || !hook_binder())
        return false;
    switch(selector)
    {
        case "otan"_4:
            /* gestaltOpenTptPresent | Loaded | TCPPresent | TCPLoaded */
            *value = 0x00000033;
            return true;
        case "otvr"_4:
            *value = 0x01108000; /* 1.1 final, the glue's generation */
            return true;
    }
    return false;
}
