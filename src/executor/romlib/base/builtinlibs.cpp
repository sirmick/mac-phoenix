#include <base/common.h>
#include <base/builtinlibs.h>
#include <base/cpu.h>
#include <base/traps.h>
#include <MemoryMgr.h>
#include <rsys/cfm.h>
#include <PEFBinaryFormat.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <iostream>
#include <cassert>
#include <functional>
#include <algorithm>
#include <cstring>

#include <PowerCore.h>

using namespace Executor;

namespace
{
    std::unordered_map<std::string, std::vector<map_entry_t>> builtins;

    struct PPCCallback
    {
        GUEST_STRUCT;
        GUEST<GUEST<uint32_t>*> codePtr;
        GUEST<uint32_t> rtoc;
        GUEST<uint32_t> sc;
        const char* name;
        std::function<uint32_t (PowerCore&)> implementation;
    };

    PPCCallback callbacks[4096];
    int nCallbacks = 0;

    /* MacPhoenix: CFM-68K transition vectors (docs/executor/CFM68K.md): the
       code address, then what the callee loads into A5 (nothing here). In
       static storage, which the guest can address. */
    struct CFM68KVector
    {
        GUEST_STRUCT;
        GUEST<uint32_t> code;
        GUEST<uint32_t> a5;
    };

    struct CFM68KRoutine
    {
        const char *name;
        std::function<syn68k_addr_t (syn68k_addr_t)> implementation;
    };

    CFM68KVector vectors68k[4096];
    CFM68KRoutine routines68k[std::size(vectors68k)];
    int nVectors68k = 0;

    std::unordered_map<std::string, std::vector<map_entry_t>> builtins68k;

    syn68k_addr_t call_routine68k(syn68k_addr_t addr, void *arg)
    {
        CFM68KRoutine& r = *(CFM68KRoutine *)arg;
        if(r.implementation)
        {
            /* The Toolbox finds QuickDraw's globals through A5; CFM-68K code
               has its fragment's data there. A library routine keeps D3-D7
               and A2-A6, as the caller expects; a routine that runs 68k code
               (a callback, a patch) needn't have. */
            uint32_t saved[10] = { EM_D3, EM_D4, EM_D5, EM_D6, EM_D7,
                                   EM_A2, EM_A3, EM_A4, EM_A5, EM_A6 };
            EM_A5 = guest_cast<uint32_t>(LM(CurrentA5));
            syn68k_addr_t ret = r.implementation(addr);
            EM_D3 = saved[0]; EM_D4 = saved[1]; EM_D5 = saved[2];
            EM_D6 = saved[3]; EM_D7 = saved[4];
            EM_A2 = saved[5]; EM_A3 = saved[6]; EM_A4 = saved[7];
            EM_A5 = saved[8]; EM_A6 = saved[9];
            return ret;
        }
        /* An import nothing exports: the caller pops its arguments, so the
           call can be answered. */
        traps::log_unimplemented(std::string("CFM-68K ") + r.name);
        EM_D0 = EM_A0 = 0;
        return POPADDR();
    }

    CFM68KVector *new_vector68k(const char *name, std::function<syn68k_addr_t (syn68k_addr_t)> code)
    {
        assert(nVectors68k < (int)std::size(vectors68k));
        int i = nVectors68k++;
        routines68k[i] = { name, std::move(code) };
        vectors68k[i].code = callback_install(&call_routine68k, &routines68k[i]);
        vectors68k[i].a5 = 0;
        return &vectors68k[i];
    }

    /* InterfaceLib's variadic routines: the caller pops their arguments.
       A routine with fixed arguments pops its own in CFM-68K, C ones
       (c2pstr) as much as Pascal ones (seen in the callers' code). */
    const char *const c_routines68k[] = {
        "CallUniversalProc", "CallOSTrapUniversalProc",
        "AEBuild", "AEBuildDesc", "AEBuildParameters", "AEBuildAppleEvent",
    };

    /* Apple's other libraries a CFM-68K application imports from, answered
       from InterfaceLib's routines when no file provides them. */
    const char *const interfacelib_aliases68k[] = {
        "DragLib", "ThreadsLib", "AppearanceLib", "WindowsLib", "ControlsLib",
        "MenusLib", "DialogsLib", "PrivateInterfaceLib", "ObjectSupportLib",
    };
}

uint32_t builtinlibs::handleSC(PowerCore& cpu)
{
    PPCCallback *cb = guestvalues::GuestTypeTraits<PPCCallback*>::reg_to_host(cpu.CIA - 8);
    int index = cb - callbacks;
    if(index >= 0 && index < nCallbacks)
    {
        if(cb->implementation)
            return cb->implementation(cpu);
        else
        {
            std::cerr << "Unimplemented PPC entrypoint: " << cb->name << std::endl << std::flush;
            std::abort();
        }
    }
    else
    {
        std::cerr << "Illegal PPC system call\n";
        std::abort();
    }
}

void builtinlibs::addPPCEntrypoint(const char *library, const char *function, std::function<uint32_t (PowerCore&)> code)
{
    assert(nCallbacks < std::size(callbacks));
    PPCCallback& callback = callbacks[nCallbacks++];
    callback.sc = POWERCORE_OP_SYSCALL;
    callback.codePtr = &callback.sc;
    callback.implementation = code;
    callback.name = function;

    builtins[library].push_back({function, &callback});
}

static std::unordered_map<std::string, std::function<syn68k_addr_t (syn68k_addr_t)>>& overrides68k()
{
    static std::unordered_map<std::string, std::function<syn68k_addr_t (syn68k_addr_t)>> map;
    return map;
}

void builtinlibs::overrideCFM68KEntrypoint(const char *function,
                                           std::function<syn68k_addr_t (syn68k_addr_t)> code)
{
    /* Called from static initializers: only noted here; the entry takes it
       when it is made, or InterfaceLib gets it when first asked for. */
    overrides68k()[function] = code;
}

void builtinlibs::addCFM68KEntrypoint(const char *library, const char *function,
                                      std::function<syn68k_addr_t (syn68k_addr_t)> code)
{
    if(auto it = overrides68k().find(function); it != overrides68k().end())
        code = it->second;
    auto& entries = builtins68k[library];
    for(auto& e : entries)
        if(!strcmp(e.symbol_name, function))
        {
            int i = (CFM68KVector *)e.value - vectors68k;
            routines68k[i].implementation = std::move(code);
            return;
        }
    entries.push_back({function, new_vector68k(function, std::move(code))});
}

bool builtinlibs::cfm68kCalleePops(const char *function)
{
    for(const char *c : c_routines68k)
        if(!strcmp(c, function))
            return false;
    return true;
}

void builtinlibs::addCFM68KData(const char *library, const char *name, void *address)
{
    builtins68k[library].push_back({name, address});
}

Ptr builtinlibs::makeUndefinedSymbolStub(const char *name, OSType arch)
{
    if(arch == "m68k"_4)
        return (Ptr)new_vector68k(name, nullptr);

    assert(nCallbacks < std::size(callbacks));
    PPCCallback& callback = callbacks[nCallbacks++];
    callback.sc = POWERCORE_OP_SYSCALL;
    callback.codePtr = &callback.sc;
    callback.implementation = nullptr;
    callback.name = name;
    return (Ptr) &callback;
}

ConnectionID builtinlibs::getBuiltinLib(ConstStr255Param libname, OSType arch)
{
    static bool glue_added = false;
    if(!glue_added)
    {
        glue_added = true;
        addGlueEntrypoints();
    }

    std::string name(libname + 1, libname + 1 + libname[0]);
    if(arch == "m68k"_4)
    {
        if(std::find(std::begin(interfacelib_aliases68k), std::end(interfacelib_aliases68k), name)
           != std::end(interfacelib_aliases68k))
            name = "InterfaceLib";
        /* One connection per library: its exports never move. */
        static std::unordered_map<std::string, ConnectionID> connections;
        ConnectionID& cid = connections[name];
        if(!cid && name == "InterfaceLib")
            for(auto& [function, code] : overrides68k())
            {
                auto& entries = builtins68k[name];
                if(std::none_of(entries.begin(), entries.end(),
                                [&](const map_entry_t& e) { return function == e.symbol_name; }))
                    entries.push_back({function.c_str(), new_vector68k(function.c_str(), code)});
            }
        auto it = builtins68k.find(name);
        if(it == builtins68k.end())
            return nullptr;
        if(!cid)
        {
            cid = ROMlib_new_connection(1);
            cid->lihp = ROMlib_build_pef_hash(it->second.data(), it->second.size());
            cid->ref_count = 1;
        }
        return cid;
    }

    auto it = builtins.find(name);
    if(it == builtins.end())
        return nullptr;
    else
    {
        ConnectionID cid = ROMlib_new_connection(1);
        cid->lihp = ROMlib_build_pef_hash(it->second.data(), it->second.size());
        cid->ref_count = 1;
        return cid;
    }
}
